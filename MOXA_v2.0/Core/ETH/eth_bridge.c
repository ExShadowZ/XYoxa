#include "eth_bridge.h"
#include "eth_config.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "main.h"
#include "bridge.h"
#include <string.h>

extern void esp_send_raw(const uint8_t *buf, uint16_t len);
extern void bridge_send_to_cdc(const uint8_t *data, uint16_t len);
extern void rs_bridge_send_raw(const uint8_t *data, uint16_t len);

static uint8_t  eth_rx_buf[ETH_BRIDGE_RX_BUF];
static uint8_t  eth_tx_buf[ETH_BRIDGE_TX_BUF];
static volatile uint8_t eth_running = 0;
static volatile uint8_t eth_connected = 0;

void eth_bridge_set_running(uint8_t on)
{
    eth_running = on;
}

void eth_bridge_set_connected(uint8_t on)
{
    eth_connected = on;
}

uint8_t eth_bridge_is_running(void)
{
    return eth_running;
}

static void eth_to_esp_tcp(const uint8_t *data, uint16_t len)
{
    static const char prefix[] = "TCP:";
    uint16_t prefix_len = sizeof(prefix) - 1;

    if (prefix_len + len + 1 > ETH_BRIDGE_TX_BUF) {
        len = ETH_BRIDGE_TX_BUF - prefix_len - 1;
    }

    memcpy(eth_tx_buf, prefix, prefix_len);
    memcpy(eth_tx_buf + prefix_len, data, len);
    eth_tx_buf[prefix_len + len] = '\n';

    esp_send_raw(eth_tx_buf, prefix_len + len + 1);
}

void eth_bridge_send(const uint8_t *buf, uint16_t len)
{
    if (!eth_running || !eth_connected) return;
    if (len == 0 || len > ETH_BRIDGE_TX_BUF) return;

    memcpy(eth_tx_buf, buf, len);

    /* send() в blocking режиме ждёт, пока всё уйдёт */
    int32_t sent = send(ETH_BRIDGE_SOCKET, eth_tx_buf, len);
    (void)sent;  /* можно залогировать, если нужно */
}

void eth_bridge_poll(void)
{
    if (!eth_running) return;

    uint8_t sr = getSn_SR(ETH_BRIDGE_SOCKET);

    switch (sr) {
    case SOCK_LISTEN:
        eth_connected = 0;
        break;

    case SOCK_ESTABLISHED:
        eth_connected = 1;
        break;

    case SOCK_CLOSE_WAIT:
        /* Клиент закрыл соединение */
        disconnect(ETH_BRIDGE_SOCKET);
        eth_connected = 0;
        break;

    case SOCK_CLOSED:
        /* Сокет почему-то закрылся — перезапустить */
        eth_connected = 0;
        if (eth_running) {
            eth_config_start();
        }
        return;
    }

    /* Приём из сокета */
    if (eth_connected) {
        uint16_t avail = getSn_RX_RSR(ETH_BRIDGE_SOCKET);
        while (avail > 0) {
            uint16_t chunk = (avail > ETH_BRIDGE_RX_BUF)
                             ? ETH_BRIDGE_RX_BUF
                             : avail;

            int32_t recvd = recv(ETH_BRIDGE_SOCKET, eth_rx_buf, chunk);
            if (recvd <= 0) break;

            bridge_mode_t mode = bridge_get_mode();

            if (mode == BRIDGE_MODE_RS_ETH) {
                /* Режим RS ↔ ETH: в RS + ESP (TCP:), CDC игнор */
                rs_bridge_send_raw(eth_rx_buf, (uint16_t)recvd);
                eth_to_esp_tcp(eth_rx_buf, (uint16_t)recvd);
            } else {
                /* Режим CDC ↔ ETH (и fallback): в CDC + ESP (TCP:) */
                bridge_send_to_cdc(eth_rx_buf, (uint16_t)recvd);
                eth_to_esp_tcp(eth_rx_buf, (uint16_t)recvd);
            }

            avail = getSn_RX_RSR(ETH_BRIDGE_SOCKET);
        }
    }
}

static uint16_t eth_port = ETH_BRIDGE_PORT;

void eth_bridge_set_port(uint16_t port)
{
    if (port > 0) eth_port = port;
}

uint16_t eth_bridge_get_port(void)
{
    return eth_port;
}
