#include "usb_cdc.h"
#include "bridge.h"
#include "main.h"
#include "cmd_router.h"
#include <string.h>

extern uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);

static uint8_t  cdc_rx_line[BRIDGE_RX_LINE_MAX];
static uint16_t cdc_rx_len = 0;

void usb_cdc_send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return;

    for (int i = 0; i < 3; i++) {
        if (CDC_Transmit_FS((uint8_t *)data, len) == 0) {
            return;
        }
        HAL_Delay(1);
    }
}

void usb_cdc_on_rx(const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) return;

    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = buf[i];

        if (c == '\r') continue;

        if (c == '\n') {
            cdc_rx_line[cdc_rx_len] = '\0';

            cmd_route_t r = cmd_router_handle_line(cdc_rx_line, cdc_rx_len);

            if (r == CMD_ROUTE_DATA) {
                /* данные без магии — только если режим не RS_ETH */
                if (bridge_get_mode() != BRIDGE_MODE_RS_ETH) {
                    bridge_on_cdc_line(cdc_rx_line, cdc_rx_len);
                }
                /* в RS_ETH — игнор */
            }
            /* если CMD_ROUTE_COMMAND — ответ уже отправлен, ничего не делаем */

            cdc_rx_len = 0;
            continue;
        }

        if (cdc_rx_len < BRIDGE_RX_LINE_MAX - 1) {
            cdc_rx_line[cdc_rx_len++] = c;
        } else {
            cdc_rx_len = 0;
        }
    }
}
