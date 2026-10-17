#include "rs_config.h"

extern UART_HandleTypeDef huart1;

#include "bridge.h"
#include "eth_config.h"

static rs_channel_t s_active_channel = RS_CH_NONE;

/* ============================================================
 * Управление мультиплексором (74HC4052D)
 *
 *   E0 (Enable): активный НИЗКИЙ уровень
 *   A0, A1:      адресация канала
 *
 *   A1 | A0 | Канал
 *   ---+----+-------
 *    0 |  0 | 485  (Y0)
 *    0 |  1 | 232  (Y1)
 *    1 |  0 | 422  (Y2)
 *    1 |  1 | UART (Y3)
 * ============================================================ */

void rs_mux_enable(void)
{
    HAL_GPIO_WritePin(E0_GPIO_Port, E0_Pin, GPIO_PIN_RESET);
}

void rs_mux_disable(void)
{
    HAL_GPIO_WritePin(E0_GPIO_Port, E0_Pin, GPIO_PIN_SET);
    s_active_channel = RS_CH_NONE;
}

void rs_mux_select(rs_channel_t ch)
{
    switch (ch) {
    case RS_CH_485:                 /* A1=0, A0=0 */
        HAL_GPIO_WritePin(A1_GPIO_Port, A1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, GPIO_PIN_RESET);
        s_active_channel = RS_CH_485;
        break;

    case RS_CH_232:                 /* A1=0, A0=1 */
        HAL_GPIO_WritePin(A1_GPIO_Port, A1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, GPIO_PIN_SET);
        s_active_channel = RS_CH_232;
        break;

    case RS_CH_422:                 /* A1=1, A0=0 */
        HAL_GPIO_WritePin(A1_GPIO_Port, A1_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, GPIO_PIN_RESET);
        s_active_channel = RS_CH_422;
        break;

    case RS_CH_UART:                /* A1=1, A0=1 */
        HAL_GPIO_WritePin(A1_GPIO_Port, A1_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(A0_GPIO_Port, A0_Pin, GPIO_PIN_SET);
        s_active_channel = RS_CH_UART;
        break;

    default:
        s_active_channel = RS_CH_NONE;
        break;
    }
}

rs_channel_t rs_get_active_channel(void)
{
    return s_active_channel;
}

/* ============================================================
 * Управление трансиверами RS-485 / RS-422
 *
 *   RS-485: RE_2x485 (active-low), DE_2x485 (active-high)
 *   RS-422: RE_4x485 (active-low), DE_4x485 (active-high)
 * ============================================================ */

static void rs_apply(rs_xcvr_mode_t mode,
                     GPIO_TypeDef *re_port, uint16_t re_pin,
                     GPIO_TypeDef *de_port, uint16_t de_pin)
{
    switch (mode) {
    case RS_XCVR_TX:                    /* RE=1, DE=1 */
        HAL_GPIO_WritePin(re_port, re_pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(de_port, de_pin, GPIO_PIN_SET);
        break;

    case RS_XCVR_RX:                    /* RE=0, DE=0 */
        HAL_GPIO_WritePin(re_port, re_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(de_port, de_pin, GPIO_PIN_RESET);
        break;

    case RS_XCVR_OFF:
    default:                            /* RE=1, DE=0 */
        HAL_GPIO_WritePin(re_port, re_pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(de_port, de_pin, GPIO_PIN_RESET);
        break;
    }
}

void rs_485_set_mode(rs_xcvr_mode_t mode)
{
    rs_apply(mode,
             RE_2x485_GPIO_Port, RE_2x485_Pin,
             DE_2x485_GPIO_Port, DE_2x485_Pin);
}

void rs_422_set_mode(rs_xcvr_mode_t mode)
{
    rs_apply(mode,
             RE_4x485_GPIO_Port, RE_4x485_Pin,
             DE_4x485_GPIO_Port, DE_4x485_Pin);
}

void rs_all_xcvr_off(void)
{
    rs_485_set_mode(RS_XCVR_OFF);
    rs_422_set_mode(RS_XCVR_OFF);
}

HAL_StatusTypeDef rs_uart_init(const rs_uart_config_t *cfg)
{
    if (cfg == NULL) {
        return HAL_ERROR;
    }

    if (HAL_UART_DeInit(&huart1) != HAL_OK) {
        return HAL_ERROR;
    }

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = cfg->baudrate;
    huart1.Init.WordLength   = cfg->word_length;
    huart1.Init.StopBits     = cfg->stop_bits;
    huart1.Init.Parity       = cfg->parity;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_StatusTypeDef status = HAL_UART_Init(&huart1);
    if (status == HAL_OK) {
    	rs_bridge_init();   /* перезапуск приёма DMA+IDLE и сброс TX-флагов */
	}
	return status;
}

void rs_uart_deinit(void)
{
    HAL_UART_DeInit(&huart1);
}

void rs_config_apply_uart(const raw_uart_params_t *raw)
{
    if (raw == NULL) return;

    /* Маппинг databits → word_length */
    uint32_t word_length = (raw->databits == 9)
                           ? UART_WORDLENGTH_9B
                           : UART_WORDLENGTH_8B;   /* 5/6/7 пока мапим в 8B */

    /* Маппинг parity */
    uint32_t parity;
    switch (raw->parity) {
    case 1:  parity = UART_PARITY_EVEN; break;
    case 2:  parity = UART_PARITY_ODD;  break;
    default: parity = UART_PARITY_NONE; break;
    }

    /* Маппинг stopbits */
    uint32_t stop_bits = (raw->stopbits == 2)
                         ? UART_STOPBITS_2
                         : UART_STOPBITS_1;

    rs_uart_config_t cfg = {
        .baudrate    = raw->baud,
        .word_length = word_length,
        .stop_bits   = stop_bits,
        .parity      = parity,
    };

    /* Включаем мультиплексор и переинициализируем USART1 */
    rs_mux_enable();
    rs_uart_init(&cfg);
}

void rs_config_com_uart(const raw_uart_params_t *raw)
{
	eth_config_stop();
    bridge_set_mode(BRIDGE_MODE_RS);
    /* Все трансиверы выключены — работаем с прямым TTL */
    rs_all_xcvr_off();

    rs_mux_select(RS_CH_UART);
    rs_config_apply_uart(raw);
}

void rs_config_com_rs232(const raw_uart_params_t *raw)
{
	eth_config_stop();
    bridge_set_mode(BRIDGE_MODE_RS);
    /* MAX232 не требует DE/RE, но 485/422 надо вырубить */
    rs_all_xcvr_off();

    rs_mux_select(RS_CH_232);
    rs_config_apply_uart(raw);
}

void rs_config_com_rs485(const raw_uart_params_t *raw)
{
	eth_config_stop();
    bridge_set_mode(BRIDGE_MODE_RS);
    /* Выключаем 422 */
    rs_422_set_mode(RS_XCVR_OFF);

    /* 485 — включаем в режим приёма */
    rs_485_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_485);
    rs_config_apply_uart(raw);
}

void rs_config_com_rs422(const raw_uart_params_t *raw)
{
	eth_config_stop();
    bridge_set_mode(BRIDGE_MODE_RS);
    /* Выключаем 485 */
    rs_485_set_mode(RS_XCVR_OFF);

    /* 422 — включаем в режим приёма */
    rs_422_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_422);
    rs_config_apply_uart(raw);
}

void rs_config_eth_uart(const raw_uart_params_t *raw)
{
    /* Все трансиверы выключены — работаем с прямым TTL */
    rs_all_xcvr_off();

    rs_mux_select(RS_CH_UART);
    rs_config_apply_uart(raw);

    /* Переключаем bridge в режим RS ↔ ETH */
    bridge_set_mode(BRIDGE_MODE_RS_ETH);
}

void rs_config_eth_rs232(const raw_uart_params_t *raw)
{
    /* MAX232 не требует DE/RE, но 485/422 надо вырубить */
    rs_all_xcvr_off();

    rs_mux_select(RS_CH_232);
    rs_config_apply_uart(raw);

    bridge_set_mode(BRIDGE_MODE_RS_ETH);
}

void rs_config_eth_rs485(const raw_uart_params_t *raw)
{
    /* Выключаем 422 */
    rs_422_set_mode(RS_XCVR_OFF);

    /* 485 — включаем в режим приёма */
    rs_485_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_485);
    rs_config_apply_uart(raw);

    bridge_set_mode(BRIDGE_MODE_RS_ETH);
}

void rs_config_eth_rs422(const raw_uart_params_t *raw)
{
    /* Выключаем 485 */
    rs_485_set_mode(RS_XCVR_OFF);

    /* 422 — включаем в режим приёма */
    rs_422_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_422);
    rs_config_apply_uart(raw);

    bridge_set_mode(BRIDGE_MODE_RS_ETH);
}

void rs_config_com_rs485_4w(const raw_uart_params_t *raw)
{
    eth_config_stop();
    bridge_set_mode(BRIDGE_MODE_RS);

    /* 485 (2W) — off */
    rs_485_set_mode(RS_XCVR_OFF);
    /* 422-трансивер работает как 485 4W — в RX */
    rs_422_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_422);
    rs_config_apply_uart(raw);
}

void rs_config_eth_rs485_4w(const raw_uart_params_t *raw)
{
    rs_485_set_mode(RS_XCVR_OFF);
    rs_422_set_mode(RS_XCVR_RX);

    rs_mux_select(RS_CH_422);
    rs_config_apply_uart(raw);

    bridge_set_mode(BRIDGE_MODE_RS_ETH);
}
