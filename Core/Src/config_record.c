#include "config_record.h"

#include <string.h>

#define CONFIG_RECORD_CRC_COVERED_HEADER_SIZE       40U
#define CONFIG_RECORD_GENERATION_HALF_RANGE         UINT64_C(0x8000000000000000)

static void WriteU16LE(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void WriteU32LE(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void WriteU64LE(uint8_t *destination, uint64_t value)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        destination[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint16_t ReadU16LE(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t ReadU32LE(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t ReadU64LE(const uint8_t *source)
{
    uint64_t value = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        value |= ((uint64_t)source[i] << (8U * i));
    }

    return value;
}

static uint32_t Crc32Update(uint32_t crc,
                            const uint8_t *data,
                            size_t length)
{
    for (size_t i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1) ^
                      CONFIG_RECORD_CRC32_REFLECTED_POLYNOMIAL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static bool HeaderIsErased(
    const uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE])
{
    return (ReadU32LE(&encoded_header[0x00]) ==
            CONFIG_RECORD_ERASED_FLASH_WORD) &&
           (ReadU32LE(&encoded_header[0x2C]) ==
            CONFIG_RECORD_ERASED_FLASH_WORD);
}

bool ConfigRecord_InitV1Header(ConfigRecord_Header_t *header,
                               uint64_t generation,
                               uint32_t payload_schema_version,
                               uint32_t payload_length)
{
    if ((header == NULL) ||
        (payload_schema_version == 0U) ||
        (payload_length == 0U) ||
        (payload_length > CONFIG_RECORD_MAX_PAYLOAD_SIZE))
    {
        return false;
    }

    (void)memset(header, 0, sizeof(*header));
    header->magic = CONFIG_RECORD_MAGIC;
    header->storage_format_version =
        CONFIG_RECORD_STORAGE_FORMAT_VERSION_V1;
    header->header_length = CONFIG_RECORD_V1_HEADER_SIZE;
    header->record_type = CONFIG_RECORD_TYPE_NODE_CONFIGURATION;
    header->payload_schema_version = payload_schema_version;
    header->payload_length = payload_length;
    header->generation = generation;
    header->crc32 = CONFIG_RECORD_ERASED_FLASH_WORD;
    header->commit_marker = CONFIG_RECORD_ERASED_FLASH_WORD;

    return true;
}

bool ConfigRecord_EncodeHeaderV1(
    const ConfigRecord_Header_t *header,
    uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE])
{
    if ((header == NULL) || (encoded_header == NULL))
    {
        return false;
    }

    WriteU32LE(&encoded_header[0x00], header->magic);
    WriteU16LE(&encoded_header[0x04], header->storage_format_version);
    WriteU16LE(&encoded_header[0x06], header->header_length);
    WriteU32LE(&encoded_header[0x08], header->record_type);
    WriteU32LE(&encoded_header[0x0C], header->payload_schema_version);
    WriteU32LE(&encoded_header[0x10], header->payload_length);
    WriteU32LE(&encoded_header[0x14], header->flags);
    WriteU64LE(&encoded_header[0x18], header->generation);
    WriteU32LE(&encoded_header[0x20], header->reserved0);
    WriteU32LE(&encoded_header[0x24], header->reserved1);
    WriteU32LE(&encoded_header[0x28], header->crc32);
    WriteU32LE(&encoded_header[0x2C], header->commit_marker);

    return true;
}

bool ConfigRecord_DecodeHeaderV1(
    const uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE],
    ConfigRecord_Header_t *header)
{
    ConfigRecord_Header_t decoded;

    if ((encoded_header == NULL) || (header == NULL))
    {
        return false;
    }

    decoded.magic = ReadU32LE(&encoded_header[0x00]);
    decoded.storage_format_version = ReadU16LE(&encoded_header[0x04]);
    decoded.header_length = ReadU16LE(&encoded_header[0x06]);
    decoded.record_type = ReadU32LE(&encoded_header[0x08]);
    decoded.payload_schema_version = ReadU32LE(&encoded_header[0x0C]);
    decoded.payload_length = ReadU32LE(&encoded_header[0x10]);
    decoded.flags = ReadU32LE(&encoded_header[0x14]);
    decoded.generation = ReadU64LE(&encoded_header[0x18]);
    decoded.reserved0 = ReadU32LE(&encoded_header[0x20]);
    decoded.reserved1 = ReadU32LE(&encoded_header[0x24]);
    decoded.crc32 = ReadU32LE(&encoded_header[0x28]);
    decoded.commit_marker = ReadU32LE(&encoded_header[0x2C]);

    *header = decoded;
    return true;
}

bool ConfigRecord_CalculateCrc32IsoHdlc(const uint8_t *data,
                                        size_t length,
                                        uint32_t *crc_out)
{
    uint32_t crc;

    if ((crc_out == NULL) || ((data == NULL) && (length != 0U)))
    {
        return false;
    }

    crc = CONFIG_RECORD_CRC32_INITIAL_VALUE;
    crc = Crc32Update(crc, data, length);
    *crc_out = crc ^ CONFIG_RECORD_CRC32_FINAL_XOR;
    return true;
}

bool ConfigRecord_CalculateRecordCrcV1(
    const ConfigRecord_Header_t *header,
    const uint8_t *payload,
    uint32_t *crc_out)
{
    uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE];
    uint32_t crc;

    if ((header == NULL) || (crc_out == NULL) ||
        (header->payload_length == 0U) ||
        (header->payload_length > CONFIG_RECORD_MAX_PAYLOAD_SIZE) ||
        (payload == NULL))
    {
        return false;
    }

    if (!ConfigRecord_EncodeHeaderV1(header, encoded_header))
    {
        return false;
    }

    crc = CONFIG_RECORD_CRC32_INITIAL_VALUE;
    crc = Crc32Update(crc,
                      encoded_header,
                      CONFIG_RECORD_CRC_COVERED_HEADER_SIZE);
    crc = Crc32Update(crc, payload, header->payload_length);
    *crc_out = crc ^ CONFIG_RECORD_CRC32_FINAL_XOR;
    return true;
}

bool ConfigRecord_FinalizeV1(ConfigRecord_Header_t *header,
                             const uint8_t *payload)
{
    uint32_t crc;

    if (header == NULL)
    {
        return false;
    }

    header->commit_marker = CONFIG_RECORD_ERASED_FLASH_WORD;
    if (!ConfigRecord_CalculateRecordCrcV1(header, payload, &crc))
    {
        return false;
    }

    header->crc32 = crc;
    header->commit_marker = CONFIG_RECORD_COMMIT_MARKER;
    return true;
}

ConfigRecord_Status_t ConfigRecord_ValidateV1(
    const uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE],
    const uint8_t *payload,
    size_t payload_buffer_length,
    ConfigRecord_Header_t *decoded_header)
{
    ConfigRecord_Header_t header;
    uint32_t calculated_crc;

    if (encoded_header == NULL)
    {
        return CONFIG_RECORD_STATUS_INVALID_ARGUMENT;
    }
    if (HeaderIsErased(encoded_header))
    {
        return CONFIG_RECORD_STATUS_ERASED;
    }
    if (!ConfigRecord_DecodeHeaderV1(encoded_header, &header))
    {
        return CONFIG_RECORD_STATUS_INVALID_ARGUMENT;
    }

    if (decoded_header != NULL)
    {
        *decoded_header = header;
    }

    if (header.commit_marker != CONFIG_RECORD_COMMIT_MARKER)
    {
        return CONFIG_RECORD_STATUS_UNCOMMITTED;
    }
    if (header.magic != CONFIG_RECORD_MAGIC)
    {
        return CONFIG_RECORD_STATUS_CORRUPT_MAGIC;
    }
    if (header.storage_format_version !=
        CONFIG_RECORD_STORAGE_FORMAT_VERSION_V1)
    {
        return CONFIG_RECORD_STATUS_UNSUPPORTED_STORAGE_VERSION;
    }
    if (header.header_length != CONFIG_RECORD_V1_HEADER_SIZE)
    {
        return CONFIG_RECORD_STATUS_INVALID_HEADER_LENGTH;
    }
    if (header.record_type != CONFIG_RECORD_TYPE_NODE_CONFIGURATION)
    {
        return CONFIG_RECORD_STATUS_UNSUPPORTED_RECORD_TYPE;
    }
    if (header.payload_schema_version !=
        CONFIG_RECORD_PAYLOAD_SCHEMA_VERSION_V1)
    {
        return CONFIG_RECORD_STATUS_UNSUPPORTED_PAYLOAD_SCHEMA;
    }
    if ((header.payload_length == 0U) ||
        (header.payload_length > CONFIG_RECORD_MAX_PAYLOAD_SIZE) ||
        (header.payload_length != CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE))
    {
        return CONFIG_RECORD_STATUS_INVALID_PAYLOAD_LENGTH;
    }
    if (header.flags != 0U)
    {
        return CONFIG_RECORD_STATUS_INVALID_FLAGS;
    }
    if ((header.reserved0 != 0U) || (header.reserved1 != 0U))
    {
        return CONFIG_RECORD_STATUS_INVALID_RESERVED_WORDS;
    }
    if ((payload == NULL) ||
        (payload_buffer_length < (size_t)header.payload_length))
    {
        return CONFIG_RECORD_STATUS_TRUNCATED_PAYLOAD;
    }
    if (!ConfigRecord_CalculateRecordCrcV1(
            &header, payload, &calculated_crc))
    {
        return CONFIG_RECORD_STATUS_INVALID_ARGUMENT;
    }
    if (calculated_crc != header.crc32)
    {
        return CONFIG_RECORD_STATUS_CRC_MISMATCH;
    }

    return CONFIG_RECORD_STATUS_VALID;
}

ConfigRecord_GenerationOrder_t ConfigRecord_CompareGenerations(uint64_t a,
                                                                uint64_t b)
{
    uint64_t difference;

    if (a == b)
    {
        return CONFIG_RECORD_GENERATION_EQUAL;
    }

    difference = a - b;
    if (difference == CONFIG_RECORD_GENERATION_HALF_RANGE)
    {
        return CONFIG_RECORD_GENERATION_AMBIGUOUS;
    }
    if (difference < CONFIG_RECORD_GENERATION_HALF_RANGE)
    {
        return CONFIG_RECORD_GENERATION_A_NEWER;
    }

    return CONFIG_RECORD_GENERATION_A_OLDER;
}
