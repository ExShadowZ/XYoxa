/**
  ******************************************************************************
  * @file    rs_config.h
  * @brief   Настройка RS-интерфейсов: мультиплексор 232/422/485/UART + USART1
  *
  *          Блок RS. НЕ знает про ESP и ETH.
  ******************************************************************************
  */

#ifndef __RS_CONFIG_H
#define __RS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum {
    RS_CH_NONE = -1,  /* ничего не выбрано (мультиплексор выключен) */
    RS_CH_485  = 0,   /* A1=0, A0=0 → Y0 (MAX485) */
    RS_CH_232  = 1,   /* A1=0, A0=1 → Y1 (MAX232) */
    RS_CH_422  = 2,   /* A1=1, A0=0 → Y2 (MAX489) */
    RS_CH_UART = 3    /* A1=1, A0=1 → Y3 (прямой TTL UART) */
} rs_channel_t;

typedef enum {
    RS_XCVR_OFF = 0,
    RS_XCVR_RX  = 1,
    RS_XCVR_TX  = 2,
} rs_xcvr_mode_t;

typedef struct {
    uint32_t baudrate;      /* 9600, 115200, ... */
    uint32_t word_length;   /* UART_WORDLENGTH_8B / 9B */
    uint32_t stop_bits;     /* UART_STOPBITS_1 / 2 */
    uint32_t parity;        /* UART_PARITY_NONE / EVEN / ODD */
} rs_uart_config_t;

typedef struct {
    uint32_t baud;
    uint8_t  databits;    /* 5..9 */
    uint8_t  parity;      /* 0=None, 1=Even, 2=Odd */
    uint8_t  stopbits;    /* 1 или 2 */
    uint8_t  handshake;   /* 0=None, 1=RTS/CTS, 2=XON/XOFF */
} raw_uart_params_t;


void rs_mux_enable(void);
void rs_mux_disable(void);
void rs_mux_select(rs_channel_t ch);
rs_channel_t rs_get_active_channel(void);
void rs_485_set_mode(rs_xcvr_mode_t mode);
void rs_422_set_mode(rs_xcvr_mode_t mode);
void rs_all_xcvr_off(void);
HAL_StatusTypeDef rs_uart_init(const rs_uart_config_t *cfg);
void rs_uart_deinit(void);
void rs_config_com_uart (const raw_uart_params_t *raw);
void rs_config_com_rs232(const raw_uart_params_t *raw);
void rs_config_com_rs485(const raw_uart_params_t *raw);
void rs_config_com_rs422(const raw_uart_params_t *raw);
void rs_config_apply_uart(const raw_uart_params_t *raw);
void rs_config_eth_uart (const raw_uart_params_t *raw);
void rs_config_eth_rs232(const raw_uart_params_t *raw);
void rs_config_eth_rs485(const raw_uart_params_t *raw);
void rs_config_eth_rs422(const raw_uart_params_t *raw);
void rs_config_com_rs485_4w(const raw_uart_params_t *raw);
void rs_config_eth_rs485_4w(const raw_uart_params_t *raw);

#ifdef __cplusplus
}
#endif

#endif /* __RS_CONFIG_H */
