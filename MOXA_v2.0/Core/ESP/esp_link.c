#include "esp_link.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;

static uint8_t  esp_rx_byte;
static char     esp_rx_line[ESP_RX_LINE_MAX];
static uint16_t esp_rx_len = 0;
static uint8_t  esp_tx_buf[ESP_TX_BUF_SIZE];
static volatile uint8_t esp_tx_busy = 0;

void esp_link_init(void)
{
    esp_rx_len  = 0;
    esp_tx_busy = 0;

    /* Запуск приёма по одному байту через прерывание */
    HAL_UART_Receive_IT(&huart2, &esp_rx_byte, 1);
}

void esp_send_raw(const uint8_t *buf, uint16_t len)
{
    if (len == 0 || len > ESP_TX_BUF_SIZE) return;

    /* Ждём, пока предыдущая посылка уйдёт.
       На старте esp_tx_busy = 0, так что первая уйдёт сразу. */
    uint32_t t = HAL_GetTick();
    while (esp_tx_busy && (HAL_GetTick() - t < 100)) { /* wait */ }

    memcpy(esp_tx_buf, buf, len);

    esp_tx_busy = 1;
    if (HAL_UART_Transmit_DMA(&huart2, esp_tx_buf, len) != HAL_OK) {
        esp_tx_busy = 0;
    }
}

void esp_send_line(const char *str)
{
    if (str == NULL) return;

    uint16_t len = strlen(str);
    if (len > ESP_TX_BUF_SIZE - 1) len = ESP_TX_BUF_SIZE - 1;

    memcpy(esp_tx_buf, str, len);
    esp_tx_buf[len] = '\n';
    len++;

    uint32_t t = HAL_GetTick();
    while (esp_tx_busy && (HAL_GetTick() - t < 100)) { /* wait */ }

    esp_tx_busy = 1;
    if (HAL_UART_Transmit_DMA(&huart2, esp_tx_buf, len) != HAL_OK) {
        esp_tx_busy = 0;
    }
}

static void esp_on_rx_byte(uint8_t byte)
{
    /* Игнорируем CR */
    if (byte == '\r') return;

    /* Конец строки — команда готова */
    if (byte == '\n') {
        esp_rx_line[esp_rx_len] = '\0';   /* терминатор */
        esp_on_command(esp_rx_line);      /* диспетчер в esp_commands.c */
        esp_rx_len = 0;
        return;
    }

    /* Обычный байт — копим */
    if (esp_rx_len < ESP_RX_LINE_MAX - 1) {
        esp_rx_line[esp_rx_len++] = (char)byte;
    } else {
        /* Переполнение — сбрасываем */
        esp_rx_len = 0;
    }
}

void esp_link_on_rx_byte(void)
{
    esp_on_rx_byte(esp_rx_byte);
    HAL_UART_Receive_IT(&huart2, &esp_rx_byte, 1);
}

void esp_link_on_tx_done(void)
{
    esp_tx_busy = 0;
}

void esp_link_on_error(void)
{
    esp_rx_len = 0;
    HAL_UART_Receive_IT(&huart2, &esp_rx_byte, 1);
}
