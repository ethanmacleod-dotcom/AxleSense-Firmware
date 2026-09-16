#ifndef RS485_TEST_H
#define RS485_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

extern volatile uint32_t rs485_tx_count;
extern volatile int32_t rs485_last_status;

void RS485_TestProcess(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* RS485_TEST_H */
