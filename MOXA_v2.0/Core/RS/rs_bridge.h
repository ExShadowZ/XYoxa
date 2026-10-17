#ifndef __RS_BRIDGE_H
#define __RS_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RS_BRIDGE_TX_BUF_SIZE   		512
#define RS_BRIDGE_RX_BUF_SIZE   		512

void rs_bridge_init(void);
void rs_bridge_on_rx_event(uint16_t size);
void rs_bridge_on_tx_done(void);
void rs_bridge_send_line(const uint8_t *line, uint16_t len);
void rs_bridge_send_raw(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __RS_BRIDGE_H */
