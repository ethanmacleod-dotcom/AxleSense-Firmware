#ifndef ADC_RUNTIME_CONFIG_H
#define ADC_RUNTIME_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ad7124.h"
#include "node_config.h"

typedef enum
{
    ADC_RUNTIME_CONFIG_OK = 0,
    ADC_RUNTIME_CONFIG_INVALID_ARGUMENT,
    ADC_RUNTIME_CONFIG_INVALID_NODE_CONFIG,
    ADC_RUNTIME_CONFIG_UNSUPPORTED_MAPPING,
    ADC_RUNTIME_CONFIG_UNSUPPORTED_ANALOG_REFERENCE,
    ADC_RUNTIME_CONFIG_UNSUPPORTED_PROFILE
} AdcRuntimeConfig_Status_t;

AdcRuntimeConfig_Status_t AdcRuntimeConfig_Build(
    const NodeConfig_t *node_config,
    AD7124_RegisterTypeDef *adc_config);

#ifdef __cplusplus
}
#endif

#endif /* ADC_RUNTIME_CONFIG_H */
