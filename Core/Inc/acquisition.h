#ifndef ACQUISITION_H
#define ACQUISITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "node_config.h"
#include "stm32f4xx_hal.h"

#define ACQUISITION_SEQUENCE_FIRST  1UL
#define ACQUISITION_SEQUENCE_MAX    0xFFFFFFFEUL
#define ACQUISITION_SEQUENCE_NONE   0xFFFFFFFFUL

typedef struct
{
    uint32_t sequence;
    uint64_t timestamp_us;
    uint8_t enabled_channel_mask;
    uint32_t raw[NODE_CONFIG_CHANNEL_COUNT];
    uint32_t sample_offset_us[NODE_CONFIG_CHANNEL_COUNT];
} AcquisitionFrame_t;

void Acquisition_Init(
    SPI_HandleTypeDef *spi,
    const NodeConfig_t *node_config);
void Acquisition_Process(void);
uint8_t Acquisition_IsReady(void);
uint32_t Acquisition_SequenceNext(uint32_t sequence);
uint8_t Acquisition_GetLatestSequence(uint32_t *sequence);
uint32_t Acquisition_GetReadErrorCount(void);
uint8_t Acquisition_GetEnabledChannelMask(void);

#ifdef __cplusplus
}
#endif

#endif /* ACQUISITION_H */
