#ifndef __USB_CDC_H
#define __USB_CDC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void usb_cdc_send(const uint8_t *data, uint16_t len);
void usb_cdc_on_rx(const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USB_CDC_H */
