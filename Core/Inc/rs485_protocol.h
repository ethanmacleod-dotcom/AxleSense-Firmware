#ifndef RS485_PROTOCOL_H
#define RS485_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "node_config.h"
#include "stm32f4xx_hal.h"

#define RS485_PROTOCOL_FAULT_CONFIG_STORAGE  (1UL << 0)
#define RS485_PROTOCOL_FAULT_ADC_INIT        (1UL << 1)

void Rs485Protocol_Init(
    UART_HandleTypeDef *uart,
    const NodeConfig_t *node_config,
    uint64_t config_generation,
    uint32_t initial_fault_flags);
void Rs485Protocol_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* RS485_PROTOCOL_H */
