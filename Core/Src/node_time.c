#include "node_time.h"

#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim2;

static volatile uint32_t timer_overflow_high = 0U;

void NodeTime_Init(void)
{
    timer_overflow_high = 0U;
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_IT(&htim2, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(&htim2);
}

uint64_t NodeTime_GetUs(void)
{
    uint32_t high;
    uint32_t counter;
    const uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();

    high = timer_overflow_high;
    counter = __HAL_TIM_GET_COUNTER(&htim2);

    if (__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET)
    {
        high++;
        counter = __HAL_TIM_GET_COUNTER(&htim2);
    }

    if (interrupt_state == 0U)
    {
        __enable_irq();
    }

    return ((uint64_t)high << 32U) | counter;
}

void NodeTime_HandleOverflowIrq(void)
{
    const uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();

    if ((__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&htim2, TIM_IT_UPDATE) != RESET))
    {
        __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);
        timer_overflow_high++;
    }

    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}
