#include "app.h"

#include "acquisition.h"
#include "config_storage.h"
#include "rs485_test.h"

static UART_HandleTypeDef *rs485_uart = NULL;
static NodeConfig_t runtime_config;
static ConfigStorage_LoadResult_t runtime_config_load_result;
static ConfigStorage_LoadStatus_t runtime_config_load_status;

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle)
{
    runtime_config_load_status = ConfigStorage_Load(
        &runtime_config,
        &runtime_config_load_result);

    Acquisition_Init(spi, &runtime_config);
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
