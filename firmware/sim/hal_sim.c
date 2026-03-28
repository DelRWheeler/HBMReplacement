#define _GNU_SOURCE
#include "../src/hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <termios.h>
#include <pty.h>
#include <pthread.h>

/*
 * Linux Simulator HAL
 *
 * Serial: PTY pair — firmware side uses the master, test harness connects
 *         to the slave (whose path is printed at startup).
 * ADC:    Synthetic load cell data with configurable noise and simulated
 *         bird-passing waveforms.
 * Config: File-backed (sim_config.bin in current directory).
 * Cores:  pthreads.
 */

/* --- PTY serial --- */
static int pty_master_fd = -1;
static int pty_slave_fd = -1;
static char pty_slave_name[256];

/* --- ADC simulation --- */
static uint16_t sim_osr = 1024;
static double sim_base_weight = 500000.0;  /* ~base ADC counts (empty shackle) */
static double sim_bird_weight = 200000.0;  /* additional counts when bird present */
static double sim_noise_amplitude = 50.0;
static uint64_t sim_start_time_us;

/* Bird simulation: 220 birds/min = 3.67 Hz, 6" centers, 4.75" deck */
static double sim_bird_freq = 220.0 / 60.0;    /* Hz */
static double sim_duty_cycle = 4.75 / 6.0;     /* fraction of period on deck */

/* --- Config file --- */
static const char *CONFIG_FILE = "sim_config.bin";

/* --- Core 1 thread --- */
static pthread_t core1_thread;

/* ================================================================ */
/* Serial (PTY)                                                      */
/* ================================================================ */

int hal_serial_init(uint32_t baudrate)
{
    (void)baudrate;

    if (openpty(&pty_master_fd, &pty_slave_fd, pty_slave_name, NULL, NULL) < 0) {
        perror("openpty");
        return -1;
    }

    /* Set master to non-blocking */
    int flags = fcntl(pty_master_fd, F_GETFL, 0);
    fcntl(pty_master_fd, F_SETFL, flags | O_NONBLOCK);

    /* Raw mode on slave */
    struct termios tty;
    tcgetattr(pty_slave_fd, &tty);
    cfmakeraw(&tty);
    cfsetospeed(&tty, B38400);
    cfsetispeed(&tty, B38400);
    tcsetattr(pty_slave_fd, TCSANOW, &tty);

    /* Close our copy of the slave fd — the test harness will open it
     * independently via the path. Holding it open here causes I/O
     * errors when both sides try to use it. */
    close(pty_slave_fd);
    pty_slave_fd = -1;

    printf("==============================================\n");
    printf("  FIT7 Simulator Serial Port: %s\n", pty_slave_name);
    printf("  Connect test harness to this PTY path.\n");
    printf("==============================================\n");

    return 0;
}

int hal_serial_write(const uint8_t *data, size_t len)
{
    if (pty_master_fd < 0) return -1;
    ssize_t n = write(pty_master_fd, data, len);
    return (n >= 0) ? (int)n : -1;
}

int hal_serial_read(uint8_t *data, size_t max_len)
{
    if (pty_master_fd < 0) return -1;
    ssize_t n = read(pty_master_fd, data, max_len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 0;
    return (n >= 0) ? (int)n : -1;
}

int hal_serial_available(void)
{
    /* Quick check if data is available */
    uint8_t tmp;
    ssize_t n = read(pty_master_fd, &tmp, 0);
    (void)n;
    /* For PTY, just return 1 to indicate "try reading" */
    return 1;
}

void hal_serial_flush(void)
{
    if (pty_master_fd >= 0) {
        tcflush(pty_master_fd, TCIFLUSH);
    }
}

void hal_serial_cleanup(void)
{
    if (pty_master_fd >= 0) close(pty_master_fd);
    if (pty_slave_fd >= 0) close(pty_slave_fd);
    pty_master_fd = -1;
    pty_slave_fd = -1;
}

/* ================================================================ */
/* ADC Simulation                                                    */
/* ================================================================ */

int hal_adc_init(void)
{
    srand(time(NULL));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sim_start_time_us = (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    printf("[sim-adc] Initialized. Base weight: %.0f counts, Bird: %.0f counts\n",
           sim_base_weight, sim_bird_weight);
    return 0;
}

bool hal_adc_data_ready(void)
{
    /* Simulate data rate based on OSR.
     * MCP3561 at ~3.3MHz AMCLK: rate = 3300000 / (4 * OSR) */
    static uint64_t last_sample_us = 0;
    uint64_t now = hal_time_us();

    double rate = 3300000.0 / (4.0 * sim_osr);
    uint64_t interval_us = (uint64_t)(1000000.0 / rate);

    if (now - last_sample_us >= interval_us) {
        last_sample_us = now;
        return true;
    }
    return false;
}

int32_t hal_adc_read(void)
{
    /* Simulate load cell reading with optional bird-passing waveform */
    uint64_t now = hal_time_us();
    double elapsed = (now - sim_start_time_us) / 1000000.0;

    /* Bird cycle */
    double period = 1.0 / sim_bird_freq;
    double phase = fmod(elapsed, period) / period;

    double weight = sim_base_weight;
    if (phase < sim_duty_cycle) {
        /* Bird is on the deck — simulate gradual settle */
        double settle_phase = phase / sim_duty_cycle;
        /* Quick rise, then settle */
        double settle = 1.0 - 0.3 * exp(-settle_phase * 8.0);
        weight += sim_bird_weight * settle;
    }

    /* Add noise */
    double noise = sim_noise_amplitude * ((double)rand() / RAND_MAX - 0.5) * 2.0;
    weight += noise;

    return (int32_t)weight;
}

void hal_adc_set_osr(uint16_t osr)
{
    sim_osr = osr;
    double rate = 3300000.0 / (4.0 * osr);
    printf("[sim-adc] OSR set to %u, effective rate: %.1f sps\n", osr, rate);
}

void hal_adc_cleanup(void)
{
    /* Nothing to clean up */
}

/* ================================================================ */
/* Configuration Storage (file-backed)                               */
/* ================================================================ */

int hal_config_read(uint8_t *buf, size_t len)
{
    FILE *f = fopen(CONFIG_FILE, "rb");
    if (!f) return -1;

    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

int hal_config_write(const uint8_t *buf, size_t len)
{
    FILE *f = fopen(CONFIG_FILE, "wb");
    if (!f) return -1;

    size_t n = fwrite(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

/* ================================================================ */
/* System                                                            */
/* ================================================================ */

void hal_init(void)
{
    printf("[sim] FIT7 Load Cell Simulator starting\n");
}

void hal_sleep_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

uint64_t hal_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

void hal_led_set(bool on)
{
    (void)on;
    /* No LED in simulator */
}

/* ================================================================ */
/* Multicore (pthreads)                                              */
/* ================================================================ */

static void *core1_wrapper(void *arg)
{
    void (*entry)(void) = (void (*)(void))arg;
    entry();
    return NULL;
}

void hal_launch_core1(void (*entry)(void))
{
    pthread_create(&core1_thread, NULL, core1_wrapper, (void *)entry);
    printf("[sim] Core 1 thread launched\n");
}

void hal_lockout_start(void)
{
    /* In simulator, flash writes don't need core lockout.
     * On real Pico, this would call multicore_lockout_start_blocking(). */
}

void hal_lockout_end(void)
{
    /* Matching end for lockout_start */
}
