#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * GPIO assignments for MIKROE-4985 Click Shield for Pi Pico
 * IMPORTANT: Verify these against the physical board with a multimeter
 * before first power-on.
 *
 * Socket 1: Load Cell 6 Click (SPI)
 * Socket 2: RS485 Isolator 3 Click (UART)
 */

/* --- Socket 1: Load Cell 6 Click (MCP3561) --- */
#define PIN_SPI_SCK     18      /* SPI0 SCK */
#define PIN_SPI_MOSI    19      /* SPI0 TX  */
#define PIN_SPI_MISO    16      /* SPI0 RX  */
#define PIN_SPI_CS      17      /* SPI0 CS  */
#define PIN_ADC_DRDY    20      /* MCP3561 Data Ready (INT pin) */
#define PIN_ADC_RST     21      /* MCP3561 Reset */

#define SPI_PORT        spi0
#define SPI_BAUDRATE    5000000 /* 5 MHz SPI clock */

/* --- Socket 2: RS485 Isolator 3 Click --- */
#define PIN_UART_TX     4       /* UART1 TX */
#define PIN_UART_RX     5       /* UART1 RX */
#define PIN_RS485_DE    2       /* RS485 Driver Enable / ~Receiver Enable */

#define UART_PORT       uart1
#define UART_BAUDRATE   38400
#define UART_DATA_BITS  8
#define UART_STOP_BITS  1
#define UART_PARITY     0       /* None */

/* --- Onboard LED --- */
#define PIN_LED         25

#endif /* BOARD_PINS_H */
