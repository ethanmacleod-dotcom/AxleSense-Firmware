#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle);
void App_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
