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
#define PIN_SPI_SCK     18      /* SPI0 SCK (shared) */
#define PIN_SPI_MOSI    19      /* SPI0 TX  (shared) */
#define PIN_SPI_MISO    16      /* SPI0 RX  (shared) */
#define PIN_SPI_CS      17      /* SPI0 CS  (Socket 1 CS0) */
#define PIN_ADC_DRDY    3       /* MCP3561 Data Ready (Socket 1 INT0) */
#define PIN_ADC_RST     6       /* MCP3561 Reset (Socket 1 RST0) */
#define PIN_EXC_EN      2       /* Excitation voltage enable (Socket 1 PWM0, active LOW) */

#define SPI_PORT        spi0
#define SPI_BAUDRATE    5000000 /* 5 MHz SPI clock */

/* --- Socket 2: RS485 Isolator 3 Click (ADM2867E, full-duplex isolated) --- */
#define PIN_UART_TX     8       /* UART1 TX (Socket 2 TX1) */
#define PIN_UART_RX     9       /* UART1 RX (Socket 2 RX1) */
#define PIN_RS485_DE    7       /* Driver Enable (Socket 2 PWM1, active HIGH) */
#define PIN_RS485_RE    20      /* Receiver Enable (Socket 2 RST1, active LOW) */
#define PIN_RS485_INV   21      /* Receiver Invert (Socket 2 INT1, HIGH=invert) */

#define UART_PORT       uart1
#define UART_BAUDRATE   38400
#define UART_DATA_BITS  8
#define UART_STOP_BITS  1
#define UART_PARITY     0       /* None */

/* --- Onboard LED --- */
#define PIN_LED         25

#endif /* BOARD_PINS_H */
