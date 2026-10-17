#ifndef __ETH_CONFIG_H
#define __ETH_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_commands.h"

void eth_config_init(void);
void eth_config_start(void);
void eth_config_stop(void);
void eth_config_com_eth(const esp_net_params_t *net);
void eth_apply_net_info(const esp_net_params_t *net);

#ifdef __cplusplus
}
#endif

#endif /* __ETH_CONFIG_H */
