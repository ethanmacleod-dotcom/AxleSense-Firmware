#include "app.h"

#include "acquisition.h"
#include "config_storage.h"
#include "node_time.h"
#include "rs485_protocol.h"

static NodeConfig_t runtime_config;
static ConfigStorage_LoadResult_t runtime_config_load_result;
static ConfigStorage_LoadStatus_t runtime_config_load_status;

static uint32_t App_GetSelectedConfigGeneration(void)
{
    const ConfigStorage_SlotResult_t *selected_slot = NULL;

    if (runtime_config_load_status != CONFIG_STORAGE_LOAD_PERSISTENT)
    {
        return 0U;
    }

    if (runtime_config_load_result.selected_slot == CONFIG_STORAGE_SLOT_A)
    {
        selected_slot = &runtime_config_load_result.slot_a;
    }
    else if (runtime_config_load_result.selected_slot == CONFIG_STORAGE_SLOT_B)
    {
        selected_slot = &runtime_config_load_result.slot_b;
    }

    if ((selected_slot == NULL) || !selected_slot->generation_available)
    {
        return 0U;
    }

    return (uint32_t)selected_slot->generation;
}

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle)
{
    (void)rs485_uart_handle;

    NodeTime_Init();

    runtime_config_load_status = ConfigStorage_Load(
        &runtime_config,
        &runtime_config_load_result);

    Acquisition_Init(spi, &runtime_config);
    Rs485Protocol_Init(
        rs485_uart_handle,
        &runtime_config,
        App_GetSelectedConfigGeneration());
}

void App_Process(void)
{
    Acquisition_Process();
    Rs485Protocol_Process();
}
