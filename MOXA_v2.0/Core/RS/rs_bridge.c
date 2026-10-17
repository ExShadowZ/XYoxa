#include "rs_bridge.h"
#include "rs_config.h"
#include "main.h"
#include "bridge.h"
#include <string.h>

extern UART_HandleTypeDef huart1;
extern void esp_send_raw(const uint8_t *buf, uint16_t len);
extern void bridge_send_to_cdc(const uint8_t *data, uint16_t len);

#define RS_BRIDGE_USART2_PREFIX_RX "RX:"

static uint8_t  rs_uart1_tx_buf[RS_BRIDGE_TX_BUF_SIZE];
static volatile uint8_t rs_uart1_tx_busy = 0;
static volatile rs_channel_t rs_uart1_tx_channel = RS_CH_NONE;
static uint8_t  rs_uart1_rx_buf[RS_BRIDGE_RX_BUF_SIZE];

static void rs_send_uart1(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > RS_BRIDGE_TX_BUF_SIZE) return;

    uint32_t t = HAL_GetTick();
    while (rs_uart1_tx_busy && (HAL_GetTick() - t < 200)) { /* wait */ }
    if (rs_uart1_tx_busy) return;

    rs_channel_t ch = rs_get_active_channel();

    if (ch == RS_CH_485) {
        rs_485_set_mode(RS_XCVR_TX);
    } else if (ch == RS_CH_422) {
        rs_422_set_mode(RS_XCVR_TX);
    }

    memcpy(rs_uart1_tx_buf, data, len);

    rs_uart1_tx_channel = ch;
    rs_uart1_tx_busy = 1;

    if (HAL_UART_Transmit_DMA(&huart1, rs_uart1_tx_buf, len) != HAL_OK) {
        rs_uart1_tx_busy = 0;
        rs_uart1_tx_channel = RS_CH_NONE;
        if (ch == RS_CH_485) {
            rs_485_set_mode(RS_XCVR_RX);
        } else if (ch == RS_CH_422) {
            rs_422_set_mode(RS_XCVR_RX);
        }
    }
}

void rs_bridge_init(void)
{
    rs_uart1_tx_busy = 0;
    rs_uart1_tx_channel = RS_CH_NONE;

    /* Запуск приёма на USART1 через DMA+IDLE */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rs_uart1_rx_buf, RS_BRIDGE_RX_BUF_SIZE);
}

static void rs_dispatch_rx(const uint8_t *data, uint16_t len)
{
    if (len == 0) return;

    bridge_mode_t mode = bridge_get_mode();

    if (mode == BRIDGE_MODE_RS_ETH) {
        /* Режим RS ↔ ETH: в TCP + ESP (RX:), CDC игнор */
        eth_bridge_send(data, len);
    } else {
        /* Режим CDC ↔ RS (и fallback): в CDC */
        bridge_send_to_cdc(data, len);
    }

    /* --- в ESP: "RX:" + данные (всегда) --- */
    static const char prefix[] = RS_BRIDGE_USART2_PREFIX_RX;
    uint16_t prefix_len = sizeof(prefix) - 1;

    if (prefix_len + len <= RS_BRIDGE_TX_BUF_SIZE) {
        static uint8_t tmp[RS_BRIDGE_TX_BUF_SIZE];
        memcpy(tmp, prefix, prefix_len);
        memcpy(tmp + prefix_len, data, len);
        esp_send_raw(tmp, prefix_len + len);
    }
}

void rs_bridge_send_line(const uint8_t *line, uint16_t len)
{
    if (len == 0) return;

    /* --- USART1: строка + \n --- */
    if (len + 1 <= RS_BRIDGE_TX_BUF_SIZE) {
        static uint8_t tmp[RS_BRIDGE_TX_BUF_SIZE];
        memcpy(tmp, line, len);
        tmp[len] = '\n';
        rs_send_uart1(tmp, len + 1);
    }
}

void rs_bridge_send_raw(const uint8_t *data, uint16_t len)
{
    if (len == 0) return;
    rs_send_uart1(data, len);
}

void rs_bridge_on_tx_done(void)
{
    rs_channel_t ch = rs_uart1_tx_channel;
    rs_uart1_tx_channel = RS_CH_NONE;
    rs_uart1_tx_busy = 0;

    if (ch == RS_CH_485) {
        rs_485_set_mode(RS_XCVR_RX);
    } else if (ch == RS_CH_422) {
        rs_422_set_mode(RS_XCVR_RX);
    }
}

void rs_bridge_on_rx_event(uint16_t size)
{
    if (size > RS_BRIDGE_RX_BUF_SIZE) {
        size = RS_BRIDGE_RX_BUF_SIZE;
    }

    if (size > 0) {
        /* Шлём частями, если size > RS_BRIDGE_TX_BUF_SIZE - prefix_len */
        static const char prefix[] = RS_BRIDGE_USART2_PREFIX_RX;
        uint16_t prefix_len = sizeof(prefix) - 1;
        uint16_t max_chunk = RS_BRIDGE_TX_BUF_SIZE - prefix_len;

        uint16_t offset = 0;
        while (offset < size) {
            uint16_t chunk = size - offset;
            if (chunk > max_chunk) {
                chunk = max_chunk;
            }
            rs_dispatch_rx(rs_uart1_rx_buf + offset, chunk);
            offset += chunk;
        }
    }

    /* Перезапуск приёма */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rs_uart1_rx_buf, RS_BRIDGE_RX_BUF_SIZE);
}
