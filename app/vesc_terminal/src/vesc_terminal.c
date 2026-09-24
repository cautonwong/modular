#include "vesc_terminal/vesc_terminal.h"
#include <string.h>

static edge_status_t vesc_terminal_poll(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

static edge_status_t vesc_terminal_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t vesc_terminal_power_off(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

void vesc_terminal_construct(vesc_terminal_app_t *app, uint32_t module_id, uint32_t priority,
                             const terminal_stream_port_t *stream_port,
                             const terminal_system_port_t *sys_port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 100u,
        .poll = vesc_terminal_poll,
        .on_event = vesc_terminal_on_event,
        .power_off = vesc_terminal_power_off,
        .private_data = app,
    };

    if (stream_port) {
        app->stream_port = *stream_port;
    }
    if (sys_port) {
        app->sys_port = *sys_port;
    }
}

edge_status_t vesc_terminal_init(vesc_terminal_app_t *app) {
    if (!app || !app->stream_port.write_string) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static void append_str(char *dst, size_t max_len, size_t *pos, const char *src) {
    if (!dst || !pos || !src) {
        return;
    }
    while (*src && *pos + 1 < max_len) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = '\0';
}

static void append_uint(char *dst, size_t max_len, size_t *pos, uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0 && i < 11) {
            buf[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (i > 0 && *pos + 1 < max_len) {
        dst[(*pos)++] = buf[--i];
    }
    dst[*pos] = '\0';
}

static void append_hex32(char *dst, size_t max_len, size_t *pos, uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    append_str(dst, max_len, pos, "0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (uint8_t)((val >> (i * 4)) & 0x0F);
        if (*pos + 1 < max_len) {
            dst[(*pos)++] = hex_chars[nibble];
        }
    }
    dst[*pos] = '\0';
}

static void append_float(char *dst, size_t max_len, size_t *pos, float val, int decimals) {
    if (val < 0.0f) {
        append_str(dst, max_len, pos, "-");
        val = -val;
    }
    uint32_t integer_part = (uint32_t)val;
    append_uint(dst, max_len, pos, integer_part);
    if (decimals > 0) {
        append_str(dst, max_len, pos, ".");
        float frac = val - (float)integer_part;
        for (int i = 0; i < decimals; i++) {
            frac *= 10.0f;
            uint32_t digit = (uint32_t)frac;
            if (digit > 9) {
                digit = 9;
            }
            if (*pos + 1 < max_len) {
                dst[(*pos)++] = (char)('0' + digit);
            }
            frac -= (float)digit;
        }
        dst[*pos] = '\0';
    }
}

edge_status_t vesc_terminal_execute(vesc_terminal_app_t *app, const char *cmd) {
    if (!app || !cmd || !app->stream_port.write_string) {
        return EDGE_EINVAL;
    }

    char out_buf[128];
    size_t pos = 0;
    out_buf[0] = '\0';

    if (strcmp(cmd, "ping") == 0) {
        return app->stream_port.write_string(app->stream_port.self, "pong\r\n");
    }

    if (strcmp(cmd, "help") == 0) {
        return app->stream_port.write_string(app->stream_port.self,
                                             "Available commands: ping, help, faults, stats\r\n");
    }

    if (strcmp(cmd, "stats") == 0) {
        if (!app->sys_port.get_stats) {
            return app->stream_port.write_string(app->stream_port.self, "stats unavailable\r\n");
        }
        float rpm = 0.0f, iq = 0.0f, v_bus = 0.0f, temp = 0.0f;
        uint32_t faults = 0;
        app->sys_port.get_stats(app->sys_port.self, &rpm, &iq, &v_bus, &temp, &faults);

        append_str(out_buf, sizeof(out_buf), &pos, "RPM: ");
        append_float(out_buf, sizeof(out_buf), &pos, rpm, 1);
        append_str(out_buf, sizeof(out_buf), &pos, ", Current: ");
        append_float(out_buf, sizeof(out_buf), &pos, iq, 2);
        append_str(out_buf, sizeof(out_buf), &pos, " A, Vbus: ");
        append_float(out_buf, sizeof(out_buf), &pos, v_bus, 1);
        append_str(out_buf, sizeof(out_buf), &pos, " V, Temp: ");
        append_float(out_buf, sizeof(out_buf), &pos, temp, 1);
        append_str(out_buf, sizeof(out_buf), &pos, " C, Faults: ");
        append_uint(out_buf, sizeof(out_buf), &pos, faults);
        append_str(out_buf, sizeof(out_buf), &pos, "\r\n");
        return app->stream_port.write_string(app->stream_port.self, out_buf);
    }

    if (strcmp(cmd, "faults") == 0) {
        if (!app->sys_port.get_stats) {
            return app->stream_port.write_string(app->stream_port.self, "faults unavailable\r\n");
        }
        float rpm = 0.0f, iq = 0.0f, v_bus = 0.0f, temp = 0.0f;
        uint32_t faults = 0;
        app->sys_port.get_stats(app->sys_port.self, &rpm, &iq, &v_bus, &temp, &faults);

        if (faults == 0) {
            return app->stream_port.write_string(app->stream_port.self, "No faults\r\n");
        }
        append_str(out_buf, sizeof(out_buf), &pos, "Fault code: ");
        append_hex32(out_buf, sizeof(out_buf), &pos, faults);
        append_str(out_buf, sizeof(out_buf), &pos, "\r\n");
        return app->stream_port.write_string(app->stream_port.self, out_buf);
    }

    append_str(out_buf, sizeof(out_buf), &pos, "Unknown command: ");
    append_str(out_buf, sizeof(out_buf), &pos, cmd);
    append_str(out_buf, sizeof(out_buf), &pos, "\r\n");
    return app->stream_port.write_string(app->stream_port.self, out_buf);
}

edge_module_t *vesc_terminal_module(vesc_terminal_app_t *app) {
    return app ? &app->module : NULL;
}
