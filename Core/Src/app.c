#include "app.h"

#include "acquisition.h"
#include "rs485_test.h"

static UART_HandleTypeDef *rs485_uart = NULL;

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle)
{
    Acquisition_Init(spi);
    rs485_uart = rs485_uart_handle;
}

void App_Process(void)
{
    if (Acquisition_IsComplete() != 0U)
    {
        RS485_TestProcess(rs485_uart);
        return;
    }

    Acquisition_Process();
}
