#include "fit7_protocol.h"
#include "config_store.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/*
 * FIT7 Protocol Engine
 *
 * Command format from PanelX manual Section 4.4.4:
 *   - Commands: CMD[space]params;  (semicolon or LF as delimiter)
 *   - Queries: CMD?;
 *   - Responses: "0\r\n" (success), "value\r\n" (query), "?\r\n" (error)
 *   - No response for: RES, STP, S00-S99
 *
 * Streaming output format with COF 8:
 *   Binary, 4 bytes, MSB→LSB, status byte in LSB, with CR+LF
 *   Total per reading: 4 data bytes + 1 status byte + optional checksum + CR + LF
 *
 * When CSM=1 (checksum enabled), a 2-byte checksum is appended before CRLF.
 * Checksum = XOR of all preceding bytes in the message, sent as 2 hex ASCII chars.
 */

/* Forward declarations */
static void process_command(fit7_ctx_t *ctx, const char *cmd);
static void respond(fit7_ctx_t *ctx, const char *fmt, ...);
static void respond_ok(fit7_ctx_t *ctx);
static void respond_error(fit7_ctx_t *ctx);
static void decode_cof(int cof, fit7_output_format_t *fmt);

/* Helper: uppercase a string in-place */
static void str_toupper(char *s)
{
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

/* Helper: trim leading whitespace */
static const char *skip_space(const char *s)
{
    while (*s && *s <= ' ') s++;
    return s;
}

void fit7_init(fit7_ctx_t *ctx, ring_buffer_t *ring)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = FIT7_STATE_IDLE;
    ctx->ring = ring;
    ctx->password_unlocked = false;
    ctx->checksum_enabled = (config_get(PARAM_CSM) == 1);
    ctx->device_address = config_get(PARAM_ADR);

    /* Decode initial output format */
    decode_cof(config_get(PARAM_COF), &ctx->output_format);
}

int fit7_feed(fit7_ctx_t *ctx, const uint8_t *data, int len)
{
    int consumed = 0;

    for (int i = 0; i < len; i++) {
        uint8_t c = data[i];
        consumed++;

        /* Delimiter: semicolon (0x3B) or LF (0x0A) */
        if (c == ';' || c == '\n') {
            if (ctx->cmd_len > 0) {
                ctx->cmd_buf[ctx->cmd_len] = '\0';
                process_command(ctx, ctx->cmd_buf);
                ctx->cmd_len = 0;
            }
            continue;
        }

        /* CR is ignored */
        if (c == '\r') continue;

        /* Ctrl-Q (0x11) and Ctrl-S (0x13) are reserved — skip */
        if (c == 0x11 || c == 0x13) continue;

        /* Accumulate */
        if (ctx->cmd_len < FIT7_CMD_MAX_LEN - 1) {
            ctx->cmd_buf[ctx->cmd_len++] = (char)c;
        }
    }

    return consumed;
}

int fit7_get_response(fit7_ctx_t *ctx, uint8_t *buf, int max_len)
{
    int len = ctx->rsp_len;
    if (len == 0) return 0;
    if (len > max_len) len = max_len;

    memcpy(buf, ctx->rsp_buf, len);

    /* Shift remaining */
    if (len < ctx->rsp_len) {
        memmove(ctx->rsp_buf, ctx->rsp_buf + len, ctx->rsp_len - len);
    }
    ctx->rsp_len -= len;
    return len;
}

bool fit7_is_streaming(const fit7_ctx_t *ctx)
{
    return ctx->state == FIT7_STATE_STREAMING;
}

void fit7_set_filter_callback(fit7_ctx_t *ctx, void (*cb)(int asf))
{
    ctx->on_filter_change = cb;
}

/*
 * Generate streaming binary output from ring buffer.
 *
 * COF 8 format: 4-byte signed value (MSB first) + 1 status byte + CRLF
 * With CSM=1: 4-byte value + 1 status + 2-char hex checksum + CRLF
 */
int fit7_stream_output(fit7_ctx_t *ctx, uint8_t *buf, int max_len)
{
    if (ctx->state != FIT7_STATE_STREAMING) return 0;

    int pos = 0;
    int32_t sample;

    /* Calculate bytes per frame */
    int frame_size;
    if (ctx->output_format.mode == 0) {
        /* Binary mode */
        frame_size = ctx->output_format.bytes;                  /* 4 data bytes */
        if (ctx->output_format.status) frame_size += 1;         /* +1 status */
        if (ctx->checksum_enabled) frame_size += 2;             /* +2 checksum */
        if (ctx->output_format.eoc) frame_size += 2;            /* +2 CRLF */
    } else {
        /* ASCII mode — estimate max */
        frame_size = 20;
    }

    while (pos + frame_size <= max_len && ring_pop(ctx->ring, &sample)) {
        if (ctx->output_format.mode == 0) {
            /* Binary output */
            uint8_t xor_sum = 0;
            int start = pos;

            if (ctx->output_format.bytes == 4) {
                if (ctx->output_format.byte_order == 0) {
                    /* MSB first */
                    buf[pos++] = (sample >> 24) & 0xFF;
                    buf[pos++] = (sample >> 16) & 0xFF;
                    buf[pos++] = (sample >> 8) & 0xFF;
                    buf[pos++] = sample & 0xFF;
                } else {
                    /* LSB first */
                    buf[pos++] = sample & 0xFF;
                    buf[pos++] = (sample >> 8) & 0xFF;
                    buf[pos++] = (sample >> 16) & 0xFF;
                    buf[pos++] = (sample >> 24) & 0xFF;
                }
            } else {
                /* 2-byte mode */
                int16_t s16 = (int16_t)(sample >> 16);
                if (ctx->output_format.byte_order == 0) {
                    buf[pos++] = (s16 >> 8) & 0xFF;
                    buf[pos++] = s16 & 0xFF;
                } else {
                    buf[pos++] = s16 & 0xFF;
                    buf[pos++] = (s16 >> 8) & 0xFF;
                }
            }

            /* Status byte: bit 0 = valid measurement */
            if (ctx->output_format.status) {
                buf[pos++] = 0x01; /* Status: valid, no error */
            }

            /* Checksum: XOR of all bytes so far in this frame */
            if (ctx->checksum_enabled) {
                for (int j = start; j < pos; j++) {
                    xor_sum ^= buf[j];
                }
                /* Encode as 2 hex ASCII chars */
                static const char hex[] = "0123456789ABCDEF";
                buf[pos++] = hex[(xor_sum >> 4) & 0x0F];
                buf[pos++] = hex[xor_sum & 0x0F];
            }

            /* EOC: CRLF */
            if (ctx->output_format.eoc) {
                buf[pos++] = '\r';
                buf[pos++] = '\n';
            }
        } else {
            /* ASCII output */
            char tmp[32];
            int n;
            if (ctx->output_format.addr) {
                n = snprintf(tmp, sizeof(tmp), "%d;%+08d",
                             ctx->device_address, sample);
            } else {
                n = snprintf(tmp, sizeof(tmp), "%+08d", sample);
            }
            if (ctx->output_format.status) {
                n += snprintf(tmp + n, sizeof(tmp) - n, ";001");
            }
            if (ctx->output_format.eoc) {
                n += snprintf(tmp + n, sizeof(tmp) - n, "\r\n");
            }
            if (pos + n > max_len) break;
            memcpy(buf + pos, tmp, n);
            pos += n;
        }
    }

    return pos;
}

/* --- Response helpers --- */

#include <stdarg.h>

static void respond(fit7_ctx_t *ctx, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(ctx->rsp_buf + ctx->rsp_len,
                      FIT7_RSP_MAX_LEN - ctx->rsp_len, fmt, args);
    va_end(args);
    if (n > 0) ctx->rsp_len += n;
}

static void respond_ok(fit7_ctx_t *ctx)
{
    respond(ctx, "0\r\n");
}

static void respond_error(fit7_ctx_t *ctx)
{
    respond(ctx, "?\r\n");
}

/* --- COF decoder (from HBMLoadCell.cpp Meas_Mode_From_COF) --- */

static void decode_cof(int cof, fit7_output_format_t *fmt)
{
    /* Binary COF values */
    static const int bin_vals[] = {0,2,4,6,8,12,16,18,20,22,24,28,32,34,36,38,40,44};
    static const int two_byte[] = {2,6,18,24,34,38};
    static const int msb_lsb[] = {0,2,8,16,18,24,32,34,40};
    static const int bin_status[] = {8,12,24,28,40,44};
    static const int asc_status[] = {9,11,25,27,41,43};
    static const int addr_vals[] = {1,5,9,17,21,25,33,37,41};

    #define IN_ARRAY(v, arr) _in_array(v, arr, sizeof(arr)/sizeof(arr[0]))
    int _in_array(int v, const int *arr, int n) {
        for (int i = 0; i < n; i++) if (arr[i] == v) return 1;
        return 0;
    }

    if (IN_ARRAY(cof, bin_vals)) {
        fmt->mode = 0; /* Binary */
        fmt->addr = 0;
        fmt->bytes = IN_ARRAY(cof, two_byte) ? 2 : 4;
        fmt->byte_order = IN_ARRAY(cof, msb_lsb) ? 0 : 1;
        fmt->status = IN_ARRAY(cof, bin_status) ? 1 : 0;
    } else {
        fmt->mode = 1; /* ASCII */
        fmt->bytes = 0;
        fmt->addr = IN_ARRAY(cof, addr_vals) ? 1 : 0;
        fmt->status = IN_ARRAY(cof, asc_status) ? 2 : 0;
    }

    fmt->eoc = (cof >= 0 && cof <= 12) ? 1 : 0;

    #undef IN_ARRAY
}

/* --- Command Processing --- */

static void handle_query(fit7_ctx_t *ctx, const char *param_name);
static void handle_set(fit7_ctx_t *ctx, const char *param_name, const char *value_str);

static void process_command(fit7_ctx_t *ctx, const char *cmd)
{
    char buf[FIT7_CMD_MAX_LEN];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Uppercase the command portion for case-insensitive matching */
    str_toupper(buf);

    const char *p = skip_space(buf);

    /* --- S-commands (Select node) --- */
    if (p[0] == 'S' && isdigit((unsigned char)p[1])) {
        /* S00-S99: Select node by address. No response. */
        int addr = atoi(p + 1);
        /* Accept if address matches or S98 (broadcast) */
        if (addr == ctx->device_address || addr == 98) {
            /* Selected — accept subsequent commands */
        }
        return; /* No response */
    }

    /* --- IDN? --- */
    if (strncmp(p, "IDN?", 4) == 0) {
        respond(ctx, "%s\r\n", FIT7_IDN_STRING);
        return;
    }

    /* --- STP (Stop) --- */
    if (strncmp(p, "STP", 3) == 0) {
        ctx->state = FIT7_STATE_IDLE;
        /* No response per spec */
        return;
    }

    /* --- RES (Reset) --- */
    if (strncmp(p, "RES", 3) == 0) {
        ctx->state = FIT7_STATE_IDLE;
        ctx->password_unlocked = false;
        /* No response per spec */
        return;
    }

    /* --- MSV? (Measured Signal Value) --- */
    if (strncmp(p, "MSV?", 4) == 0) {
        int mode = 0;
        if (p[4]) mode = atoi(p + 4);

        if (mode == 0) {
            /* Continuous output mode */
            ctx->state = FIT7_STATE_STREAMING;
            /* No immediate text response — data stream begins */
        } else {
            /* Single measurement — return last available sample */
            int32_t sample = 0;
            ring_pop(ctx->ring, &sample);
            respond(ctx, "%+08d\r\n", sample);
        }
        return;
    }

    /* --- SPW (Set Password) --- */
    if (strncmp(p, "SPW", 3) == 0) {
        /* SPW"AED" — extract password from quotes */
        const char *orig = cmd; /* Use original (not uppercased) for password */
        const char *q1 = strchr(orig, '"');
        if (q1) {
            const char *q2 = strchr(q1 + 1, '"');
            if (q2) {
                char pw[32];
                int pwlen = q2 - q1 - 1;
                if (pwlen > 0 && pwlen < (int)sizeof(pw)) {
                    strncpy(pw, q1 + 1, pwlen);
                    pw[pwlen] = '\0';
                    if (strcmp(pw, FIT7_PASSWORD) == 0) {
                        ctx->password_unlocked = true;
                        respond_ok(ctx);
                        return;
                    }
                }
            }
        }
        respond_error(ctx);
        return;
    }

    /* --- TDD (Store Parameters) --- */
    if (strncmp(p, "TDD", 3) == 0) {
        if (config_save()) {
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- COF (Configure Output Format) --- */
    if (strncmp(p, "COF", 3) == 0) {
        if (p[3] == '?') {
            respond(ctx, "%+08d\r\n", config_get(PARAM_COF));
            return;
        }
        /* Set: "COF 8" */
        const char *val = skip_space(p + 3);
        int cof = atoi(val);
        if (config_set(PARAM_COF, cof)) {
            decode_cof(cof, &ctx->output_format);
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- CSM (Checksum) --- */
    if (strncmp(p, "CSM", 3) == 0) {
        if (p[3] == '?') {
            respond(ctx, "%+08d\r\n", config_get(PARAM_CSM));
            return;
        }
        const char *val = skip_space(p + 3);
        int v = atoi(val);
        if (config_set(PARAM_CSM, v)) {
            ctx->checksum_enabled = (v == 1);
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- STR (Termination Resistor) --- */
    if (strncmp(p, "STR", 3) == 0) {
        if (p[3] == '?') {
            respond(ctx, "%+08d\r\n", config_get(PARAM_STR));
            return;
        }
        const char *val = skip_space(p + 3);
        int v = atoi(val);
        if (config_set(PARAM_STR, v)) {
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- BDR? (Baud Rate query) --- */
    if (strncmp(p, "BDR?", 4) == 0) {
        /* Respond with current baud rate and parity (0=none) */
        respond(ctx, "38400,0\r\n");
        return;
    }
    if (strncmp(p, "BDR", 3) == 0) {
        /* We don't support changing baud rate at runtime */
        respond_ok(ctx);
        return;
    }

    /* --- ADR (Device Address) --- */
    if (strncmp(p, "ADR", 3) == 0) {
        if (p[3] == '?') {
            respond(ctx, "%+08d\r\n", config_get(PARAM_ADR));
            return;
        }
        /* ADR addr,"serial" — parse address */
        const char *val = skip_space(p + 3);
        int addr = atoi(val);
        if (config_set(PARAM_ADR, addr)) {
            ctx->device_address = addr;
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- LIC? (Linearization Coefficients — multi-value query) --- */
    if (strncmp(p, "LIC?", 4) == 0) {
        respond(ctx, "%+08d,%+08d,%+08d,%+08d\r\n",
                config_get(PARAM_LIC0), config_get(PARAM_LIC1),
                config_get(PARAM_LIC2), config_get(PARAM_LIC3));
        return;
    }

    /* --- LIC0-3 set commands --- */
    if (strncmp(p, "LIC", 3) == 0 && (p[3] >= '0' && p[3] <= '3')) {
        int idx = p[3] - '0';
        param_id_t pid = PARAM_LIC0 + idx;
        /* "LIC0, value" or "LIC0 value" */
        const char *val = p + 4;
        if (*val == ',') val++;
        val = skip_space(val);
        if (!ctx->password_unlocked && config_get_def(pid)->password) {
            respond_error(ctx);
            return;
        }
        int32_t v = atoi(val);
        if (config_set(pid, v)) {
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- TRC (Trigger Command — multi-value) --- */
    if (strncmp(p, "TRC?", 4) == 0) {
        respond(ctx, "%.1d,%.1d, %07d,%02d,%02d\r\n",
                config_get(PARAM_TRC1), config_get(PARAM_TRC2),
                config_get(PARAM_TRC3), config_get(PARAM_TRC4),
                config_get(PARAM_TRC5));
        return;
    }
    if (strncmp(p, "TRC", 3) == 0 && p[3] != '?') {
        /* TRC p1,p2,p3,p4,p5 */
        const char *val = skip_space(p + 3);
        char tmp[64];
        strncpy(tmp, val, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char *tok = strtok(tmp, ",");
        int vals[5] = {0};
        for (int i = 0; i < 5 && tok; i++) {
            vals[i] = atoi(tok);
            tok = strtok(NULL, ",");
        }
        config_set(PARAM_TRC1, vals[0]);
        config_set(PARAM_TRC2, vals[1]);
        config_set(PARAM_TRC3, vals[2]);
        config_set(PARAM_TRC4, vals[3]);
        config_set(PARAM_TRC5, vals[4]);
        respond_ok(ctx);
        return;
    }

    /* --- CWT? (Calibration Weight — multi-value query) --- */
    if (strncmp(p, "CWT?", 4) == 0) {
        int32_t cwt = config_get(PARAM_CWT);
        respond(ctx, "%+08d, %+08d\r\n", cwt, cwt);
        return;
    }

    /* --- ASF (special: also triggers filter change callback) --- */
    if (strncmp(p, "ASF", 3) == 0 && p[3] != '?') {
        const char *val = skip_space(p + 3);
        int v = atoi(val);
        if (v < 2) {
            respond_error(ctx);
            return;
        }
        if (config_set(PARAM_ASF, v)) {
            if (ctx->on_filter_change) {
                ctx->on_filter_change(v);
            }
            respond_ok(ctx);
        } else {
            respond_error(ctx);
        }
        return;
    }

    /* --- Generic parameter query/set --- */
    /* Extract command name (letters only) and check for ? */
    char param_name[8];
    int ni = 0;
    while (ni < 7 && isalpha((unsigned char)p[ni])) {
        param_name[ni] = p[ni];
        ni++;
    }
    param_name[ni] = '\0';

    if (p[ni] == '?') {
        handle_query(ctx, param_name);
    } else {
        const char *val = skip_space(p + ni);
        handle_set(ctx, param_name, val);
    }
}

static void handle_query(fit7_ctx_t *ctx, const char *param_name)
{
    int id = config_find_by_name(param_name);
    if (id < 0) {
        respond_error(ctx);
        return;
    }
    respond(ctx, "%+08d\r\n", config_get((param_id_t)id));
}

static void handle_set(fit7_ctx_t *ctx, const char *param_name, const char *value_str)
{
    int id = config_find_by_name(param_name);
    if (id < 0) {
        respond_error(ctx);
        return;
    }

    const param_def_t *def = config_get_def((param_id_t)id);
    if (def->password && !ctx->password_unlocked) {
        respond_error(ctx);
        return;
    }

    int32_t value = atoi(value_str);
    if (config_set((param_id_t)id, value)) {
        respond_ok(ctx);
    } else {
        respond_error(ctx);
    }
}
