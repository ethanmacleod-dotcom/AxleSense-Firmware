#include "acquisition.h"

#include "acquisition_buffer.h"
#include "ad7124.h"
#include "adc_runtime_config.h"
#include "main.h"
#include "node_time.h"

#include <stddef.h>

extern AD7124_ConfigTypeDef AD7124_Handler;
extern uint32_t AD7124_ChannelSamples[AD7124_ENABLED_CHANNELS];
extern volatile uint8_t AD7124_LastActiveChannel;

static AD7124_RegisterTypeDef runtime_adc_config;
static uint8_t runtime_enabled_channel_mask = 0U;
static uint32_t last_samples[NODE_CONFIG_CHANNEL_COUNT] = {0U};
static uint8_t frame_seen_mask = 0U;
static uint64_t frame_start_time_us = 0U;
static uint32_t frame_sample_offset_us[NODE_CONFIG_CHANNEL_COUNT];
static uint8_t runtime_frame_boundary_channel = 0xFFU;
static uint16_t startup_settling_remaining = 0U;
static AcquisitionFrame_t latest_frame;
static uint8_t acquisition_ready = 0U;
static uint32_t adc_read_errors = 0U;

static void ResetFrameTimingState(void)
{
    frame_start_time_us = 0U;
    for (uint8_t channel = 0U;
         channel < NODE_CONFIG_CHANNEL_COUNT;
         channel++)
    {
        frame_sample_offset_us[channel] = 0xFFFFFFFFU;
    }
}

void Acquisition_Init(
    SPI_HandleTypeDef *spi,
    const NodeConfig_t *node_config)
{
    acquisition_ready = 0U;
    adc_read_errors = 0U;
    AcquisitionBuffer_Init();

    if ((spi == NULL) || (node_config == NULL))
    {
        return;
    }

    if (AdcRuntimeConfig_Build(
            node_config,
            &runtime_adc_config) != ADC_RUNTIME_CONFIG_OK)
    {
        return;
    }

    runtime_enabled_channel_mask = node_config->enabled_channel_mask;
    startup_settling_remaining = node_config->startup_settling_conversions;
    latest_frame = (AcquisitionFrame_t){0};
    frame_seen_mask = 0U;
    ResetFrameTimingState();

    for (uint8_t channel = 0U;
         channel < NODE_CONFIG_CHANNEL_COUNT;
         channel++)
    {
        last_samples[channel] = 0U;
    }

    runtime_frame_boundary_channel = 0xFFU;
    for (uint8_t channel = 0U;
         channel < NODE_CONFIG_CHANNEL_COUNT;
         channel++)
    {
        if ((runtime_enabled_channel_mask & (1U << channel)) != 0U)
        {
            runtime_frame_boundary_channel = channel;
        }
    }

    HAL_GPIO_WritePin(
        AD7124_CS_GPIO_Port,
        AD7124_CS_Pin,
        GPIO_PIN_SET);

    AD7124_Handler.SPIx = spi;
    AD7124_Handler.csPort = AD7124_CS_GPIO_Port;
    AD7124_Handler.csPin = AD7124_CS_Pin;

    if (AD7124_Reset(&AD7124_Handler, 100U) != AD7124_OK)
    {
        return;
    }

    if (AD7124_Config(
            &AD7124_Handler,
            &runtime_adc_config) != AD7124_OK)
    {
        return;
    }

    acquisition_ready = 1U;
}

void Acquisition_Process(void)
{
    uint32_t status_reg = 0U;

    if (acquisition_ready == 0U)
    {
        return;
    }

    if (AD7124_ReadRegister(
            &AD7124_Handler,
            AD7124_STATUS_REG,
            1U,
            &status_reg) != AD7124_OK)
    {
        adc_read_errors++;
        return;
    }

    /* STATUS.RDY is active low. */
    if ((status_reg & 0x80U) != 0U)
    {
        return;
    }

    if (AD7124_ReadSampleData(&AD7124_Handler) != AD7124_OK)
    {
        adc_read_errors++;
        return;
    }

    const uint8_t channel = AD7124_LastActiveChannel;
    if (channel >= NODE_CONFIG_CHANNEL_COUNT)
    {
        adc_read_errors++;
        return;
    }

    if ((runtime_enabled_channel_mask & (1U << channel)) == 0U)
    {
        adc_read_errors++;
        return;
    }

    /* Discard the configured number of accepted conversions after startup. */
    if (startup_settling_remaining > 0U)
    {
        startup_settling_remaining--;
        return;
    }

    const uint32_t raw = AD7124_ChannelSamples[channel];
    const uint64_t sample_time_us = NodeTime_GetUs();

    if (frame_seen_mask == 0U)
    {
        frame_start_time_us = sample_time_us;
        for (uint8_t i = 0U;
             i < NODE_CONFIG_CHANNEL_COUNT;
             i++)
        {
            frame_sample_offset_us[i] = 0xFFFFFFFFU;
        }
    }

    frame_sample_offset_us[channel] =
        (uint32_t)(sample_time_us - frame_start_time_us);
    last_samples[channel] = raw;
    frame_seen_mask |= (uint8_t)(1U << channel);

    if (channel == runtime_frame_boundary_channel)
    {
        if ((frame_seen_mask & runtime_enabled_channel_mask) ==
            runtime_enabled_channel_mask)
        {
            latest_frame.sequence =
                Acquisition_SequenceNext(latest_frame.sequence);
            latest_frame.timestamp_us = frame_start_time_us;
            latest_frame.enabled_channel_mask =
                runtime_enabled_channel_mask;

            for (uint8_t i = 0U;
                 i < NODE_CONFIG_CHANNEL_COUNT;
                 i++)
            {
                latest_frame.raw[i] = last_samples[i];
                latest_frame.sample_offset_us[i] =
                    frame_sample_offset_us[i];
            }

            AcquisitionBuffer_Push(&latest_frame);
        }

        frame_seen_mask = 0U;
        ResetFrameTimingState();
    }
}

uint8_t Acquisition_IsReady(void)
{
    return acquisition_ready;
}

uint32_t Acquisition_SequenceNext(uint32_t sequence)
{
    if ((sequence == 0U) ||
        (sequence >= ACQUISITION_SEQUENCE_MAX))
    {
        return ACQUISITION_SEQUENCE_FIRST;
    }

    return sequence + 1U;
}

uint8_t Acquisition_GetLatestSequence(uint32_t *sequence)
{
    if ((sequence == NULL) || (latest_frame.sequence == 0U))
    {
        return 0U;
    }

    *sequence = latest_frame.sequence;
    return 1U;
}

uint32_t Acquisition_GetReadErrorCount(void)
{
    return adc_read_errors;
}

uint8_t Acquisition_GetEnabledChannelMask(void)
{
    return runtime_enabled_channel_mask;
}
