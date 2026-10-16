#include "rs485_protocol.h"

#include "acquisition.h"
#include "axlesense_version.h"
#include "main.h"
#include "node_time.h"

#include <stddef.h>

#define RS485_PROTOCOL_SYNC0                    0xAAU
#define RS485_PROTOCOL_SYNC1                    0x55U
#define RS485_PROTOCOL_MESSAGE_PING             0x01U
#define RS485_PROTOCOL_MESSAGE_GET_STATUS       0x02U
#define RS485_PROTOCOL_MESSAGE_PONG             0x81U
#define RS485_PROTOCOL_MESSAGE_STATUS           0x82U
#define RS485_PROTOCOL_STATUS_OK                0x00U
#define RS485_PROTOCOL_NODE_STATE_RUNNING       0x02U
#define RS485_PROTOCOL_MAX_PAYLOAD              1024U
#define RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE   7U
#define RS485_PROTOCOL_PONG_PAYLOAD_SIZE        18U
#define RS485_PROTOCOL_PONG_PACKET_SIZE         29U
#define RS485_PROTOCOL_STATUS_PAYLOAD_SIZE      52U
#define RS485_PROTOCOL_STATUS_PACKET_SIZE       63U
#define RS485_PROTOCOL_NO_SEQUENCE              0xFFFFFFFFU
#define RS485_PROTOCOL_CRC_INITIAL              0xFFFFU
#define RS485_PROTOCOL_CRC_POLYNOMIAL           0x1021U

_Static_assert(
    RS485_PROTOCOL_PONG_PACKET_SIZE ==
        (2U + RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE +
         RS485_PROTOCOL_PONG_PAYLOAD_SIZE + 2U),
    "PONG packet size must be 29 bytes");

_Static_assert(
    RS485_PROTOCOL_STATUS_PACKET_SIZE ==
        (2U + RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE +
         RS485_PROTOCOL_STATUS_PAYLOAD_SIZE + 2U),
    "STATUS packet size must be 63 bytes");

typedef enum
{
    RS485_RX_SYNC0 = 0,
    RS485_RX_SYNC1,
    RS485_RX_HEADER,
    RS485_RX_PAYLOAD,
    RS485_RX_CRC_LOW,
    RS485_RX_CRC_HIGH
} Rs485Protocol_RxState_t;

static UART_HandleTypeDef *protocol_uart = NULL;
static uint8_t configured_node_address = 0U;
static uint32_t configured_generation = 0U;
static uint8_t rx_byte = 0U;
static uint8_t rx_header[RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE];
static uint8_t rx_payload[RS485_PROTOCOL_MAX_PAYLOAD];
static uint16_t rx_header_index = 0U;
static uint16_t rx_payload_index = 0U;
static uint16_t rx_payload_length = 0U;
static uint16_t rx_crc = RS485_PROTOCOL_CRC_INITIAL;
static uint16_t rx_received_crc = 0U;
static Rs485Protocol_RxState_t rx_state = RS485_RX_SYNC0;

static volatile uint8_t pending_response_type = 0U;
static volatile uint16_t pending_request_id = 0U;
static volatile uint8_t tx_busy = 0U;
static uint8_t tx_buffer[RS485_PROTOCOL_STATUS_PACKET_SIZE];

static volatile uint32_t rx_crc_errors = 0U;
static volatile uint32_t protocol_errors = 0U;

static uint16_t Crc16Update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)((uint16_t)byte << 8U);
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
        if ((crc & 0x8000U) != 0U)
        {
            crc = (uint16_t)((crc << 1U) ^
                             RS485_PROTOCOL_CRC_POLYNOMIAL);
        }
        else
        {
            crc = (uint16_t)(crc << 1U);
        }
    }

    return crc;
}

static uint16_t Crc16Calculate(const uint8_t *data, uint16_t length)
{
    uint16_t crc = RS485_PROTOCOL_CRC_INITIAL;

    for (uint16_t i = 0U; i < length; i++)
    {
        crc = Crc16Update(crc, data[i]);
    }

    return crc;
}

static void ParserReset(void)
{
    rx_state = RS485_RX_SYNC0;
    rx_header_index = 0U;
    rx_payload_index = 0U;
    rx_payload_length = 0U;
    rx_crc = RS485_PROTOCOL_CRC_INITIAL;
    rx_received_crc = 0U;
}

static void QueueResponse(uint8_t response_type, uint16_t request_id)
{
    if ((tx_busy != 0U) || (pending_response_type != 0U))
    {
        return;
    }

    pending_request_id = request_id;
    pending_response_type = response_type;
}

static void DispatchPacket(void)
{
    const uint8_t message_type = rx_header[1];
    const uint8_t node_address = rx_header[2];
    const uint16_t request_id =
        (uint16_t)rx_header[3] |
        (uint16_t)((uint16_t)rx_header[4] << 8U);

    if (node_address != configured_node_address)
    {
        return;
    }

    if (message_type == RS485_PROTOCOL_MESSAGE_PING)
    {
        if (rx_payload_length != 0U)
        {
            protocol_errors++;
            return;
        }

        QueueResponse(RS485_PROTOCOL_MESSAGE_PONG, request_id);
        return;
    }

    if (message_type == RS485_PROTOCOL_MESSAGE_GET_STATUS)
    {
        if (rx_payload_length != 0U)
        {
            protocol_errors++;
            return;
        }

        QueueResponse(RS485_PROTOCOL_MESSAGE_STATUS, request_id);
        return;
    }

    protocol_errors++;
}

static void ParserConsumeByte(uint8_t byte)
{
    switch (rx_state)
    {
        case RS485_RX_SYNC0:
            if (byte == RS485_PROTOCOL_SYNC0)
            {
                rx_state = RS485_RX_SYNC1;
            }
            break;

        case RS485_RX_SYNC1:
            if (byte == RS485_PROTOCOL_SYNC1)
            {
                rx_state = RS485_RX_HEADER;
                rx_header_index = 0U;
                rx_crc = RS485_PROTOCOL_CRC_INITIAL;
            }
            else if (byte != RS485_PROTOCOL_SYNC0)
            {
                rx_state = RS485_RX_SYNC0;
            }
            break;

        case RS485_RX_HEADER:
            rx_header[rx_header_index] = byte;
            rx_header_index++;
            rx_crc = Crc16Update(rx_crc, byte);

            if (rx_header_index >=
                RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE)
            {
                rx_payload_length =
                    (uint16_t)rx_header[5] |
                    (uint16_t)((uint16_t)rx_header[6] << 8U);
                rx_payload_index = 0U;

                if (rx_header[0] != AXLESENSE_PROTOCOL_VERSION)
                {
                    if (rx_header[2] == configured_node_address)
                    {
                        protocol_errors++;
                    }
                    ParserReset();
                }
                else if (rx_payload_length > RS485_PROTOCOL_MAX_PAYLOAD)
                {
                    if (rx_header[2] == configured_node_address)
                    {
                        protocol_errors++;
                    }
                    ParserReset();
                }
                else if (rx_payload_length == 0U)
                {
                    rx_state = RS485_RX_CRC_LOW;
                }
                else
                {
                    rx_state = RS485_RX_PAYLOAD;
                }
            }
            break;

        case RS485_RX_PAYLOAD:
            rx_payload[rx_payload_index] = byte;
            rx_payload_index++;
            rx_crc = Crc16Update(rx_crc, byte);
            if (rx_payload_index >= rx_payload_length)
            {
                rx_state = RS485_RX_CRC_LOW;
            }
            break;

        case RS485_RX_CRC_LOW:
            rx_received_crc = byte;
            rx_state = RS485_RX_CRC_HIGH;
            break;

        case RS485_RX_CRC_HIGH:
            rx_received_crc |= (uint16_t)((uint16_t)byte << 8U);
            if (rx_received_crc == rx_crc)
            {
                DispatchPacket();
            }
            else
            {
                rx_crc_errors++;
            }
            ParserReset();
            break;

        default:
            ParserReset();
            break;
    }
}

static void WriteUint16LittleEndian(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void WriteUint32LittleEndian(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void WriteUint64LittleEndian(uint8_t *destination, uint64_t value)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        destination[i] = (uint8_t)(value >> (8U * i));
    }
}

static void SendPong(uint16_t request_id)
{
    tx_buffer[0] = RS485_PROTOCOL_SYNC0;
    tx_buffer[1] = RS485_PROTOCOL_SYNC1;
    tx_buffer[2] = AXLESENSE_PROTOCOL_VERSION;
    tx_buffer[3] = RS485_PROTOCOL_MESSAGE_PONG;
    tx_buffer[4] = configured_node_address;
    tx_buffer[5] = (uint8_t)request_id;
    tx_buffer[6] = (uint8_t)(request_id >> 8U);
    tx_buffer[7] = (uint8_t)RS485_PROTOCOL_PONG_PAYLOAD_SIZE;
    tx_buffer[8] = 0U;

    tx_buffer[9] = RS485_PROTOCOL_STATUS_OK;
    tx_buffer[10] = AXLESENSE_FIRMWARE_VERSION_MAJOR;
    tx_buffer[11] = AXLESENSE_FIRMWARE_VERSION_MINOR;
    tx_buffer[12] = AXLESENSE_FIRMWARE_VERSION_PATCH;
    tx_buffer[13] = AXLESENSE_HARDWARE_VERSION_MAJOR;
    tx_buffer[14] = AXLESENSE_HARDWARE_VERSION_MINOR;
    WriteUint32LittleEndian(&tx_buffer[15], HAL_GetUIDw0());
    WriteUint32LittleEndian(&tx_buffer[19], HAL_GetUIDw1());
    WriteUint32LittleEndian(&tx_buffer[23], HAL_GetUIDw2());

    const uint16_t crc = Crc16Calculate(
        &tx_buffer[2],
        (uint16_t)(RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE +
                   RS485_PROTOCOL_PONG_PAYLOAD_SIZE));
    tx_buffer[27] = (uint8_t)crc;
    tx_buffer[28] = (uint8_t)(crc >> 8U);

    HAL_GPIO_WritePin(
        RS485_RE_DE_GPIO_Port,
        RS485_RE_DE_Pin,
        GPIO_PIN_SET);

    if (HAL_UART_Transmit_IT(
            protocol_uart,
            tx_buffer,
            RS485_PROTOCOL_PONG_PACKET_SIZE) != HAL_OK)
    {
        HAL_GPIO_WritePin(
            RS485_RE_DE_GPIO_Port,
            RS485_RE_DE_Pin,
            GPIO_PIN_RESET);
        tx_busy = 0U;
        return;
    }

}

static void SendStatus(uint16_t request_id)
{
    uint32_t latest_sequence = RS485_PROTOCOL_NO_SEQUENCE;
    (void)Acquisition_GetLatestSequence(&latest_sequence);

    tx_buffer[0] = RS485_PROTOCOL_SYNC0;
    tx_buffer[1] = RS485_PROTOCOL_SYNC1;
    tx_buffer[2] = AXLESENSE_PROTOCOL_VERSION;
    tx_buffer[3] = RS485_PROTOCOL_MESSAGE_STATUS;
    tx_buffer[4] = configured_node_address;
    WriteUint16LittleEndian(&tx_buffer[5], request_id);
    WriteUint16LittleEndian(
        &tx_buffer[7],
        RS485_PROTOCOL_STATUS_PAYLOAD_SIZE);

    tx_buffer[9] = RS485_PROTOCOL_STATUS_OK;
    tx_buffer[10] = RS485_PROTOCOL_NODE_STATE_RUNNING;
    tx_buffer[11] = Acquisition_GetEnabledChannelMask();
    tx_buffer[12] = 0U;
    WriteUint64LittleEndian(&tx_buffer[13], NodeTime_GetUs());
    WriteUint32LittleEndian(&tx_buffer[21], 0U);
    WriteUint32LittleEndian(&tx_buffer[25], latest_sequence);
    WriteUint32LittleEndian(
        &tx_buffer[29],
        RS485_PROTOCOL_NO_SEQUENCE);
    WriteUint16LittleEndian(&tx_buffer[33], 0U);
    WriteUint16LittleEndian(&tx_buffer[35], 0U);
    WriteUint32LittleEndian(&tx_buffer[37], 0U);
    WriteUint32LittleEndian(
        &tx_buffer[41],
        Acquisition_GetReadErrorCount());
    WriteUint32LittleEndian(&tx_buffer[45], rx_crc_errors);
    WriteUint32LittleEndian(&tx_buffer[49], protocol_errors);
    WriteUint32LittleEndian(&tx_buffer[53], 0U);
    WriteUint32LittleEndian(&tx_buffer[57], configured_generation);

    const uint16_t crc = Crc16Calculate(
        &tx_buffer[2],
        (uint16_t)(RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE +
                   RS485_PROTOCOL_STATUS_PAYLOAD_SIZE));
    WriteUint16LittleEndian(&tx_buffer[61], crc);

    HAL_GPIO_WritePin(
        RS485_RE_DE_GPIO_Port,
        RS485_RE_DE_Pin,
        GPIO_PIN_SET);

    if (HAL_UART_Transmit_IT(
            protocol_uart,
            tx_buffer,
            RS485_PROTOCOL_STATUS_PACKET_SIZE) != HAL_OK)
    {
        HAL_GPIO_WritePin(
            RS485_RE_DE_GPIO_Port,
            RS485_RE_DE_Pin,
            GPIO_PIN_RESET);
        tx_busy = 0U;
        return;
    }
}

void Rs485Protocol_Init(
    UART_HandleTypeDef *uart,
    const NodeConfig_t *node_config,
    uint32_t config_generation)
{
    if ((uart == NULL) ||
        (uart->Instance != USART2) ||
        (node_config == NULL))
    {
        return;
    }

    protocol_uart = uart;
    configured_node_address = (uint8_t)node_config->node_address;
    configured_generation = config_generation;
    pending_response_type = 0U;
    pending_request_id = 0U;
    tx_busy = 0U;
    ParserReset();

    HAL_GPIO_WritePin(
        RS485_RE_DE_GPIO_Port,
        RS485_RE_DE_Pin,
        GPIO_PIN_RESET);

    HAL_NVIC_SetPriority(USART2_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    (void)HAL_UART_Receive_IT(protocol_uart, &rx_byte, 1U);
}

void Rs485Protocol_Process(void)
{
    uint16_t request_id = 0U;
    uint8_t response_type = 0U;
    const uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    if ((pending_response_type != 0U) && (tx_busy == 0U))
    {
        request_id = pending_request_id;
        response_type = pending_response_type;
        pending_response_type = 0U;
        tx_busy = 1U;
    }
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }

    if (response_type == RS485_PROTOCOL_MESSAGE_PONG)
    {
        SendPong(request_id);
    }
    else if (response_type == RS485_PROTOCOL_MESSAGE_STATUS)
    {
        SendStatus(request_id);
    }
}

void USART2_IRQHandler(void)
{
    if (protocol_uart != NULL)
    {
        HAL_UART_IRQHandler(protocol_uart);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != protocol_uart)
    {
        return;
    }

    ParserConsumeByte(rx_byte);

    (void)HAL_UART_Receive_IT(protocol_uart, &rx_byte, 1U);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != protocol_uart)
    {
        return;
    }

    HAL_GPIO_WritePin(
        RS485_RE_DE_GPIO_Port,
        RS485_RE_DE_Pin,
        GPIO_PIN_RESET);
    tx_busy = 0U;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != protocol_uart)
    {
        return;
    }

    ParserReset();

    if (uart->RxState == HAL_UART_STATE_READY)
    {
        (void)HAL_UART_Receive_IT(protocol_uart, &rx_byte, 1U);
    }
}
