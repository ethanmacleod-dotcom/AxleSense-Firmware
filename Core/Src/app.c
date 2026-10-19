#include "app.h"

#include "acquisition.h"
#include "config_storage.h"
#include "main.h"
#include "node_time.h"
#include "rs485_protocol.h"

static NodeConfig_t runtime_config;
static ConfigStorage_LoadResult_t runtime_config_load_result;
static ConfigStorage_LoadStatus_t runtime_config_load_status;

static uint64_t App_GetSelectedConfigGeneration(void)
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

    return selected_slot->generation;
}

static void App_ApplyRs485UartConfig(UART_HandleTypeDef *uart)
{
    if ((uart == NULL) || (uart->Instance != USART2))
    {
        Error_Handler();
        return;
    }

    if (uart->Init.BaudRate == runtime_config.uart_baud_rate)
    {
        return;
    }

    if (HAL_UART_DeInit(uart) != HAL_OK)
    {
        Error_Handler();
        return;
    }

    uart->Init.BaudRate = runtime_config.uart_baud_rate;
    if (HAL_UART_Init(uart) != HAL_OK)
    {
        Error_Handler();
    }
}

static uint32_t App_GetInitialFaultFlags(void)
{
    uint32_t fault_flags = 0U;

    if (runtime_config_load_status != CONFIG_STORAGE_LOAD_PERSISTENT)
    {
        fault_flags |= RS485_PROTOCOL_FAULT_CONFIG_STORAGE;
    }

    if (Acquisition_IsReady() == 0U)
    {
        fault_flags |= RS485_PROTOCOL_FAULT_ADC_INIT;
    }

    return fault_flags;
}

void App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *rs485_uart_handle)
{
    NodeTime_Init();

    runtime_config_load_status = ConfigStorage_Load(
        &runtime_config,
        &runtime_config_load_result);

    App_ApplyRs485UartConfig(rs485_uart_handle);
    Acquisition_Init(spi, &runtime_config);
    Rs485Protocol_Init(
        rs485_uart_handle,
        &runtime_config,
        App_GetSelectedConfigGeneration(),
        App_GetInitialFaultFlags());
}

void App_Process(void)
{
    Acquisition_Process();
    Rs485Protocol_Process();
}
