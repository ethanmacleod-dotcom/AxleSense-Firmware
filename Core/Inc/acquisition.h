#ifndef ACQUISITION_H
#define ACQUISITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "node_config.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    uint32_t sequence;
    uint64_t timestamp_us;
    uint8_t enabled_channel_mask;
    uint32_t raw[NODE_CONFIG_CHANNEL_COUNT];
    uint32_t sample_offset_us[NODE_CONFIG_CHANNEL_COUNT];
} AcquisitionFrame_t;

extern volatile int32_t adc_runtime_config_status;
extern volatile int32_t adc_reset_status;
extern volatile int32_t adc_config_status;

extern volatile uint32_t rb_magic;
extern volatile uint32_t rb_seq[6];
extern volatile int32_t rb_st_seq[6];
extern volatile uint8_t rb_all_reads_ok;

extern volatile uint32_t adc_read_errors;
extern volatile uint32_t adc_samples_total;
extern volatile uint32_t adc_last_raw;
extern volatile uint32_t adc_last_status;
extern volatile uint8_t adc_last_channel;

extern volatile uint8_t noise_done;
extern volatile uint32_t noise_window_samples;
extern volatile uint32_t noise_raw_min;
extern volatile uint32_t noise_raw_max;
extern volatile double noise_mean_code;
extern volatile double noise_std_code;
extern volatile double noise_pp_code;
extern volatile double noise_mean_uV;
extern volatile double noise_std_uV;
extern volatile double noise_pp_uV;

void Acquisition_Init(
    SPI_HandleTypeDef *spi,
    const NodeConfig_t *node_config);
void Acquisition_Process(void);
uint8_t Acquisition_IsComplete(void);
uint8_t Acquisition_GetLatestFrame(
    AcquisitionFrame_t *frame,
    uint32_t *last_sequence);
uint8_t Acquisition_GetLatestSequence(uint32_t *sequence);
uint32_t Acquisition_GetReadErrorCount(void);
uint8_t Acquisition_GetEnabledChannelMask(void);

#ifdef __cplusplus
}
#endif

#endif /* ACQUISITION_H */
