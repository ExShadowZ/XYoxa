#include "cmd_router.h"
#include "main.h"
#include "usb_cdc.h"
#include "esp_commands.h"
#include "bridge.h"
#include <string.h>
#include <stdio.h>

/* Сравнение: magic_str + ',' в начале line */
static int has_magic(const uint8_t *line, uint16_t len)
{
    if (len < CMD_MAGIC_LEN + 1) return 0;

    if (memcmp(line, CMD_MAGIC_STR, CMD_MAGIC_LEN) != 0) return 0;
    if (line[CMD_MAGIC_LEN] != ',') return 0;

    return 1;
}

/* Отправить <магия>,OK\n или <магия>,ERROR\n в CDC */
static void send_reply(int ok)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%s,%s\n",
                     CMD_MAGIC_STR,
                     ok ? "OK" : "ERROR");
    if (n > 0) {
        usb_cdc_send((const uint8_t *)buf, (uint16_t)n);
    }
}

cmd_route_t cmd_router_handle_line(const uint8_t *line, uint16_t len)
{
    if (!has_magic(line, len)) return CMD_ROUTE_DATA;

    /* Отрезаем "магия," — остаётся CMD=... */
    const char *cmd = (const char *)(line + CMD_MAGIC_LEN + 1);
    uint16_t cmd_len = len - (CMD_MAGIC_LEN + 1);
    (void)cmd_len;

    /* CMD=... */
    if (strncmp(cmd, "CMD=", 4) != 0) {
        send_reply(0);
        return CMD_ROUTE_COMMAND;
    }

    const char *body = cmd + 4;

    if (strncmp(body, "CONNECT", 7) == 0) {
        send_reply(1);
        return CMD_ROUTE_COMMAND;
    }

    if (strncmp(body, "SET", 3) == 0) {
        const char *params = (body[3] == ',') ? body + 4 : "";

        esp_set_cmd_t parsed;
        if (esp_parse_set_cmd(params, &parsed) != 0) {
            send_reply(0);
            return CMD_ROUTE_COMMAND;
        }

        if (bridge_apply_settings(parsed.dev1, parsed.dev2,
                                  &parsed.raw, &parsed.net) != 0) {
            send_reply(0);
            return CMD_ROUTE_COMMAND;
        }

        send_reply(1);
        return CMD_ROUTE_COMMAND;
    }

    send_reply(0);
    return CMD_ROUTE_COMMAND;
}
