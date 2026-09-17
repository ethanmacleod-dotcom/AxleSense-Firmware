#include "node_config.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define NODE_CONFIG_IDENTITY_FLAG_MASK \
    (NODE_CONFIG_IDENTITY_FLAG_PROVISIONED | \
     NODE_CONFIG_IDENTITY_FLAG_MANUFACTURING_OK)

#define NODE_CONFIG_STRAIN_CAL_FLAG_MASK \
    (NODE_CONFIG_STRAIN_CAL_FLAG_VALID | \
     NODE_CONFIG_STRAIN_CAL_FLAG_TEMPERATURE)

#define NODE_CONFIG_ACCEL_CAL_FLAG_MASK \
    (NODE_CONFIG_ACCEL_CAL_FLAG_TEMPERATURE | \
     NODE_CONFIG_ACCEL_CAL_FLAG_FIXTURE)

#define NODE_CONFIG_ACCEL_VALID_MASK \
    (NODE_CONFIG_ACCEL_VALID_X | NODE_CONFIG_ACCEL_VALID_Y | \
     NODE_CONFIG_ACCEL_VALID_Z)

#define NODE_CONFIG_FILTER_OPTION_MASK \
    (NODE_CONFIG_FILTER_OPTION_SINGLE_CYCLE | \
     NODE_CONFIG_FILTER_OPTION_60HZ_REJECTION)

#define NODE_CONFIG_QA_FLAG_MASK \
    (NODE_CONFIG_QA_ELECTRICAL | NODE_CONFIG_QA_ADC_ANALOG | \
     NODE_CONFIG_QA_RS485 | NODE_CONFIG_QA_STRAIN_CAL | \
     NODE_CONFIG_QA_ACCEL_X_CAL | NODE_CONFIG_QA_ACCEL_Y_CAL | \
     NODE_CONFIG_QA_ACCEL_Z_CAL | NODE_CONFIG_QA_FINAL)

#define NODE_CONFIG_ADC_MAX_CODE                    0x00FFFFFFUL
#define NODE_CONFIG_MAX_SETTLING_CONVERSIONS        4096U
#define NODE_CONFIG_STRAIN_CALIBRATION_MODEL        1U
#define NODE_CONFIG_ACCEL_CALIBRATION_MODEL         1U

static void WriteU16LE(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void WriteU32LE(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void WriteU64LE(uint8_t *destination, uint64_t value)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        destination[i] = (uint8_t)(value >> (8U * i));
    }
}

static void WriteI32LE(uint8_t *destination, int32_t value)
{
    WriteU32LE(destination, (uint32_t)value);
}

static void WriteI64LE(uint8_t *destination, int64_t value)
{
    WriteU64LE(destination, (uint64_t)value);
}

static uint16_t ReadU16LE(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t ReadU32LE(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t ReadU64LE(const uint8_t *source)
{
    uint64_t value = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        value |= ((uint64_t)source[i] << (8U * i));
    }

    return value;
}

static int32_t ReadI32LE(const uint8_t *source)
{
    const uint32_t value = ReadU32LE(source);

    if (value <= (uint32_t)INT32_MAX)
    {
        return (int32_t)value;
    }

    if (value == 0x80000000UL)
    {
        return INT32_MIN;
    }

    return -(int32_t)((~value) + 1U);
}

static int64_t ReadI64LE(const uint8_t *source)
{
    const uint64_t value = ReadU64LE(source);

    if (value <= (uint64_t)INT64_MAX)
    {
        return (int64_t)value;
    }

    if (value == UINT64_C(0x8000000000000000))
    {
        return INT64_MIN;
    }

    return -(int64_t)((~value) + 1U);
}

static bool IsSupportedBaudRate(uint32_t baud_rate)
{
    static const uint32_t supported_baud_rates[] = {
        9600UL,
        19200UL,
        38400UL,
        57600UL,
        115200UL,
        230400UL,
        460800UL,
        921600UL
    };

    for (size_t i = 0U;
         i < (sizeof(supported_baud_rates) / sizeof(supported_baud_rates[0]));
         i++)
    {
        if (baud_rate == supported_baud_rates[i])
        {
            return true;
        }
    }

    return false;
}

static bool IsSupportedGain(uint8_t gain)
{
    switch (gain)
    {
        case 1U:
        case 2U:
        case 4U:
        case 8U:
        case 16U:
        case 32U:
        case 64U:
        case 128U:
            return true;

        default:
            return false;
    }
}

static bool IsSupportedFilterMode(uint8_t filter_mode)
{
    return filter_mode <= (uint8_t)NODE_CONFIG_FILTER_50_60_HZ_REJECTION;
}

static bool TemperatureFlagMatches(int32_t temperature_mC,
                                   bool temperature_recorded)
{
    return temperature_recorded ==
           (temperature_mC != NODE_CONFIG_UNKNOWN_TEMPERATURE_MC);
}

void NodeConfig_SetDefaults(NodeConfig_t *config)
{
    if (config == NULL)
    {
        return;
    }

    (void)memset(config, 0, sizeof(*config));

    config->product_id = NODE_CONFIG_PRODUCT_ID_AXLESENSE;
    config->uart_baud_rate = NODE_CONFIG_DEFAULT_UART_BAUD_RATE;

    config->enabled_channel_mask = NODE_CONFIG_DEFAULT_ENABLED_CHANNEL_MASK;
    config->adc_power_mode = 2U;
    config->acquisition_mode = 0U;
    config->adc_mapping_version = NODE_CONFIG_ADC_MAPPING_VERSION;
    config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_X] = 1U;
    config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_Y] = 1U;
    config->channel_gain[NODE_CONFIG_CHANNEL_ACCEL_Z] = 1U;
    config->channel_gain[NODE_CONFIG_CHANNEL_STRAIN] = 16U;
    config->startup_settling_conversions = 256U;
    config->analog_reference_config_version =
        NODE_CONFIG_ANALOG_REFERENCE_VERSION;

    config->strain_calibration.model =
        NODE_CONFIG_STRAIN_CALIBRATION_MODEL;
    config->strain_calibration.calibration_temperature_mC =
        NODE_CONFIG_UNKNOWN_TEMPERATURE_MC;

    config->accel_calibration.model =
        NODE_CONFIG_ACCEL_CALIBRATION_MODEL;
    config->accel_calibration.calibration_temperature_mC =
        NODE_CONFIG_UNKNOWN_TEMPERATURE_MC;
}

bool NodeConfig_Validate(const NodeConfig_t *config)
{
    if (config == NULL)
    {
        return false;
    }

    if (config->product_id != NODE_CONFIG_PRODUCT_ID_AXLESENSE)
    {
        return false;
    }
    if ((config->identity_flags & ~NODE_CONFIG_IDENTITY_FLAG_MASK) != 0U)
    {
        return false;
    }
    if (config->node_address > 254U)
    {
        return false;
    }
    if (!IsSupportedBaudRate(config->uart_baud_rate))
    {
        return false;
    }
    if ((config->enabled_channel_mask & 0xF0U) != 0U)
    {
        return false;
    }
    if (config->adc_power_mode > 2U)
    {
        return false;
    }
    if (config->acquisition_mode != 0U)
    {
        return false;
    }
    if (config->adc_mapping_version != NODE_CONFIG_ADC_MAPPING_VERSION)
    {
        return false;
    }
    if (config->startup_settling_conversions >
        NODE_CONFIG_MAX_SETTLING_CONVERSIONS)
    {
        return false;
    }
    if (config->analog_reference_config_version !=
        NODE_CONFIG_ANALOG_REFERENCE_VERSION)
    {
        return false;
    }

    for (uint8_t i = 0U; i < NODE_CONFIG_CHANNEL_COUNT; i++)
    {
        if (!IsSupportedGain(config->channel_gain[i]))
        {
            return false;
        }
        if (!IsSupportedFilterMode(config->filter_mode[i]))
        {
            return false;
        }
        if ((config->filter_options[i] & ~NODE_CONFIG_FILTER_OPTION_MASK) != 0U)
        {
            return false;
        }
    }

    if (config->strain_calibration.model !=
        NODE_CONFIG_STRAIN_CALIBRATION_MODEL)
    {
        return false;
    }
    if ((config->strain_calibration.flags &
         ~NODE_CONFIG_STRAIN_CAL_FLAG_MASK) != 0U)
    {
        return false;
    }
    if (config->strain_calibration.zero_code > NODE_CONFIG_ADC_MAX_CODE)
    {
        return false;
    }
    if (!TemperatureFlagMatches(
            config->strain_calibration.calibration_temperature_mC,
            (config->strain_calibration.flags &
             NODE_CONFIG_STRAIN_CAL_FLAG_TEMPERATURE) != 0U))
    {
        return false;
    }
    if (!IsSupportedFilterMode(
            config->strain_calibration.calibration_filter_mode))
    {
        return false;
    }
    if ((config->strain_calibration.bound_gain != 0U) &&
        !IsSupportedGain(config->strain_calibration.bound_gain))
    {
        return false;
    }

    if ((config->strain_calibration.flags &
         NODE_CONFIG_STRAIN_CAL_FLAG_VALID) != 0U)
    {
        if ((config->strain_calibration.scale_pstrain_per_code == 0) ||
            (config->strain_calibration.bound_product_id == 0U) ||
            (config->strain_calibration.bound_mapping_version == 0U) ||
            (config->strain_calibration.bound_analog_reference_version == 0U) ||
            !IsSupportedGain(config->strain_calibration.bound_gain))
        {
            return false;
        }
    }

    if (config->accel_calibration.model !=
        NODE_CONFIG_ACCEL_CALIBRATION_MODEL)
    {
        return false;
    }
    if ((config->accel_calibration.valid_axis_mask &
         ~NODE_CONFIG_ACCEL_VALID_MASK) != 0U)
    {
        return false;
    }
    if ((config->accel_calibration.flags &
         ~NODE_CONFIG_ACCEL_CAL_FLAG_MASK) != 0U)
    {
        return false;
    }
    if (!TemperatureFlagMatches(
            config->accel_calibration.calibration_temperature_mC,
            (config->accel_calibration.flags &
             NODE_CONFIG_ACCEL_CAL_FLAG_TEMPERATURE) != 0U))
    {
        return false;
    }

    for (uint8_t i = 0U; i < NODE_CONFIG_ACCEL_AXIS_COUNT; i++)
    {
        const uint8_t axis_bit = (uint8_t)(1U << i);

        if (config->accel_calibration.zero_code[i] >
            NODE_CONFIG_ADC_MAX_CODE)
        {
            return false;
        }
        if (!IsSupportedFilterMode(
                config->accel_calibration.calibration_filter_mode[i]))
        {
            return false;
        }
        if ((config->accel_calibration.calibration_filter_options[i] &
             ~NODE_CONFIG_FILTER_OPTION_MASK) != 0U)
        {
            return false;
        }
        if ((config->accel_calibration.bound_gain[i] != 0U) &&
            !IsSupportedGain(config->accel_calibration.bound_gain[i]))
        {
            return false;
        }

        if ((config->accel_calibration.valid_axis_mask & axis_bit) != 0U)
        {
            if ((config->accel_calibration.scale_ng_per_code[i] == 0) ||
                (config->accel_calibration.bound_product_id == 0U) ||
                (config->accel_calibration.bound_mapping_version == 0U) ||
                (config->accel_calibration.bound_analog_reference_version ==
                 0U) ||
                !IsSupportedGain(config->accel_calibration.bound_gain[i]))
            {
                return false;
            }
        }
    }

    if ((config->qa_flags & ~NODE_CONFIG_QA_FLAG_MASK) != 0U)
    {
        return false;
    }

    return true;
}

bool NodeConfig_EncodeV1(const NodeConfig_t *config,
                         uint8_t payload[NODE_CONFIG_V1_PAYLOAD_SIZE])
{
    if ((payload == NULL) || !NodeConfig_Validate(config))
    {
        return false;
    }

    (void)memset(payload, 0, NODE_CONFIG_V1_PAYLOAD_SIZE);

    WriteU64LE(&payload[0x000], config->node_serial_number);
    WriteU64LE(&payload[0x008], config->pcb_serial_number);
    WriteU32LE(&payload[0x010], config->product_id);
    WriteU16LE(&payload[0x014], config->hardware_revision_major);
    WriteU16LE(&payload[0x016], config->hardware_revision_minor);
    WriteU16LE(&payload[0x018], config->assembly_variant);
    WriteU16LE(&payload[0x01A], config->identity_flags);

    WriteU16LE(&payload[0x020], config->node_address);
    WriteU32LE(&payload[0x024], config->uart_baud_rate);

    payload[0x030] = config->enabled_channel_mask;
    payload[0x031] = config->adc_power_mode;
    payload[0x032] = config->acquisition_mode;
    payload[0x033] = config->adc_mapping_version;
    for (uint8_t i = 0U; i < NODE_CONFIG_CHANNEL_COUNT; i++)
    {
        payload[0x034U + i] = config->channel_gain[i];
        payload[0x038U + i] = config->filter_mode[i];
        payload[0x03CU + i] = config->filter_options[i];
        WriteU32LE(&payload[0x040U + (4U * i)],
                   config->output_data_rate_millihz[i]);
    }
    WriteU16LE(&payload[0x050], config->startup_settling_conversions);
    payload[0x052] = config->analog_reference_config_version;

    WriteU16LE(&payload[0x060], config->strain_calibration.model);
    WriteU16LE(&payload[0x062], config->strain_calibration.flags);
    WriteU32LE(&payload[0x064], config->strain_calibration.zero_code);
    WriteI32LE(&payload[0x068],
               config->strain_calibration.scale_pstrain_per_code);
    WriteI64LE(&payload[0x06C], config->strain_calibration.offset_pstrain);
    WriteI32LE(&payload[0x074],
               config->strain_calibration.calibration_temperature_mC);
    WriteU32LE(&payload[0x078],
               config->strain_calibration.bound_product_id);
    WriteU16LE(&payload[0x07C],
               config->strain_calibration.bound_hardware_revision_major);
    WriteU16LE(&payload[0x07E],
               config->strain_calibration.bound_hardware_revision_minor);
    payload[0x080] = config->strain_calibration.bound_mapping_version;
    payload[0x081] =
        config->strain_calibration.bound_analog_reference_version;
    payload[0x082] = config->strain_calibration.bound_gain;
    payload[0x083] = config->strain_calibration.calibration_filter_mode;
    WriteU32LE(&payload[0x084],
               config->strain_calibration.calibration_rate_millihz);

    WriteU16LE(&payload[0x088], config->accel_calibration.model);
    payload[0x08A] = config->accel_calibration.valid_axis_mask;
    payload[0x08B] = config->accel_calibration.flags;
    for (uint8_t i = 0U; i < NODE_CONFIG_ACCEL_AXIS_COUNT; i++)
    {
        WriteU32LE(&payload[0x08CU + (4U * i)],
                   config->accel_calibration.zero_code[i]);
        WriteI32LE(&payload[0x098U + (4U * i)],
                   config->accel_calibration.scale_ng_per_code[i]);
    }
    WriteI32LE(&payload[0x0A4],
               config->accel_calibration.calibration_temperature_mC);
    WriteU32LE(&payload[0x0A8],
               config->accel_calibration.bound_product_id);
    WriteU16LE(&payload[0x0AC],
               config->accel_calibration.bound_hardware_revision_major);
    WriteU16LE(&payload[0x0AE],
               config->accel_calibration.bound_hardware_revision_minor);
    payload[0x0B0] = config->accel_calibration.bound_mapping_version;
    payload[0x0B1] =
        config->accel_calibration.bound_analog_reference_version;
    for (uint8_t i = 0U; i < NODE_CONFIG_ACCEL_AXIS_COUNT; i++)
    {
        payload[0x0B2U + i] = config->accel_calibration.bound_gain[i];
        payload[0x0B5U + i] =
            config->accel_calibration.calibration_filter_mode[i];
        WriteU32LE(&payload[0x0B8U + (4U * i)],
                   config->accel_calibration.calibration_rate_millihz[i]);
        payload[0x0C4U + i] =
            config->accel_calibration.calibration_filter_options[i];
    }

    WriteU64LE(&payload[0x0D0], config->manufacturing_timestamp_utc_s);
    WriteU64LE(&payload[0x0D8], config->calibration_timestamp_utc_s);
    WriteU32LE(&payload[0x0E0], config->manufacturing_station_id);
    WriteU32LE(&payload[0x0E4], config->calibration_station_id);
    WriteU32LE(&payload[0x0E8], config->manufacturing_lot_id);
    WriteU32LE(&payload[0x0EC], config->calibration_run_id);
    WriteU32LE(&payload[0x0F0], config->qa_flags);

    return true;
}

bool NodeConfig_DecodeV1(const uint8_t payload[NODE_CONFIG_V1_PAYLOAD_SIZE],
                         NodeConfig_t *config)
{
    NodeConfig_t decoded;

    if ((payload == NULL) || (config == NULL))
    {
        return false;
    }

    (void)memset(&decoded, 0, sizeof(decoded));

    decoded.node_serial_number = ReadU64LE(&payload[0x000]);
    decoded.pcb_serial_number = ReadU64LE(&payload[0x008]);
    decoded.product_id = ReadU32LE(&payload[0x010]);
    decoded.hardware_revision_major = ReadU16LE(&payload[0x014]);
    decoded.hardware_revision_minor = ReadU16LE(&payload[0x016]);
    decoded.assembly_variant = ReadU16LE(&payload[0x018]);
    decoded.identity_flags = ReadU16LE(&payload[0x01A]);

    decoded.node_address = ReadU16LE(&payload[0x020]);
    decoded.uart_baud_rate = ReadU32LE(&payload[0x024]);

    decoded.enabled_channel_mask = payload[0x030];
    decoded.adc_power_mode = payload[0x031];
    decoded.acquisition_mode = payload[0x032];
    decoded.adc_mapping_version = payload[0x033];
    for (uint8_t i = 0U; i < NODE_CONFIG_CHANNEL_COUNT; i++)
    {
        decoded.channel_gain[i] = payload[0x034U + i];
        decoded.filter_mode[i] = payload[0x038U + i];
        decoded.filter_options[i] = payload[0x03CU + i];
        decoded.output_data_rate_millihz[i] =
            ReadU32LE(&payload[0x040U + (4U * i)]);
    }
    decoded.startup_settling_conversions = ReadU16LE(&payload[0x050]);
    decoded.analog_reference_config_version = payload[0x052];

    decoded.strain_calibration.model = ReadU16LE(&payload[0x060]);
    decoded.strain_calibration.flags = ReadU16LE(&payload[0x062]);
    decoded.strain_calibration.zero_code = ReadU32LE(&payload[0x064]);
    decoded.strain_calibration.scale_pstrain_per_code =
        ReadI32LE(&payload[0x068]);
    decoded.strain_calibration.offset_pstrain = ReadI64LE(&payload[0x06C]);
    decoded.strain_calibration.calibration_temperature_mC =
        ReadI32LE(&payload[0x074]);
    decoded.strain_calibration.bound_product_id = ReadU32LE(&payload[0x078]);
    decoded.strain_calibration.bound_hardware_revision_major =
        ReadU16LE(&payload[0x07C]);
    decoded.strain_calibration.bound_hardware_revision_minor =
        ReadU16LE(&payload[0x07E]);
    decoded.strain_calibration.bound_mapping_version = payload[0x080];
    decoded.strain_calibration.bound_analog_reference_version = payload[0x081];
    decoded.strain_calibration.bound_gain = payload[0x082];
    decoded.strain_calibration.calibration_filter_mode = payload[0x083];
    decoded.strain_calibration.calibration_rate_millihz =
        ReadU32LE(&payload[0x084]);

    decoded.accel_calibration.model = ReadU16LE(&payload[0x088]);
    decoded.accel_calibration.valid_axis_mask = payload[0x08A];
    decoded.accel_calibration.flags = payload[0x08B];
    for (uint8_t i = 0U; i < NODE_CONFIG_ACCEL_AXIS_COUNT; i++)
    {
        decoded.accel_calibration.zero_code[i] =
            ReadU32LE(&payload[0x08CU + (4U * i)]);
        decoded.accel_calibration.scale_ng_per_code[i] =
            ReadI32LE(&payload[0x098U + (4U * i)]);
    }
    decoded.accel_calibration.calibration_temperature_mC =
        ReadI32LE(&payload[0x0A4]);
    decoded.accel_calibration.bound_product_id = ReadU32LE(&payload[0x0A8]);
    decoded.accel_calibration.bound_hardware_revision_major =
        ReadU16LE(&payload[0x0AC]);
    decoded.accel_calibration.bound_hardware_revision_minor =
        ReadU16LE(&payload[0x0AE]);
    decoded.accel_calibration.bound_mapping_version = payload[0x0B0];
    decoded.accel_calibration.bound_analog_reference_version = payload[0x0B1];
    for (uint8_t i = 0U; i < NODE_CONFIG_ACCEL_AXIS_COUNT; i++)
    {
        decoded.accel_calibration.bound_gain[i] = payload[0x0B2U + i];
        decoded.accel_calibration.calibration_filter_mode[i] =
            payload[0x0B5U + i];
        decoded.accel_calibration.calibration_rate_millihz[i] =
            ReadU32LE(&payload[0x0B8U + (4U * i)]);
        decoded.accel_calibration.calibration_filter_options[i] =
            payload[0x0C4U + i];
    }

    decoded.manufacturing_timestamp_utc_s = ReadU64LE(&payload[0x0D0]);
    decoded.calibration_timestamp_utc_s = ReadU64LE(&payload[0x0D8]);
    decoded.manufacturing_station_id = ReadU32LE(&payload[0x0E0]);
    decoded.calibration_station_id = ReadU32LE(&payload[0x0E4]);
    decoded.manufacturing_lot_id = ReadU32LE(&payload[0x0E8]);
    decoded.calibration_run_id = ReadU32LE(&payload[0x0EC]);
    decoded.qa_flags = ReadU32LE(&payload[0x0F0]);

    if (!NodeConfig_Validate(&decoded))
    {
        return false;
    }

    *config = decoded;
    return true;
}
