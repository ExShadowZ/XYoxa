#ifndef ETH_BRIDGE_H_
#define ETH_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ETH_BRIDGE_SOCKET       1       /* номер сокета W5500 */
#define ETH_BRIDGE_PORT         5000    /* TCP-порт */
#define ETH_BRIDGE_RX_BUF       1024    /* буфер приёма из сокета */
#define ETH_BRIDGE_TX_BUF       1024    /* буфер отправки в сокет */

void eth_bridge_poll(void);
void eth_bridge_send(const uint8_t *buf, uint16_t len);
void eth_bridge_set_running(uint8_t on);
void eth_bridge_set_connected(uint8_t on);
uint8_t eth_bridge_is_running(void);
void     eth_bridge_set_port(uint16_t port);
uint16_t eth_bridge_get_port(void);

#ifdef __cplusplus
}
#endif

#endif /* ETH_BRIDGE_H_ */
