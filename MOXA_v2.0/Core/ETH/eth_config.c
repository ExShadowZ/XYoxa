#include "eth_config.h"
#include "eth_bridge.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "bridge.h"
#include <string.h>

extern void rs_mux_disable(void);
extern void rs_all_xcvr_off(void);
extern void rs_uart_deinit(void);

extern wiz_NetInfo gWIZNETINFO;

void eth_apply_net_info(const esp_net_params_t *net)
{
    if (net == NULL) return;

    if (net->has_port) {
        eth_bridge_set_port(net->port);
    }
    if (net->has_mac) {
        memcpy(gWIZNETINFO.mac, net->mac, 6);
    }
    if (net->has_ip) {
        memcpy(gWIZNETINFO.ip, net->ip, 4);
    }
    if (net->has_sn) {
        memcpy(gWIZNETINFO.sn, net->sn, 4);
    }
    if (net->has_gw) {
        memcpy(gWIZNETINFO.gw, net->gw, 4);
    }
    if (net->has_dns) {
        memcpy(gWIZNETINFO.dns, net->dns, 4);
    }
    if (net->has_dhcp) {
        gWIZNETINFO.dhcp = (net->dhcp != 0) ? NETINFO_DHCP : NETINFO_STATIC;
    }

    /* Применяем к W5500 */
    wizchip_setnetinfo(&gWIZNETINFO);
    ctlnetwork(CN_SET_NETINFO, (void*)&gWIZNETINFO);
}

void eth_config_init(void)
{
    /* ничего */
}

void eth_config_start(void)
{
    int8_t ret;

    eth_bridge_set_running(0);
    eth_bridge_set_connected(0);

    close(ETH_BRIDGE_SOCKET);

    uint16_t port = eth_bridge_get_port();

    ret = socket(ETH_BRIDGE_SOCKET, Sn_MR_TCP, port, 0);
    if (ret != ETH_BRIDGE_SOCKET) return;

    if (listen(ETH_BRIDGE_SOCKET) != SOCK_OK) {
        close(ETH_BRIDGE_SOCKET);
        return;
    }

    eth_bridge_set_running(1);
}

static void eth_force_close(uint8_t sn)
{
    uint8_t sr = getSn_SR(sn);

    if (sr == SOCK_ESTABLISHED || sr == SOCK_CLOSE_WAIT) {
        setSn_CR(sn, Sn_CR_DISCON);
        while (getSn_CR(sn));

        uint32_t t = HAL_GetTick();
        while (getSn_SR(sn) != SOCK_CLOSED) {
            if (HAL_GetTick() - t > 500) {
                break;   /* 500 мс — и хватит, не ждём вечно */
            }
        }
    }

    close(sn);
}

void eth_config_stop(void)
{
    if (eth_bridge_is_running()) {
        eth_force_close(ETH_BRIDGE_SOCKET);
    }
    eth_bridge_set_running(0);
    eth_bridge_set_connected(0);
}

void eth_config_com_eth(const esp_net_params_t *net)
{
    /* 1. Остановить, если было запущено */
    eth_config_stop();

    /* 2. Применить сеть */
    eth_apply_net_info(net);

    /* 3. RS — off */
    rs_mux_disable();
    rs_all_xcvr_off();
    rs_uart_deinit();

    /* 4. Режим */
    bridge_set_mode(BRIDGE_MODE_ETH);

    /* 5. Старт */
    eth_config_start();
}
