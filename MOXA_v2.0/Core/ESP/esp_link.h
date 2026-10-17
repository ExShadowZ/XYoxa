#ifndef __ESP_LINK_H
#define __ESP_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define ESP_RX_LINE_MAX     256
#define ESP_TX_BUF_SIZE     512

void esp_link_init(void);
void esp_send_line(const char *str);
void esp_send_raw(const uint8_t *buf, uint16_t len);
void esp_on_command(const char *line);
void esp_link_on_rx_byte(void);
void esp_link_on_tx_done(void);
void esp_link_on_error(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_LINK_H */
