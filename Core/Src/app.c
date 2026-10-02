#include "app.h"

#include "acquisition.h"
#include "config_storage.h"

static NodeConfig_t runtime_config;
static ConfigStorage_LoadResult_t runtime_config_load_result;
static ConfigStorage_LoadStatus_t runtime_config_load_status;

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle)
{
    (void)rs485_uart_handle;

    runtime_config_load_status = ConfigStorage_Load(
        &runtime_config,
        &runtime_config_load_result);

    Acquisition_Init(spi, &runtime_config);
}

void App_Process(void)
{
    Acquisition_Process();
}
