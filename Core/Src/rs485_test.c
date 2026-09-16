#include "rs485_test.h"

/* RS-485 one-way transmit test diagnostics. */
volatile uint32_t rs485_tx_count = 0U;
volatile int32_t rs485_last_status = -99;
static uint32_t rs485_last_tx_ms = 0U;

void RS485_TestProcess(UART_HandleTypeDef *huart)
{
    /*
     * RS-485 sanity test.
     *
     * PCB routing:
     *   USART2_TX = PA2 -> ADM2587 TXD
     *   USART2_RX = PA3 <- ADM2587 RXD
     *   PA4       = RE/DE direction control
     *
     * Drive PA4 high while transmitting, then return low to receive mode.
     */
    const uint32_t now_ms = HAL_GetTick();

    if ((now_ms - rs485_last_tx_ms) >= 1000U)
    {
        static const uint8_t rs485_msg[] =
            "AXLESENSE RS485 TEST OK\r\n";

        rs485_last_tx_ms = now_ms;

        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

        rs485_last_status = (int32_t)HAL_UART_Transmit(
            huart,
            (uint8_t *)rs485_msg,
            (uint16_t)(sizeof(rs485_msg) - 1U),
            100U
        );

        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

        if (rs485_last_status == (int32_t)HAL_OK)
        {
            rs485_tx_count++;
        }
    }
}
