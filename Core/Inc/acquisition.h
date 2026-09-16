#ifndef ACQUISITION_H
#define ACQUISITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

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

void Acquisition_Init(SPI_HandleTypeDef *spi);
void Acquisition_Process(void);
uint8_t Acquisition_IsComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* ACQUISITION_H */
