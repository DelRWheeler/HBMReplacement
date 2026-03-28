#ifndef FIT7_PROTOCOL_H
#define FIT7_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"

/*
 * FIT7 Protocol Engine
 *
 * Parses incoming RS485 commands and generates responses.
 * Protocol details from PanelX User Manual and HBMLoadCell.cpp.
 *
 * Command format: CMD[params];  (terminated by semicolon or LF)
 * Response format:
 *   - Set command success: "0\r\n"
 *   - Query response: "value\r\n" (comma-separated for multi-value)
 *   - Error: "?\r\n"
 *   - No response for: RES, STP, S00-S99
 */

/* Maximum command length */
#define FIT7_CMD_MAX_LEN    128
#define FIT7_RSP_MAX_LEN    256

/* Device identification string */
#define FIT7_IDN_STRING     "DCH Load Cell,FIT7-EMU,0001,1.0.0"

/* Password */
#define FIT7_PASSWORD        "AED"

/* Protocol state */
typedef enum {
    FIT7_STATE_IDLE,
    FIT7_STATE_STREAMING
} fit7_state_t;

/* COF output format (from HBMLoadCell.h) */
typedef struct {
    int mode;           /* 0=Binary, 1=ASCII */
    int bytes;          /* 2 or 4 for binary */
    int byte_order;     /* 0=MSB->LSB, 1=LSB->MSB */
    int status;         /* 0=None, 1=LSB, 2=PRM3 */
    int eoc;            /* 0=No CRLF, 1=CRLF */
    int addr;           /* 0=None, 1=PRM2 */
} fit7_output_format_t;

/* Protocol context */
typedef struct {
    fit7_state_t        state;
    fit7_output_format_t output_format;
    bool                password_unlocked;
    bool                checksum_enabled;
    int                 device_address;

    /* Command accumulation buffer */
    char                cmd_buf[FIT7_CMD_MAX_LEN];
    int                 cmd_len;

    /* Response buffer */
    char                rsp_buf[FIT7_RSP_MAX_LEN];
    int                 rsp_len;

    /* Pointer to ring buffer for streaming */
    ring_buffer_t       *ring;

    /* Callback to signal ADC filter change */
    void                (*on_filter_change)(int asf);
} fit7_ctx_t;

/* Initialize protocol engine */
void    fit7_init(fit7_ctx_t *ctx, ring_buffer_t *ring);

/* Feed incoming bytes from RS485. Returns number of bytes consumed. */
int     fit7_feed(fit7_ctx_t *ctx, const uint8_t *data, int len);

/* Get pending response data to transmit. Returns bytes to send. */
int     fit7_get_response(fit7_ctx_t *ctx, uint8_t *buf, int max_len);

/* Generate streaming output from ring buffer. Call this repeatedly when streaming.
 * Returns bytes written to buf. */
int     fit7_stream_output(fit7_ctx_t *ctx, uint8_t *buf, int max_len);

/* Check if currently streaming */
bool    fit7_is_streaming(const fit7_ctx_t *ctx);

/* Set the filter-change callback */
void    fit7_set_filter_callback(fit7_ctx_t *ctx, void (*cb)(int asf));

#endif /* FIT7_PROTOCOL_H */
