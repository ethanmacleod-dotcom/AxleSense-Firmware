#include "rs485_protocol.h"

#include "axlesense_version.h"
#include "main.h"

#include <stddef.h>

#define RS485_PROTOCOL_SYNC0                    0xAAU
#define RS485_PROTOCOL_SYNC1                    0x55U
#define RS485_PROTOCOL_MESSAGE_PING             0x01U
#define RS485_PROTOCOL_MESSAGE_PONG             0x81U
#define RS485_PROTOCOL_STATUS_OK                0x00U
#define RS485_PROTOCOL_MAX_PAYLOAD              1024U
#define RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE   7U
#define RS485_PROTOCOL_PONG_PAYLOAD_SIZE        18U
#define RS485_PROTOCOL_PONG_PACKET_SIZE         29U
#define RS485_PROTOCOL_CRC_INITIAL              0xFFFFU
#define RS485_PROTOCOL_CRC_POLYNOMIAL           0x1021U

_Static_assert(
    RS485_PROTOCOL_PONG_PACKET_SIZE ==
        (2U + RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE +
         RS485_PROTOCOL_PONG_PAYLOAD_SIZE + 2U),
    "PONG packet size must be 29 bytes");

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
static uint8_t rx_byte = 0U;
static uint8_t rx_header[RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE];
static uint8_t rx_payload[RS485_PROTOCOL_MAX_PAYLOAD];
static uint16_t rx_header_index = 0U;
static uint16_t rx_payload_index = 0U;
static uint16_t rx_payload_length = 0U;
static uint16_t rx_crc = RS485_PROTOCOL_CRC_INITIAL;
static uint16_t rx_received_crc = 0U;
static Rs485Protocol_RxState_t rx_state = RS485_RX_SYNC0;

static volatile uint8_t pending_ping = 0U;
static volatile uint16_t pending_request_id = 0U;
static volatile uint8_t tx_busy = 0U;
static uint8_t tx_buffer[RS485_PROTOCOL_PONG_PACKET_SIZE];

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

static void QueuePing(uint16_t request_id)
{
    if ((tx_busy != 0U) || (pending_ping != 0U))
    {
        return;
    }

    pending_request_id = request_id;
    pending_ping = 1U;
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

    if ((message_type != RS485_PROTOCOL_MESSAGE_PING) ||
        (rx_payload_length != 0U))
    {
        return;
    }

    QueuePing(request_id);
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

            if ((rx_header_index == 1U) &&
                (byte != AXLESENSE_PROTOCOL_VERSION))
            {
                ParserReset();
            }
            else if (rx_header_index >=
                     RS485_PROTOCOL_HEADER_AFTER_SYNC_SIZE)
            {
                rx_payload_length =
                    (uint16_t)rx_header[5] |
                    (uint16_t)((uint16_t)rx_header[6] << 8U);
                rx_payload_index = 0U;

                if (rx_payload_length > RS485_PROTOCOL_MAX_PAYLOAD)
                {
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
            ParserReset();
            break;

        default:
            ParserReset();
            break;
    }
}

static void WriteUint32LittleEndian(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
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

void Rs485Protocol_Init(
    UART_HandleTypeDef *uart,
    const NodeConfig_t *node_config)
{
    if ((uart == NULL) ||
        (uart->Instance != USART2) ||
        (node_config == NULL))
    {
        return;
    }

    protocol_uart = uart;
    configured_node_address = (uint8_t)node_config->node_address;
    pending_ping = 0U;
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
    uint8_t send_pong = 0U;
    const uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    if ((pending_ping != 0U) && (tx_busy == 0U))
    {
        request_id = pending_request_id;
        pending_ping = 0U;
        tx_busy = 1U;
        send_pong = 1U;
    }
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }

    if (send_pong != 0U)
    {
        SendPong(request_id);
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
