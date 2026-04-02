#include "../src/hal.h"
#include "../config/board_pins.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

#include <string.h>
#include <stdio.h>

/*
 * RP2040 Hardware HAL
 *
 * Serial: UART1 -> RS485 Isolator 3 Click (half-duplex via DE pin)
 * ADC:    SPI0  -> MCP3561 on Load Cell 6 Click
 * Config: Last flash sector (4KB)
 * Cores:  RP2040 multicore
 */

/* ================================================================ */
/* MCP3561 Register Definitions                                      */
/* ================================================================ */

/* Fast command byte: device addr (upper 2 bits = 01) + register + R/W */
#define MCP3561_DEV_ADDR        0x01

/* Register addresses */
#define MCP3561_REG_ADCDATA     0x0
#define MCP3561_REG_CONFIG0     0x1
#define MCP3561_REG_CONFIG1     0x2
#define MCP3561_REG_CONFIG2     0x3
#define MCP3561_REG_CONFIG3     0x4
#define MCP3561_REG_IRQ         0x5
#define MCP3561_REG_MUX         0x6

/* Command byte construction */
#define MCP3561_CMD_READ(reg)   ((MCP3561_DEV_ADDR << 6) | ((reg) << 2) | 0x01)
#define MCP3561_CMD_WRITE(reg)  ((MCP3561_DEV_ADDR << 6) | ((reg) << 2) | 0x02)

/* CONFIG0: VREF=internal, CLK=internal, ADC mode=conversion */
#define MCP3561_CONFIG0_DEFAULT 0xE3    /* Internal VREF, internal CLK, conv mode */

/* CONFIG1: OSR selection (bits 5:2) */
#define MCP3561_OSR_128         0x04
#define MCP3561_OSR_256         0x05
#define MCP3561_OSR_512         0x06
#define MCP3561_OSR_1024        0x07
#define MCP3561_OSR_2048        0x08
#define MCP3561_OSR_4096        0x09
#define MCP3561_OSR_8192        0x0A
#define MCP3561_OSR_16384       0x0B

/* CONFIG2: Gain=1x, AZ_MUX=1 (auto-zero) */
#define MCP3561_CONFIG2_DEFAULT 0x8B

/* CONFIG3: Conv mode=continuous, DATA_FORMAT=32-bit sign-extended */
#define MCP3561_CONFIG3_DEFAULT 0xF0

/* IRQ: Data ready on IRQ pin, inactive high */
#define MCP3561_IRQ_DEFAULT     0x73

/* MUX: CH0=VIN+, CH1=VIN- (differential) */
#define MCP3561_MUX_DEFAULT     0x01

/* ================================================================ */
/* Serial (RS485 via UART1)                                          */
/* ================================================================ */

static inline void rs485_driver_enable(void)
{
    /* Full-duplex RS485: driver is always on.
     * Separate TX (A/B) and RX (Y/Z) pairs — no bus contention. */
    gpio_put(PIN_RS485_DE, 1);
}

int hal_serial_init(uint32_t baudrate)
{
    /* Init UART */
    uart_init(UART_PORT, baudrate);
    gpio_set_function(PIN_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UART_RX, GPIO_FUNC_UART);
    uart_set_format(UART_PORT, UART_DATA_BITS, UART_STOP_BITS, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_PORT, true);

    /* DE pin — enable driver permanently for full-duplex RS485 */
    gpio_init(PIN_RS485_DE);
    gpio_set_dir(PIN_RS485_DE, GPIO_OUT);
    rs485_driver_enable();

    return 0;
}

int hal_serial_write(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_putc_raw(UART_PORT, data[i]);
    }

    return (int)len;
}

int hal_serial_read(uint8_t *data, size_t max_len)
{
    size_t count = 0;
    while (count < max_len && uart_is_readable(UART_PORT)) {
        data[count++] = uart_getc(UART_PORT);
    }
    return (int)count;
}

int hal_serial_available(void)
{
    return uart_is_readable(UART_PORT) ? 1 : 0;
}

void hal_serial_flush(void)
{
    while (uart_is_readable(UART_PORT)) {
        uart_getc(UART_PORT);
    }
}

void hal_serial_cleanup(void)
{
    uart_deinit(UART_PORT);
}

/* ================================================================ */
/* ADC (MCP3561 via SPI0)                                            */
/* ================================================================ */

static uint8_t current_osr_bits = MCP3561_OSR_1024;

static void mcp3561_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t cmd = MCP3561_CMD_WRITE(reg);
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_write_blocking(SPI_PORT, &value, 1);
    gpio_put(PIN_SPI_CS, 1);
}

static uint8_t mcp3561_read_reg(uint8_t reg)
{
    uint8_t cmd = MCP3561_CMD_READ(reg);
    uint8_t val = 0;
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_read_blocking(SPI_PORT, 0x00, &val, 1);
    gpio_put(PIN_SPI_CS, 1);
    return val;
}

static int32_t mcp3561_read_adc(void)
{
    uint8_t cmd = MCP3561_CMD_READ(MCP3561_REG_ADCDATA);
    uint8_t buf[4] = {0};

    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_read_blocking(SPI_PORT, 0x00, buf, 4);
    gpio_put(PIN_SPI_CS, 1);

    /* 32-bit sign-extended format (CONFIG3 DATA_FORMAT = 11) */
    int32_t result = ((int32_t)buf[0] << 24) |
                     ((int32_t)buf[1] << 16) |
                     ((int32_t)buf[2] << 8)  |
                     (int32_t)buf[3];
    return result;
}

int hal_adc_init(void)
{
    /* Init SPI */
    spi_init(SPI_PORT, SPI_BAUDRATE);
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);

    /* CS as GPIO (manual control) */
    gpio_init(PIN_SPI_CS);
    gpio_set_dir(PIN_SPI_CS, GPIO_OUT);
    gpio_put(PIN_SPI_CS, 1);

    /* DRDY as input */
    gpio_init(PIN_ADC_DRDY);
    gpio_set_dir(PIN_ADC_DRDY, GPIO_IN);
    gpio_pull_up(PIN_ADC_DRDY);

    /* Reset pin */
    gpio_init(PIN_ADC_RST);
    gpio_set_dir(PIN_ADC_RST, GPIO_OUT);

    /* Hardware reset */
    gpio_put(PIN_ADC_RST, 0);
    busy_wait_ms(10);
    gpio_put(PIN_ADC_RST, 1);
    busy_wait_ms(10);

    /* Configure MCP3561 */
    mcp3561_write_reg(MCP3561_REG_CONFIG0, MCP3561_CONFIG0_DEFAULT);
    mcp3561_write_reg(MCP3561_REG_CONFIG1, (current_osr_bits << 2));
    mcp3561_write_reg(MCP3561_REG_CONFIG2, MCP3561_CONFIG2_DEFAULT);
    mcp3561_write_reg(MCP3561_REG_CONFIG3, MCP3561_CONFIG3_DEFAULT);
    mcp3561_write_reg(MCP3561_REG_IRQ, MCP3561_IRQ_DEFAULT);
    mcp3561_write_reg(MCP3561_REG_MUX, MCP3561_MUX_DEFAULT);

    return 0;
}

bool hal_adc_data_ready(void)
{
    /* DRDY pin is active low */
    return !gpio_get(PIN_ADC_DRDY);
}

int32_t hal_adc_read(void)
{
    return mcp3561_read_adc();
}

void hal_adc_set_osr(uint16_t osr)
{
    /* Map OSR value to register bits */
    switch (osr) {
        case 128:   current_osr_bits = MCP3561_OSR_128;   break;
        case 256:   current_osr_bits = MCP3561_OSR_256;   break;
        case 512:   current_osr_bits = MCP3561_OSR_512;   break;
        case 1024:  current_osr_bits = MCP3561_OSR_1024;  break;
        case 2048:  current_osr_bits = MCP3561_OSR_2048;  break;
        case 4096:  current_osr_bits = MCP3561_OSR_4096;  break;
        case 8192:  current_osr_bits = MCP3561_OSR_8192;  break;
        case 16384: current_osr_bits = MCP3561_OSR_16384; break;
        default:    current_osr_bits = MCP3561_OSR_1024;  break;
    }
    mcp3561_write_reg(MCP3561_REG_CONFIG1, (current_osr_bits << 2));
}

void hal_adc_cleanup(void)
{
    spi_deinit(SPI_PORT);
}

/* ================================================================ */
/* Configuration Storage (Flash)                                     */
/* ================================================================ */

/*
 * Use the last 4KB sector of flash for config storage.
 * RP2040 flash starts at 0x10000000, total 2MB.
 * Last sector: 0x101FF000 (offset 0x1FF000).
 */
#define FLASH_CONFIG_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

int hal_config_read(uint8_t *buf, size_t len)
{
    const uint8_t *flash_data = (const uint8_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);
    if (len > FLASH_SECTOR_SIZE) return -1;
    memcpy(buf, flash_data, len);
    return 0;
}

int hal_config_write(const uint8_t *buf, size_t len)
{
    if (len > FLASH_SECTOR_SIZE) return -1;

    /* Flash writes require interrupts disabled and core1 paused */
    uint32_t ints = save_and_disable_interrupts();

    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, buf, (len + 255) & ~255);

    restore_interrupts(ints);
    return 0;
}

/* ================================================================ */
/* System                                                            */
/* ================================================================ */

void hal_init(void)
{
    stdio_init_all();
    /* Small delay to let USB CDC enumerate (for debug printf) */
    sleep_ms(100);
}

void hal_sleep_ms(uint32_t ms)
{
    if (ms == 0) {
        /* Yield — tight_loop_contents() on Pico */
        tight_loop_contents();
        return;
    }
    sleep_ms(ms);
}

uint64_t hal_time_us(void)
{
    return time_us_64();
}

void hal_led_set(bool on)
{
    static bool inited = false;
    if (!inited) {
        gpio_init(PIN_LED);
        gpio_set_dir(PIN_LED, GPIO_OUT);
        inited = true;
    }
    gpio_put(PIN_LED, on);
}

/* ================================================================ */
/* Multicore                                                         */
/* ================================================================ */

void hal_launch_core1(void (*entry)(void))
{
    multicore_launch_core1(entry);
}

void hal_lockout_start(void)
{
    multicore_lockout_start_blocking();
}

void hal_lockout_end(void)
{
    multicore_lockout_end_blocking();
}
