// ST7701 panel driver for the Pimoroni Presto, 8-bit palette mode at 480x480.
//
// C port of Pimoroni's driver (pimoroni/presto drivers/st7701, MIT licence, see LICENSE.pimoroni),
// "mainly due to Michael Bell and Phil Howard (Gadgetoid)", with the palette mode based on Dmitry
// GR's. Only the full-resolution palette path is kept.
//
// How it runs: the timing state machine makes HSYNC/VSYNC/DE and the dot clock from words the
// timing ISR feeds it. For each display line it raises PIO IRQ 0, and at the end of the frame
// IRQ 1; the end-of-line ISR then points DMA at the next line of the framebuffer. That DMA feeds
// the palette state machine bytes, which turns each into the address of its palette entry; two
// more DMA channels fetch that entry and hand it to the parallel state machine, which drives the
// 18 data pins.
#include "st7701.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/sync.h"

#include "st7701_palette.pio.h"
#include "st7701_parallel.pio.h"
#include "st7701_timing.pio.h"

// The Presto's wiring
#define LCD_SPI       spi1
#define LCD_CS        28
#define LCD_CLK       26
#define LCD_DAT       27
#define LCD_BACKLIGHT 45
#define LCD_D0        1     // the first of 18 data pins
#define LCD_HSYNC     19
#define LCD_VSYNC     20
#define LCD_DE        21
#define LCD_DOT_CLK   22

#define SPI_BAUD          8000000
#define BACKLIGHT_PWM_TOP 6200

#define WIDTH  ST7701_WIDTH
#define HEIGHT ST7701_HEIGHT

#define TIMING_V_PULSE   8
#define TIMING_V_BACK    (5 + TIMING_V_PULSE)
#define TIMING_V_DISPLAY (HEIGHT + TIMING_V_BACK)
#define TIMING_V_FRONT   (5 + TIMING_V_DISPLAY)
#define TIMING_H_FRONT   4
#define TIMING_H_PULSE   16
#define TIMING_H_BACK    30
#define TIMING_H_DISPLAY WIDTH

static PIO st_pio;
static uint parallel_sm, timing_sm, palette_sm;
static uint parallel_offset;
static uint st_dma, st_dma2, st_dma3, st_dma4;

static uint16_t timing_row, timing_phase;
static volatile uint32_t frames;

static uint8_t *framebuffer;
static uint8_t *volatile next_framebuffer;
static uint32_t *palette;
static uint8_t *volatile next_line_addr;
static int display_row;

// -- interrupts (from RAM: they mustn't wait on flash)

// The timing state machine's FIFO has room: queue its next phases.
static void __isr __not_in_flash_func(timing_isr)(void) {
    while (!pio_sm_is_tx_fifo_full(st_pio, timing_sm)) {
        uint32_t instr;
        switch (timing_phase) {
            case 0:     // front porch
                instr = 0x4000B042u;    // HSYNC high, NOP
                if (timing_row >= TIMING_V_PULSE) instr |= 0x80000000u;    // VSYNC high outside its pulse
                instr |= (TIMING_H_FRONT - 3) << 16;
                break;
            case 1:     // HSYNC
                instr = 0x0000B042u;    // HSYNC low, NOP
                if (timing_row >= TIMING_V_PULSE) instr |= 0x80000000u;
                instr |= (TIMING_H_PULSE - 3) << 16;
                break;
            case 2:     // back porch; in the display window, IRQ 4 starts the data state machine
                instr = 0x40000000u;
                if (timing_row >= TIMING_V_PULSE) instr |= 0x80000000u;
                if (timing_row >= TIMING_V_BACK && timing_row < TIMING_V_DISPLAY) instr |= 0xD004u;
                else instr |= 0xB042u;
                instr |= (TIMING_H_BACK - 3) << 16;
                break;
            default:    // display; IRQ 0 queues the next line's DMA, IRQ 1 the next frame's
                instr = 0x40000000u;
                if (timing_row == TIMING_V_DISPLAY) instr |= 0xD001u;
                else if (timing_row >= TIMING_V_BACK - 1 && timing_row < TIMING_V_DISPLAY) instr |= 0xD000u;
                else instr |= 0xB042u;
                if (timing_row >= TIMING_V_PULSE) instr |= 0x80000000u;
                instr |= (TIMING_H_DISPLAY - 3) << 16;
                if (++timing_row >= TIMING_V_FRONT) timing_row = 0;
                break;
        }
        pio_sm_put(st_pio, timing_sm, instr);
        timing_phase = (timing_phase + 1) & 3;
    }
}

static void __not_in_flash_func(start_line_xfer)(void) {
    hw_clear_bits(&st_pio->irq, 0x1);
    ++display_row;
    next_line_addr = display_row == HEIGHT ? NULL : framebuffer + WIDTH * display_row;
}

static void __not_in_flash_func(start_frame_xfer)(void) {
    hw_clear_bits(&st_pio->irq, 0x2);

    if (next_framebuffer) {
        framebuffer = next_framebuffer;
        next_framebuffer = NULL;
    }

    next_line_addr = NULL;
    dma_channel_abort(st_dma);
    dma_channel_wait_for_finish_blocking(st_dma);
    pio_sm_set_enabled(st_pio, parallel_sm, false);
    pio_sm_clear_fifos(st_pio, parallel_sm);
    // Restart rather than exec an `out`, which stalls forever with an empty FIFO (Pimoroni's note);
    // X and Y survive, so the width count in Y is kept.
    pio_sm_restart(st_pio, parallel_sm);
    pio_sm_exec(st_pio, parallel_sm, pio_encode_jmp(parallel_offset));
    pio_sm_set_enabled(st_pio, parallel_sm, true);
    display_row = 0;
    next_line_addr = framebuffer;
    dma_channel_set_read_addr(st_dma, framebuffer, true);

    frames++;
    __sev();
}

static void __isr __not_in_flash_func(end_of_line_isr)(void) {
    if (st_pio->irq & 0x2) start_frame_xfer();
    else start_line_xfer();
}

// -- the panel's set-up commands, over 9-bit SPI (the 9th bit is D/CX)

static void command(uint8_t cmd, size_t len, const char *data) {
    uint16_t buf[20];
    gpio_put(LCD_CS, 0);
    uint16_t c = cmd;                       // D/CX low: a command
    spi_write16_blocking(LCD_SPI, &c, 1);
    if (data) {
        for (size_t i = 0; i < len; i++) buf[i] = 0x100 | (uint8_t)data[i];    // D/CX high: data
        spi_write16_blocking(LCD_SPI, buf, len);
    }
    gpio_put(LCD_CS, 1);
}

static const char CN2BKxDIS[] = {0x77, 0x01, 0x00, 0x00, 0x00};
static const char CN2BK0SEL[] = {0x77, 0x01, 0x00, 0x00, 0x10};
static const char CN2BK1SEL[] = {0x77, 0x01, 0x00, 0x00, 0x11};
static const char CN2BK3SEL[] = {0x77, 0x01, 0x00, 0x00, 0x13};
#define BANK(sel) command(0xFF, sizeof(sel), sel)

static void panel_init(void) {
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, BACKLIGHT_PWM_TOP);
    pwm_init(pwm_gpio_to_slice_num(LCD_BACKLIGHT), &cfg, true);
    gpio_set_function(LCD_BACKLIGHT, GPIO_FUNC_PWM);
    st7701_set_backlight(0);

    command(0x01, 0, NULL);     // SWRESET
    sleep_ms(150);

    BANK(CN2BK0SEL);
    command(0xC0, 2, "\x3b\x00");   // LNESET: (59 + 1) * 8 = 480 lines
    command(0xC1, 2, "\x0d\x02");   // PORCTRL: 13 VBP, 2 VFP
    command(0xC2, 2, "\x31\x01");   // INVSET
    command(0xCD, 1, "\x08");       // COLCTRL: LED polarity reversed
    command(0xB0, 16, "\x00\x11\x18\x0e\x11\x06\x07\x08\x07\x22\x04\x12\x0f\xaa\x31\x18");  // gamma +
    command(0xB1, 16, "\x00\x11\x19\x0e\x12\x07\x08\x08\x08\x22\x04\x11\x11\xa9\x32\x18");  // gamma -
    command(0xC3, 3, "\x80\x2e\x0e");   // RGBCTRL: HV mode, back porch and sync

    BANK(CN2BK1SEL);
    command(0xB0, 1, "\x60");   // VHRS 4.7375 V
    command(0xB1, 1, "\x32");   // VCOMS 0.725 V
    command(0xB2, 1, "\x07");   // VGHSS 15 V
    command(0xB3, 1, "\x80");   // TESTCMD
    command(0xB5, 1, "\x49");   // VGLS -10.17 V
    command(0xB7, 1, "\x85");   // PWCTRL1
    command(0xB8, 1, "\x21");   // PWCTRL2
    command(0xC1, 1, "\x78");   // PDR1 1.6 us
    command(0xC2, 1, "\x78");   // PDR2 6.4 us

    // Undocumented, specific to this panel (TL040WVS03CT15-H1263A); it doesn't work without them
    command(0xE0, 3, "\x00\x1b\x02");
    command(0xE1, 11, "\x08\xa0\x00\x00\x07\xa0\x00\x00\x00\x44\x44");
    command(0xE2, 12, "\x11\x11\x44\x44\xed\xa0\x00\x00\xec\xa0\x00\x00");
    command(0xE3, 4, "\x00\x00\x11\x11");
    command(0xE4, 2, "\x44\x44");
    command(0xE5, 16, "\x0a\xe9\xd8\xa0\x0c\xeb\xd8\xa0\x0e\xed\xd8\xa0\x10\xef\xd8\xa0");
    command(0xE6, 4, "\x00\x00\x11\x11");
    command(0xE7, 2, "\x44\x44");
    command(0xE8, 16, "\x09\xe8\xd8\xa0\x0b\xea\xd8\xa0\x0d\xec\xd8\xa0\x0f\xee\xd8\xa0");
    command(0xEB, 7, "\x02\x00\xe4\xe4\x88\x00\x40");
    command(0xEC, 2, "\x3c\x00");
    command(0xED, 16, "\xab\x89\x76\x54\x02\xff\xff\xff\xff\xff\xff\x20\x45\x67\x98\xba");
    command(0x36, 1, "\x00");
    BANK(CN2BK3SEL);
    command(0xE5, 1, "\xe4");

    BANK(CN2BKxDIS);
    command(0x3A, 1, "\x66");   // COLMOD: 18 bits a pixel

    // Rotation 0: MADCTL and SDIR unmirrored
    command(0x36, 1, "\x00");
    BANK(CN2BK0SEL);
    command(0xC7, 1, "\x00");
    BANK(CN2BKxDIS);

    command(0x21, 0, NULL);     // INVON
    sleep_ms(1);
    command(0x11, 0, NULL);     // SLPOUT
    sleep_ms(120);
    command(0x29, 0, NULL);     // DISPON
    sleep_ms(100);
    st7701_set_backlight(255);
}

void st7701_init(uint8_t *fb, uint32_t *pal) {
    framebuffer = fb;
    palette = pal;

    // The panel's DMA must win the bus over the CPUs, or lines break up under load
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    st_pio = pio1;
    parallel_sm = pio_claim_unused_sm(st_pio, true);
    timing_sm = pio_claim_unused_sm(st_pio, true);
    palette_sm = pio_claim_unused_sm(st_pio, true);
    uint timing_offset = pio_add_program(st_pio, &st7701_timing_program);
    parallel_offset = pio_add_program(st_pio, &st7701_parallel_18bpp_program);
    uint palette_offset = pio_add_program(st_pio, &st7701_palette_program);

    spi_init(LCD_SPI, SPI_BAUD);
    gpio_init(LCD_CS);
    gpio_set_dir(LCD_CS, GPIO_OUT);
    gpio_put(LCD_CS, 1);
    gpio_set_function(LCD_DAT, GPIO_FUNC_SPI);
    gpio_set_function(LCD_CLK, GPIO_FUNC_SPI);
    spi_set_format(LCD_SPI, 9, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    pio_gpio_init(st_pio, LCD_HSYNC);
    pio_gpio_init(st_pio, LCD_VSYNC);
    pio_gpio_init(st_pio, LCD_DE);
    pio_gpio_init(st_pio, LCD_DOT_CLK);
    for (uint i = 0; i < 18; i++) pio_gpio_init(st_pio, LCD_D0 + i);
    pio_sm_set_consecutive_pindirs(st_pio, parallel_sm, LCD_D0, 18, true);
    pio_sm_set_consecutive_pindirs(st_pio, parallel_sm, LCD_HSYNC, 4, true);

    // Parallel output: two pixels a loop, at twice the timing state machine's rate
    pio_sm_config c = st7701_parallel_18bpp_program_get_default_config(parallel_offset);
    sm_config_set_out_pins(&c, LCD_D0, 18);
    sm_config_set_sideset_pins(&c, LCD_DE);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_in_shift(&c, false, false, 32);

    const uint32_t max_pio_clk = 34 * MHZ;
    uint32_t clk_div = (clock_get_hz(clk_sys) + max_pio_clk - 1) / max_pio_clk;
    if (clk_div < 8) clk_div = 8;   // time for the palette look-up
    if (clk_div & 1) clk_div += 1;
    sm_config_set_clkdiv(&c, clk_div >> 1);
    pio_sm_init(st_pio, parallel_sm, parallel_offset, &c);
    pio_sm_exec(st_pio, parallel_sm, pio_encode_out(pio_y, 32));
    pio_sm_put(st_pio, parallel_sm, (WIDTH >> 1) - 1);
    pio_sm_set_enabled(st_pio, parallel_sm, true);

    c = st7701_timing_program_get_default_config(timing_offset);
    sm_config_set_out_pins(&c, LCD_HSYNC, 2);
    sm_config_set_sideset_pins(&c, LCD_DOT_CLK);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_clkdiv(&c, clk_div);
    pio_sm_init(st_pio, timing_sm, timing_offset, &c);
    pio_sm_set_enabled(st_pio, timing_sm, true);

    // Palette: X holds the palette's address >> 10; each byte in becomes &palette[byte] out
    c = st7701_palette_program_get_default_config(palette_offset);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_in_shift(&c, true, true, 30);
    pio_sm_init(st_pio, palette_sm, palette_offset, &c);
    pio_sm_exec(st_pio, palette_sm, pio_encode_out(pio_x, 32));
    pio_sm_put(st_pio, palette_sm, ((uintptr_t)palette) >> 10);
    pio_sm_set_enabled(st_pio, palette_sm, true);

    st_dma = dma_claim_unused_channel(true);
    st_dma2 = dma_claim_unused_channel(true);
    st_dma3 = dma_claim_unused_channel(true);
    st_dma4 = dma_claim_unused_channel(true);

    // st_dma: a line of pixel bytes into the palette state machine; then st_dma2 reloads it with
    // the next line's address (a null address is a null trigger, which ends the frame)
    dma_channel_config cfg = dma_channel_get_default_config(st_dma);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(st_pio, palette_sm, true));
    channel_config_set_bswap(&cfg, true);
    channel_config_set_chain_to(&cfg, st_dma2);
    dma_channel_configure(st_dma, &cfg, &st_pio->txf[palette_sm], NULL, WIDTH >> 2, false);

    cfg = dma_channel_get_default_config(st_dma2);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    dma_channel_configure(st_dma2, &cfg, &dma_hw->ch[st_dma].al3_read_addr_trig, &next_line_addr, 1, false);

    // st_dma4 takes each palette address from the palette state machine and starts st_dma3 on
    // it, which copies that entry to the parallel state machine, then restarts st_dma4
    cfg = dma_channel_get_default_config(st_dma3);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(st_pio, parallel_sm, true));
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_chain_to(&cfg, st_dma4);
    dma_channel_configure(st_dma3, &cfg, &st_pio->txf[parallel_sm], NULL, 1, false);

    cfg = dma_channel_get_default_config(st_dma4);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(st_pio, palette_sm, false));
    channel_config_set_read_increment(&cfg, false);
    dma_channel_configure(st_dma4, &cfg, &dma_hw->ch[st_dma3].al3_read_addr_trig, &st_pio->rxf[palette_sm], 1, true);

    panel_init();

    // Timing: refill the timing state machine whenever its FIFO isn't full (PIO IRQ line 1)
    hw_set_bits(&st_pio->inte1, 0x010 << timing_sm);
    irq_set_exclusive_handler(pio_get_irq_num(st_pio, 1), timing_isr);
    irq_set_priority(pio_get_irq_num(st_pio, 1), 0x40);
    irq_set_enabled(pio_get_irq_num(st_pio, 1), true);

    // Lines and frames: state machine IRQ flags 0 and 1 (PIO IRQ line 0)
    hw_set_bits(&st_pio->inte0, 0x300);
    irq_set_exclusive_handler(pio_get_irq_num(st_pio, 0), end_of_line_isr);
    irq_set_priority(pio_get_irq_num(st_pio, 0), 0x40);
    irq_set_enabled(pio_get_irq_num(st_pio, 0), true);
}

void st7701_set_palette(uint8_t entry, uint8_t r, uint8_t g, uint8_t b) {
    if (!palette) return;   // before st7701_init
    // RGB666 at the top of the word plus red's low bit; the PIO reverses the bits on the way out
    palette[entry] = (((uint32_t)r << 24) & 0xF8000000u) |
                     (((uint32_t)g << 19) & 0x07E00000u) |
                     (((uint32_t)b << 13) & 0x001F8000u) |
                     (((uint32_t)r << 12) & 0x00004000u);
}

void st7701_set_framebuffer(uint8_t *fb) {
    next_framebuffer = fb;
}

void __not_in_flash_func(st7701_wait_vsync)(void) {
    uint32_t f = frames;
    while (frames == f) __wfe();
}

void st7701_set_backlight(uint8_t brightness) {
    // Pimoroni's curve: a usable range across the 0-255 scale
    uint16_t value;
    if (brightness == 0) value = 0;
    else if (brightness == 255) value = BACKLIGHT_PWM_TOP;
    else value = 181 + (brightness * brightness) / 85;
    pwm_set_gpio_level(LCD_BACKLIGHT, value);
}

uint32_t st7701_frames(void) {
    return frames;
}
