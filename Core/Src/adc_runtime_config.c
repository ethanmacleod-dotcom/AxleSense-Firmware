#include "adc_runtime_config.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * V1 PCB mapping:
 *   logical CH0 / accel X = AIN2 to AVSS, Setup0
 *   logical CH1 / accel Y = AIN3 to AVSS, Setup0
 *   logical CH2 / accel Z = AIN0 to AVSS, Setup0
 *   logical CH3 / strain  = AIN4 to AIN5, Setup1
 * AIN1 is unused.
 */
#define ADC_RUNTIME_CONFIG_ACCEL_X_AIN2_AVSS_WORD  0x8051U
#define ADC_RUNTIME_CONFIG_ACCEL_Y_AIN3_AVSS_WORD  0x8071U
#define ADC_RUNTIME_CONFIG_ACCEL_Z_AIN0_AVSS_WORD  0x8011U
#define ADC_RUNTIME_CONFIG_STRAIN_AIN4_AIN5_WORD   0x9085U
#define ADC_RUNTIME_CONFIG_CHANNEL_ENABLE_BIT      0x8000U

extern AD7124_RegisterTypeDef configA;

static bool HasSupportedProfile(const NodeConfig_t *node_config)
{
    if ((node_config->adc_power_mode != 2U) ||
        (node_config->acquisition_mode != 0U) ||
        (node_config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_X] != 1U) ||
        (node_config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_Y] != 1U) ||
        (node_config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_Z] != 1U) ||
        (node_config->channel_gain[NODE_CONFIG_CHANNEL_STRAIN] != 64U))
    {
        return false;
    }

    for (uint8_t channel = 0U;
         channel < NODE_CONFIG_CHANNEL_COUNT;
         channel++)
    {
        if ((node_config->filter_mode[channel] !=
             NODE_CONFIG_FILTER_DEFAULT) ||
            (node_config->filter_options[channel] != 0U) ||
            (node_config->output_data_rate_millihz[channel] != 0U))
        {
            return false;
        }
    }

    return true;
}

AdcRuntimeConfig_Status_t AdcRuntimeConfig_Build(
    const NodeConfig_t *node_config,
    AD7124_RegisterTypeDef *adc_config)
{
    bool node_config_valid;

    if ((node_config == NULL) || (adc_config == NULL))
    {
        return ADC_RUNTIME_CONFIG_INVALID_ARGUMENT;
    }

    node_config_valid = NodeConfig_Validate(node_config);

    if (node_config->adc_mapping_version != NODE_CONFIG_ADC_MAPPING_VERSION)
    {
        return ADC_RUNTIME_CONFIG_UNSUPPORTED_MAPPING;
    }
    if (node_config->analog_reference_config_version !=
        NODE_CONFIG_ANALOG_REFERENCE_VERSION)
    {
        return ADC_RUNTIME_CONFIG_UNSUPPORTED_ANALOG_REFERENCE;
    }
    if (!node_config_valid)
    {
        return ADC_RUNTIME_CONFIG_INVALID_NODE_CONFIG;
    }
    if (!HasSupportedProfile(node_config))
    {
        return ADC_RUNTIME_CONFIG_UNSUPPORTED_PROFILE;
    }

    *adc_config = configA;

    adc_config->channels[NODE_CONFIG_CHANNEL_ACCEL_X] =
        ((node_config->enabled_channel_mask &
          (1U << NODE_CONFIG_CHANNEL_ACCEL_X)) != 0U) ?
        ADC_RUNTIME_CONFIG_ACCEL_X_AIN2_AVSS_WORD : 0x0000U;
    adc_config->channels[NODE_CONFIG_CHANNEL_ACCEL_Y] =
        ((node_config->enabled_channel_mask &
          (1U << NODE_CONFIG_CHANNEL_ACCEL_Y)) != 0U) ?
        ADC_RUNTIME_CONFIG_ACCEL_Y_AIN3_AVSS_WORD : 0x0000U;
    adc_config->channels[NODE_CONFIG_CHANNEL_ACCEL_Z] =
        ((node_config->enabled_channel_mask &
          (1U << NODE_CONFIG_CHANNEL_ACCEL_Z)) != 0U) ?
        ADC_RUNTIME_CONFIG_ACCEL_Z_AIN0_AVSS_WORD : 0x0000U;
    adc_config->channels[NODE_CONFIG_CHANNEL_STRAIN] =
        ((node_config->enabled_channel_mask &
          (1U << NODE_CONFIG_CHANNEL_STRAIN)) != 0U) ?
        ADC_RUNTIME_CONFIG_STRAIN_AIN4_AIN5_WORD :
        (uint16_t)(ADC_RUNTIME_CONFIG_STRAIN_AIN4_AIN5_WORD &
                   ~ADC_RUNTIME_CONFIG_CHANNEL_ENABLE_BIT);

    return ADC_RUNTIME_CONFIG_OK;
}
