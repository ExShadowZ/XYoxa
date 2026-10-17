#include "esp_commands.h"
#include "esp_link.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern int bridge_apply_settings(uint8_t dev1, uint8_t dev2,
                                 const raw_uart_params_t *raw,
                                 const esp_net_params_t *net);

static const char *find_param(const char *params, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = params;

    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            return p + key_len + 1;
        }
        const char *comma = strchr(p, ',');
        if (!comma) break;
        p = comma + 1;
    }
    return NULL;
}

static void get_value(const char *val_start, char *out, size_t out_size)
{
    if (!val_start) { out[0] = '\0'; return; }
    size_t i = 0;
    while (val_start[i] && val_start[i] != ',' && i < out_size - 1) {
        out[i] = val_start[i];
        i++;
    }
    out[i] = '\0';
}

static int parse_mac(const char *s, uint8_t *mac)
{
    unsigned int v[6];
    int n = sscanf(s, "%x:%x:%x:%x:%x:%x",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    if (n != 6) return -1;
    for (int i = 0; i < 6; i++) {
        if (v[i] > 0xFF) return -1;
        mac[i] = (uint8_t)v[i];
    }
    return 0;
}

static int parse_ip(const char *s, uint8_t *ip)
{
    unsigned int v[4];
    int n = sscanf(s, "%u.%u.%u.%u", &v[0], &v[1], &v[2], &v[3]);
    if (n != 4) return -1;
    for (int i = 0; i < 4; i++) {
        if (v[i] > 255) return -1;
        ip[i] = (uint8_t)v[i];
    }
    return 0;
}

int esp_parse_set_cmd(const char *params, esp_set_cmd_t *out)
{
    if (out == NULL) return -1;

    char val[32];

    memset(out, 0, sizeof(*out));

    const char *p_dev1      = find_param(params, "DEV1");
    const char *p_dev2      = find_param(params, "DEV2");
    const char *p_baud      = find_param(params, "BAUD");
    const char *p_databits  = find_param(params, "DATABITS");
    const char *p_parity    = find_param(params, "PARITY");
    const char *p_stopbits  = find_param(params, "STOPBITS");
    const char *p_handshake = find_param(params, "HANDSHAKE");

    /* UART-часть — значения по умолчанию */
    out->dev1 = 0;
    out->dev2 = 0;
    out->raw.baud      = 115200;
    out->raw.databits  = 8;
    out->raw.parity    = 0;
    out->raw.stopbits  = 1;
    out->raw.handshake = 0;

    get_value(p_dev1, val, sizeof(val)); out->dev1 = (uint8_t)atoi(val);
    get_value(p_dev2, val, sizeof(val)); out->dev2 = (uint8_t)atoi(val);

    get_value(p_baud,      val, sizeof(val)); out->raw.baud      = strtoul(val, NULL, 10);
    get_value(p_databits,  val, sizeof(val)); out->raw.databits  = (uint8_t)atoi(val);
    get_value(p_parity,    val, sizeof(val)); out->raw.parity    = (uint8_t)atoi(val);
    get_value(p_stopbits,  val, sizeof(val)); out->raw.stopbits  = (uint8_t)atoi(val);
    get_value(p_handshake, val, sizeof(val)); out->raw.handshake = (uint8_t)atoi(val);

    /* Сетевые поля — парсим ВСЕГДА, если они есть в строке.
       Для DEV1=0 их обычно нет — тогда has_* останутся 0. */
    const char *p_port = find_param(params, "PORT");
    const char *p_mac  = find_param(params, "MAC");
    const char *p_ip   = find_param(params, "IP");
    const char *p_sn   = find_param(params, "SN");
    const char *p_gw   = find_param(params, "GW");
    const char *p_dns  = find_param(params, "DNS");
    const char *p_dhcp = find_param(params, "DHCP");

    if (p_port) {
        get_value(p_port, val, sizeof(val));
        out->net.port = (uint16_t)atoi(val);
        out->net.has_port = 1;
    }
    if (p_mac) {
        get_value(p_mac, val, sizeof(val));
        if (parse_mac(val, out->net.mac) == 0) out->net.has_mac = 1;
    }
    if (p_ip) {
        get_value(p_ip, val, sizeof(val));
        if (parse_ip(val, out->net.ip) == 0) out->net.has_ip = 1;
    }
    if (p_sn) {
        get_value(p_sn, val, sizeof(val));
        if (parse_ip(val, out->net.sn) == 0) out->net.has_sn = 1;
    }
    if (p_gw) {
        get_value(p_gw, val, sizeof(val));
        if (parse_ip(val, out->net.gw) == 0) out->net.has_gw = 1;
    }
    if (p_dns) {
        get_value(p_dns, val, sizeof(val));
        if (parse_ip(val, out->net.dns) == 0) out->net.has_dns = 1;
    }
    if (p_dhcp) {
        get_value(p_dhcp, val, sizeof(val));
        out->net.dhcp = (uint8_t)atoi(val);
        out->net.has_dhcp = 1;
    }

    return 0;
}

void esp_on_command(const char *line)
{
    if (line == NULL || line[0] == '\0') return;

    if (strncmp(line, "CMD=", 4) != 0) return;

    const char *cmd = line + 4;

    if (strncmp(cmd, "CONNECT", 7) == 0) {
        esp_send_line("OK");
        return;
    }

    if (strncmp(cmd, "SET", 3) == 0) {
        const char *params = (cmd[3] == ',') ? cmd + 4 : "";

        esp_set_cmd_t parsed;
        esp_parse_set_cmd(params, &parsed);

        if (bridge_apply_settings(parsed.dev1, parsed.dev2,
                                  &parsed.raw, &parsed.net) != 0) {
            esp_send_line("ERROR");
            return;
        }

        esp_send_line("OK");
        return;
    }

    esp_send_line("ERROR");
}
