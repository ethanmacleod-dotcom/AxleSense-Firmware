#ifndef RS485_PROTOCOL_H
#define RS485_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "node_config.h"
#include "stm32f4xx_hal.h"

void Rs485Protocol_Init(
    UART_HandleTypeDef *uart,
    const NodeConfig_t *node_config);
void Rs485Protocol_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* RS485_PROTOCOL_H */
