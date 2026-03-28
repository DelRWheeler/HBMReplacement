#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Hardware Abstraction Layer
 *
 * Real hardware: SPI to MCP3561, UART to RS485
 * Simulator: file-backed config, PTY serial, synthetic ADC data
 */

/* --- Serial (RS485) --- */
int     hal_serial_init(uint32_t baudrate);
int     hal_serial_write(const uint8_t *data, size_t len);
int     hal_serial_read(uint8_t *data, size_t max_len);
int     hal_serial_available(void);
void    hal_serial_flush(void);
void    hal_serial_cleanup(void);

/* --- ADC (MCP3561 / Load Cell 6 Click) --- */
int     hal_adc_init(void);
bool    hal_adc_data_ready(void);
int32_t hal_adc_read(void);
void    hal_adc_set_osr(uint16_t osr);
void    hal_adc_cleanup(void);

/* --- Configuration Storage --- */
int     hal_config_read(uint8_t *buf, size_t len);
int     hal_config_write(const uint8_t *buf, size_t len);

/* --- System --- */
void    hal_init(void);
void    hal_sleep_ms(uint32_t ms);
uint64_t hal_time_us(void);
void    hal_led_set(bool on);

/* --- Multicore (no-op on simulator) --- */
void    hal_launch_core1(void (*entry)(void));
void    hal_lockout_start(void);
void    hal_lockout_end(void);

#endif /* HAL_H */
