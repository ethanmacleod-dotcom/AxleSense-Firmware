#include "acquisition_buffer.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(
    ACQUISITION_BUFFER_CAPACITY > 0U,
    "Acquisition buffer capacity must be nonzero");
_Static_assert(
    ACQUISITION_BUFFER_CAPACITY <= UINT16_MAX,
    "Acquisition buffer capacity must fit its index and count types");

static AcquisitionFrame_t frames[ACQUISITION_BUFFER_CAPACITY];
static uint16_t oldest_index;
static uint16_t frame_count;
static uint32_t overflow_count;

static uint16_t AdvanceIndex(uint16_t index)
{
    index++;
    if (index >= ACQUISITION_BUFFER_CAPACITY)
    {
        index = 0U;
    }

    return index;
}

static uint16_t IndexFromOldest(uint16_t offset)
{
    uint32_t index = (uint32_t)oldest_index + (uint32_t)offset;
    if (index >= ACQUISITION_BUFFER_CAPACITY)
    {
        index -= ACQUISITION_BUFFER_CAPACITY;
    }

    return (uint16_t)index;
}

void AcquisitionBuffer_Init(void)
{
    oldest_index = 0U;
    frame_count = 0U;
    overflow_count = 0U;
}

void AcquisitionBuffer_Push(const AcquisitionFrame_t *frame)
{
    if (frame == NULL)
    {
        return;
    }

    if (frame_count < ACQUISITION_BUFFER_CAPACITY)
    {
        const uint16_t write_index = IndexFromOldest(frame_count);
        frames[write_index] = *frame;
        frame_count++;
        return;
    }

    frames[oldest_index] = *frame;
    oldest_index = AdvanceIndex(oldest_index);
    overflow_count++;
}

uint16_t AcquisitionBuffer_GetCount(void)
{
    return frame_count;
}

uint16_t AcquisitionBuffer_GetCapacity(void)
{
    return (uint16_t)ACQUISITION_BUFFER_CAPACITY;
}

uint32_t AcquisitionBuffer_GetOverflowCount(void)
{
    return overflow_count;
}

bool AcquisitionBuffer_GetOldestSequence(uint32_t *sequence)
{
    if ((sequence == NULL) || (frame_count == 0U))
    {
        return false;
    }

    *sequence = frames[oldest_index].sequence;
    return true;
}

bool AcquisitionBuffer_GetLatestSequence(uint32_t *sequence)
{
    if ((sequence == NULL) || (frame_count == 0U))
    {
        return false;
    }

    *sequence = frames[IndexFromOldest((uint16_t)(frame_count - 1U))].sequence;
    return true;
}

bool AcquisitionBuffer_GetFrame(
    uint32_t sequence,
    AcquisitionFrame_t *frame)
{
    if (frame == NULL)
    {
        return false;
    }

    for (uint16_t offset = 0U; offset < frame_count; offset++)
    {
        const uint16_t index = IndexFromOldest(offset);
        if (frames[index].sequence == sequence)
        {
            *frame = frames[index];
            return true;
        }
    }

    return false;
}
