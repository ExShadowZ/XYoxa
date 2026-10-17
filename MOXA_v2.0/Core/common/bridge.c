#include "bridge.h"
#include "usb_cdc.h"
#include "main.h"
#include "rs_config.h"
#include "eth_config.h"
#include <string.h>

extern UART_HandleTypeDef huart2;
extern void esp_send_raw(const uint8_t *buf, uint16_t len);
extern void rs_bridge_send_line(const uint8_t *line, uint16_t len);
extern void rs_bridge_send_raw (const uint8_t *data, uint16_t len);
extern void eth_bridge_send(const uint8_t *buf, uint16_t len);

static bridge_mode_t bridge_mode = BRIDGE_MODE_RS;
static uint8_t  bridge_uart2_tx_buf[BRIDGE_TX_BUF_SIZE];
static volatile uint8_t bridge_uart2_tx_busy = 0;

void bridge_set_mode(bridge_mode_t mode) { bridge_mode = mode; }
bridge_mode_t bridge_get_mode(void)      { return bridge_mode; }

void bridge_init(void)
{
    bridge_uart2_tx_busy = 0;
    bridge_mode = BRIDGE_MODE_RS;
}

static void bridge_send_uart2(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > BRIDGE_TX_BUF_SIZE) return;

    uint32_t t = HAL_GetTick();
    while (bridge_uart2_tx_busy && (HAL_GetTick() - t < 200)) { }
    if (bridge_uart2_tx_busy) return;

    memcpy(bridge_uart2_tx_buf, data, len);
    bridge_uart2_tx_busy = 1;

    if (HAL_UART_Transmit_DMA(&huart2, bridge_uart2_tx_buf, len) != HAL_OK) {
        bridge_uart2_tx_busy = 0;
    }
}

void bridge_send_to_cdc(const uint8_t *data, uint16_t len)
{
    usb_cdc_send(data, len);
}

static void bridge_dispatch_line_rs(const uint8_t *line, uint16_t len)
{
    rs_bridge_send_line(line, len);

    static const char prefix[] = BRIDGE_USART2_PREFIX_TX;
    uint16_t prefix_len = sizeof(prefix) - 1;

    if (prefix_len + len + 1 <= BRIDGE_TX_BUF_SIZE) {
        static uint8_t tmp[BRIDGE_TX_BUF_SIZE];
        memcpy(tmp, prefix, prefix_len);
        memcpy(tmp + prefix_len, line, len);
        tmp[prefix_len + len] = '\n';
        bridge_send_uart2(tmp, prefix_len + len + 1);
    }
}

static void bridge_dispatch_line_eth(const uint8_t *line, uint16_t len)
{
    if (len + 1 <= BRIDGE_TX_BUF_SIZE) {
        static uint8_t tmp[BRIDGE_TX_BUF_SIZE];
        memcpy(tmp, line, len);
        tmp[len] = '\n';
        eth_bridge_send(tmp, len + 1);
    }

    static const char prefix[] = "COM:";
    uint16_t prefix_len = sizeof(prefix) - 1;

    if (prefix_len + len + 1 <= BRIDGE_TX_BUF_SIZE) {
        static uint8_t tmp[BRIDGE_TX_BUF_SIZE];
        memcpy(tmp, prefix, prefix_len);
        memcpy(tmp + prefix_len, line, len);
        tmp[prefix_len + len] = '\n';
        bridge_send_uart2(tmp, prefix_len + len + 1);
    }
}

void bridge_on_cdc_line(const uint8_t *line, uint16_t len)
{
    switch (bridge_mode) {
    case BRIDGE_MODE_ETH:
        bridge_dispatch_line_eth(line, len);
        break;
    case BRIDGE_MODE_RS_ETH:
        /* CDC игнорируется */
        break;
    case BRIDGE_MODE_RS:
    default:
        bridge_dispatch_line_rs(line, len);
        break;
    }
}

int bridge_apply_settings(uint8_t dev1, uint8_t dev2,
                          const raw_uart_params_t *raw,
                          const esp_net_params_t  *net)
{
    uint8_t a = (dev1 < dev2) ? dev1 : dev2;
    uint8_t b = (dev1 < dev2) ? dev2 : dev1;

    switch ((a << 4) | b) {
    /* --- пары с COM (0) --- */
    case 0x01: eth_config_com_eth(net);           return 0;   /* {0,1} COM ↔ ETH    */
    case 0x02: rs_config_com_uart(raw);           return 0;   /* {0,2} COM ↔ UART   */
    case 0x03: rs_config_com_rs232(raw);          return 0;   /* {0,3} COM ↔ RS-232 */
    case 0x04: rs_config_com_rs485(raw);          return 0;   /* {0,4} COM ↔ RS-485 2W */
    case 0x05: rs_config_com_rs422(raw);          return 0;   /* {0,5} COM ↔ RS-422 */
    case 0x06: rs_config_com_rs485_4w(raw);       return 0;   /* {0,6} COM ↔ RS-485 4W */

    /* --- пары с ETH (1), без COM --- */
    case 0x12: eth_apply_net_info(net);
               rs_config_eth_uart(raw);
               eth_config_start();                return 0;   /* {1,2} ETH ↔ UART   */

    case 0x13: eth_apply_net_info(net);
               rs_config_eth_rs232(raw);
               eth_config_start();                return 0;   /* {1,3} ETH ↔ RS-232 */

    case 0x14: eth_apply_net_info(net);
               rs_config_eth_rs485(raw);
               eth_config_start();                return 0;   /* {1,4} ETH ↔ RS-485 2W */

    case 0x15: eth_apply_net_info(net);
               rs_config_eth_rs422(raw);
               eth_config_start();                return 0;   /* {1,5} ETH ↔ RS-422 */

    case 0x16: eth_apply_net_info(net);
               rs_config_eth_rs485_4w(raw);
               eth_config_start();                return 0;   /* {1,6} ETH ↔ RS-485 4W */

    default:
        return -1;
    }
}

void bridge_on_uart2_tx_done(void)
{
    bridge_uart2_tx_busy = 0;
}
