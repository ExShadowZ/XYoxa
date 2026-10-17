#ifndef __ESP_COMMANDS_H
#define __ESP_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "rs_config.h"

typedef struct {
    uint16_t port;
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  sn[4];
    uint8_t  gw[4];
    uint8_t  dns[4];
    uint8_t  dhcp;

    uint8_t  has_port;
    uint8_t  has_mac;
    uint8_t  has_ip;
    uint8_t  has_sn;
    uint8_t  has_gw;
    uint8_t  has_dns;
    uint8_t  has_dhcp;
} esp_net_params_t;

typedef struct {
    uint8_t  dev1;
    uint8_t  dev2;
    raw_uart_params_t raw;

    esp_net_params_t net;
} esp_set_cmd_t;

int esp_parse_set_cmd(const char *params, esp_set_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_COMMANDS_H */
