#ifndef __CMD_ROUTER_H
#define __CMD_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    CMD_ROUTE_DATA    = 0,   /* не команда — пусть bridge решает */
    CMD_ROUTE_COMMAND = 1,   /* была команда, ответ уже отправлен */
} cmd_route_t;

/**
  * @brief  Проверить строку от CDC на магию и, если это команда, обработать.
  *         Ответ (<магия>,OK/ERROR) шлётся в CDC внутри.
  */
cmd_route_t cmd_router_handle_line(const uint8_t *line, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __CMD_ROUTER_H */
