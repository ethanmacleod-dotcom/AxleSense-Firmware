#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define NODE_CONFIG_V1_PAYLOAD_SIZE                 256U
#define NODE_CONFIG_PRODUCT_ID_AXLESENSE            1UL
#define NODE_CONFIG_ADC_MAPPING_VERSION             1U
#define NODE_CONFIG_ANALOG_REFERENCE_VERSION        1U
#define NODE_CONFIG_CHANNEL_COUNT                   4U
#define NODE_CONFIG_ACCEL_AXIS_COUNT                3U
#define NODE_CONFIG_DEFAULT_UART_BAUD_RATE           115200UL
#define NODE_CONFIG_DEFAULT_ENABLED_CHANNEL_MASK    0x08U
#define NODE_CONFIG_UNKNOWN_TEMPERATURE_MC          INT32_MIN

#define NODE_CONFIG_IDENTITY_FLAG_PROVISIONED       (1U << 0)
#define NODE_CONFIG_IDENTITY_FLAG_MANUFACTURING_OK  (1U << 1)

#define NODE_CONFIG_STRAIN_CAL_FLAG_VALID           (1U << 0)
#define NODE_CONFIG_STRAIN_CAL_FLAG_TEMPERATURE     (1U << 1)

#define NODE_CONFIG_ACCEL_CAL_FLAG_TEMPERATURE      (1U << 0)
#define NODE_CONFIG_ACCEL_CAL_FLAG_FIXTURE          (1U << 1)

#define NODE_CONFIG_ACCEL_VALID_X                   (1U << 0)
#define NODE_CONFIG_ACCEL_VALID_Y                   (1U << 1)
#define NODE_CONFIG_ACCEL_VALID_Z                   (1U << 2)

#define NODE_CONFIG_FILTER_OPTION_SINGLE_CYCLE      (1U << 0)
#define NODE_CONFIG_FILTER_OPTION_60HZ_REJECTION    (1U << 1)

#define NODE_CONFIG_QA_ELECTRICAL                   (1UL << 0)
#define NODE_CONFIG_QA_ADC_ANALOG                   (1UL << 1)
#define NODE_CONFIG_QA_RS485                        (1UL << 2)
#define NODE_CONFIG_QA_STRAIN_CAL                   (1UL << 3)
#define NODE_CONFIG_QA_ACCEL_X_CAL                  (1UL << 4)
#define NODE_CONFIG_QA_ACCEL_Y_CAL                  (1UL << 5)
#define NODE_CONFIG_QA_ACCEL_Z_CAL                  (1UL << 6)
#define NODE_CONFIG_QA_FINAL                        (1UL << 7)

typedef enum
{
    NODE_CONFIG_CHANNEL_ACCEL_X = 0,
    NODE_CONFIG_CHANNEL_ACCEL_Y = 1,
    NODE_CONFIG_CHANNEL_ACCEL_Z = 2,
    NODE_CONFIG_CHANNEL_STRAIN = 3
} NodeConfig_Channel_t;

typedef enum
{
    NODE_CONFIG_FILTER_DEFAULT = 0,
    NODE_CONFIG_FILTER_SINC4 = 1,
    NODE_CONFIG_FILTER_SINC3 = 2,
    NODE_CONFIG_FILTER_FAST_SETTLING = 3,
    NODE_CONFIG_FILTER_50_60_HZ_REJECTION = 4
} NodeConfig_FilterMode_t;

typedef struct
{
    uint16_t model;
    uint16_t flags;
    uint32_t zero_code;
    int32_t scale_pstrain_per_code;
    int64_t offset_pstrain;
    int32_t calibration_temperature_mC;
    uint32_t bound_product_id;
    uint16_t bound_hardware_revision_major;
    uint16_t bound_hardware_revision_minor;
    uint8_t bound_mapping_version;
    uint8_t bound_analog_reference_version;
    uint8_t bound_gain;
    uint8_t calibration_filter_mode;
    uint32_t calibration_rate_millihz;
} NodeConfig_StrainCalibration_t;

typedef struct
{
    uint16_t model;
    uint8_t valid_axis_mask;
    uint8_t flags;
    uint32_t zero_code[NODE_CONFIG_ACCEL_AXIS_COUNT];
    int32_t scale_ng_per_code[NODE_CONFIG_ACCEL_AXIS_COUNT];
    int32_t calibration_temperature_mC;
    uint32_t bound_product_id;
    uint16_t bound_hardware_revision_major;
    uint16_t bound_hardware_revision_minor;
    uint8_t bound_mapping_version;
    uint8_t bound_analog_reference_version;
    uint8_t bound_gain[NODE_CONFIG_ACCEL_AXIS_COUNT];
    uint8_t calibration_filter_mode[NODE_CONFIG_ACCEL_AXIS_COUNT];
    uint32_t calibration_rate_millihz[NODE_CONFIG_ACCEL_AXIS_COUNT];
    uint8_t calibration_filter_options[NODE_CONFIG_ACCEL_AXIS_COUNT];
} NodeConfig_AccelCalibration_t;

typedef struct
{
    uint64_t node_serial_number;
    uint64_t pcb_serial_number;
    uint32_t product_id;
    uint16_t hardware_revision_major;
    uint16_t hardware_revision_minor;
    uint16_t assembly_variant;
    uint16_t identity_flags;

    uint16_t node_address;
    uint32_t uart_baud_rate;

    uint8_t enabled_channel_mask;
    uint8_t adc_power_mode;
    uint8_t acquisition_mode;
    uint8_t adc_mapping_version;
    uint8_t channel_gain[NODE_CONFIG_CHANNEL_COUNT];
    uint8_t filter_mode[NODE_CONFIG_CHANNEL_COUNT];
    uint8_t filter_options[NODE_CONFIG_CHANNEL_COUNT];
    uint32_t output_data_rate_millihz[NODE_CONFIG_CHANNEL_COUNT];
    uint16_t startup_settling_conversions;
    uint8_t analog_reference_config_version;

    NodeConfig_StrainCalibration_t strain_calibration;
    NodeConfig_AccelCalibration_t accel_calibration;

    uint64_t manufacturing_timestamp_utc_s;
    uint64_t calibration_timestamp_utc_s;
    uint32_t manufacturing_station_id;
    uint32_t calibration_station_id;
    uint32_t manufacturing_lot_id;
    uint32_t calibration_run_id;
    uint32_t qa_flags;
} NodeConfig_t;

void NodeConfig_SetDefaults(NodeConfig_t *config);
bool NodeConfig_EncodeV1(const NodeConfig_t *config,
                         uint8_t payload[NODE_CONFIG_V1_PAYLOAD_SIZE]);
bool NodeConfig_DecodeV1(const uint8_t payload[NODE_CONFIG_V1_PAYLOAD_SIZE],
                         NodeConfig_t *config);
bool NodeConfig_Validate(const NodeConfig_t *config);

#ifdef __cplusplus
}
#endif

#endif /* NODE_CONFIG_H */
