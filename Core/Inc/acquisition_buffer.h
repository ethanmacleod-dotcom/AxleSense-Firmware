#ifndef ACQUISITION_BUFFER_H
#define ACQUISITION_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "acquisition.h"

#include <stdbool.h>
#include <stdint.h>

#define ACQUISITION_BUFFER_CAPACITY 1024U

void AcquisitionBuffer_Init(void);
void AcquisitionBuffer_Push(const AcquisitionFrame_t *frame);

uint16_t AcquisitionBuffer_GetCount(void);
uint16_t AcquisitionBuffer_GetCapacity(void);
uint32_t AcquisitionBuffer_GetOverflowCount(void);

bool AcquisitionBuffer_GetOldestSequence(uint32_t *sequence);
bool AcquisitionBuffer_GetLatestSequence(uint32_t *sequence);
bool AcquisitionBuffer_GetFrame(
    uint32_t sequence,
    AcquisitionFrame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* ACQUISITION_BUFFER_H */
