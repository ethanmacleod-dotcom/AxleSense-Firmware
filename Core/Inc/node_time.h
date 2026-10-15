#ifndef NODE_TIME_H
#define NODE_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void NodeTime_Init(void);
uint64_t NodeTime_GetUs(void);
void NodeTime_HandleOverflowIrq(void);

#ifdef __cplusplus
}
#endif

#endif /* NODE_TIME_H */
