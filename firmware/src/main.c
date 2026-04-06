#include "hal.h"
#include "ring_buffer.h"
#include "fit7_protocol.h"
#include "config_store.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>

/*
 * Main entry point — works on both Pico (real hardware) and Linux (simulator).
 *
 * Core 0: ADC sampling loop — reads MCP3561 and pushes to ring buffer
 * Core 1: Comms loop — RS485 command parsing and streaming output
 *
 * In simulator mode, these run as pthreads.
 */

static ring_buffer_t ring;
static fit7_ctx_t protocol;
static volatile bool running = true;

/* ASF to OSR mapping (approximate, matching FIT7 behavior) */
static uint16_t asf_to_osr(int asf)
{
    /* FIT7 ASF values 2-10 map to different filter cutoff frequencies.
     * Lower ASF = less filtering = higher output rate.
     * These OSR values give roughly equivalent output rates on the MCP3561. */
    switch (asf) {
        case 2:  return 128;   /* ~6400 sps */
        case 3:  return 256;   /* ~3200 sps */
        case 4:  return 512;   /* ~1600 sps */
        case 5:  return 768;   /* ~1075 sps */
        case 6:  return 1024;  /* ~800 sps  */
        case 7:  return 2048;  /* ~400 sps  */
        case 8:  return 4096;  /* ~200 sps  */
        case 9:  return 8192;  /* ~100 sps  */
        case 10: return 16384; /* ~50 sps   */
        default: return 1024;
    }
}

/* Callback from protocol engine when ASF changes */
static void on_filter_change(int asf)
{
    uint16_t osr = asf_to_osr(asf);
    hal_adc_set_osr(osr);
    printf("[main] Filter changed: ASF=%d -> OSR=%u\n", asf, osr);
}

/* --- Core 0: ADC Sampling Loop --- */
static void core0_adc_loop(void)
{
    /* Set initial OSR from config */
    int asf = config_get(PARAM_ASF);
    hal_adc_set_osr(asf_to_osr(asf));

    printf("[core0] ADC sampling loop started\n");

    while (running) {
        if (hal_adc_data_ready()) {
            int32_t sample = hal_adc_read();
            if (!ring_push(&ring, sample)) {
                /* Ring full — drop oldest sample and push */
                int32_t discard;
                ring_pop(&ring, &discard);
                ring_push(&ring, sample);
            }
        }
        /* Short sleep to yield CPU for USB CDC background task.
         * In sim, hal_adc_data_ready() rate-limits us. */
        hal_sleep_ms(1);
    }
}

/* --- Core 1: Communications Loop --- */
static void core1_comms_loop(void)
{
    uint8_t rx_buf[256];
    uint8_t tx_buf[1024];

    printf("[core1] Communications loop started\n");

    while (running) {
        /* Read incoming RS485 data */
        int rx_len = hal_serial_read(rx_buf, sizeof(rx_buf));
        if (rx_len > 0) {
            printf("[rx] %d bytes:", rx_len);
            for (int i = 0; i < rx_len; i++) printf(" %02X", rx_buf[i]);
            printf("\n");
            fit7_feed(&protocol, rx_buf, rx_len);
        }

        /* Send any pending command responses */
        int rsp_len = fit7_get_response(&protocol, tx_buf, sizeof(tx_buf));
        if (rsp_len > 0) {
            printf("[tx] %d bytes:", rsp_len);
            for (int i = 0; i < rsp_len; i++) printf(" %02X", tx_buf[i]);
            printf("\n");
            hal_serial_write(tx_buf, rsp_len);
        }

        /* If streaming, generate and send measurement data */
        if (fit7_is_streaming(&protocol)) {
            int stream_len = fit7_stream_output(&protocol, tx_buf, sizeof(tx_buf));
            if (stream_len > 0) {
                hal_serial_write(tx_buf, stream_len);
            }
            hal_sleep_ms(1); /* ~1ms between stream bursts */
        } else {
            hal_sleep_ms(5); /* Idle polling interval */
        }
    }
}

/* Signal handler for clean shutdown */
static void sigint_handler(int sig)
{
    (void)sig;
    printf("\n[main] Shutting down...\n");
    running = false;
}

int main(void)
{
    signal(SIGINT, sigint_handler);

    /* Initialize HAL */
    hal_init();
    printf("[main] HAL initialized\n");

    /* Initialize config store */
    config_init();
    printf("[main] Config initialized\n");

    /* Initialize ring buffer */
    ring_init(&ring);

    /* Initialize ADC */
    printf("[main] Initializing ADC...\n");
    hal_adc_init();
    printf("[main] ADC initialized\n");

    /* Initialize serial port (RS485) */
    printf("[main] Initializing serial...\n");
    if (hal_serial_init(38400) != 0) {
        printf("[main] Failed to initialize serial port\n");
        return 1;
    }
    printf("[main] Serial initialized\n");

    /* Initialize protocol engine */
    fit7_init(&protocol, &ring);
    fit7_set_filter_callback(&protocol, on_filter_change);
    printf("[main] Protocol initialized\n");

    /* Diagnostic: send a known pattern on RS485 TX */
    printf("[main] Sending test pattern on UART TX...\n");
    uint8_t test_msg[] = "HELLO\r\n";
    hal_serial_write(test_msg, 7);
    printf("[main] Test pattern sent\n");

    /* Check for any data already in UART RX */
    hal_sleep_ms(100);
    uint8_t rxtest[64];
    int rxn = hal_serial_read(rxtest, sizeof(rxtest));
    printf("[main] UART RX buffer: %d bytes", rxn);
    if (rxn > 0) {
        printf(":");
        for (int i = 0; i < rxn; i++) printf(" %02X", rxtest[i]);
    }
    printf("\n");

    printf("[main] Launching core1\n");

    /* Launch Core 1 (comms) on a separate thread/core */
    hal_launch_core1(core1_comms_loop);

    /* Core 0 runs the ADC loop on the main thread */
    core0_adc_loop();

    /* Cleanup */
    hal_serial_cleanup();
    hal_adc_cleanup();

    printf("[main] Shutdown complete\n");
    return 0;
}
