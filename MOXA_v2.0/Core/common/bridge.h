#ifndef __BRIDGE_H
#define __BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "rs_config.h"
#include "esp_commands.h"

#define BRIDGE_RX_LINE_MAX   			256
#define BRIDGE_TX_BUF_SIZE   			512
#define BRIDGE_USART2_PREFIX_TX 		"TX:"
#define BRIDGE_USART2_PREFIX_RX 		"RX:"

typedef enum {
    BRIDGE_MODE_RS  = 0,   /* USART1 (RS) + USART2 (ESP) */
    BRIDGE_MODE_ETH = 1,   /* W5500 TCP + USART2 (ESP)   */
	BRIDGE_MODE_RS_ETH  = 2,   /* RS  ↔ TCP + ESP (RX:/TCP:), CDC игнор */
} bridge_mode_t;

void bridge_init(void);
void bridge_set_mode(bridge_mode_t mode);
bridge_mode_t bridge_get_mode(void);
int bridge_apply_settings(uint8_t dev1, uint8_t dev2, const raw_uart_params_t *raw, const esp_net_params_t  *net);
void bridge_on_cdc_line(const uint8_t *line, uint16_t len);
void bridge_send_to_cdc(const uint8_t *data, uint16_t len);
void bridge_on_uart2_tx_done(void);

#ifdef __cplusplus
}
#endif

#endif /* __BRIDGE_H */
