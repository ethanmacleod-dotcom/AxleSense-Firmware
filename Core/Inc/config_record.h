#ifndef CONFIG_RECORD_H
#define CONFIG_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_RECORD_V1_HEADER_SIZE                48U
#define CONFIG_RECORD_MAX_PAYLOAD_SIZE              4096U
#define CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE     256U

#define CONFIG_RECORD_STORAGE_FORMAT_VERSION_V1     1U
#define CONFIG_RECORD_TYPE_NODE_CONFIGURATION       1UL
#define CONFIG_RECORD_PAYLOAD_SCHEMA_VERSION_V1     1UL

#define CONFIG_RECORD_MAGIC                         0x46435841UL
#define CONFIG_RECORD_COMMIT_MARKER                 0x54494D43UL
#define CONFIG_RECORD_ERASED_FLASH_WORD             0xFFFFFFFFUL

#define CONFIG_RECORD_CRC32_POLYNOMIAL              0x04C11DB7UL
#define CONFIG_RECORD_CRC32_REFLECTED_POLYNOMIAL    0xEDB88320UL
#define CONFIG_RECORD_CRC32_INITIAL_VALUE           0xFFFFFFFFUL
#define CONFIG_RECORD_CRC32_FINAL_XOR               0xFFFFFFFFUL

typedef struct
{
    uint32_t magic;
    uint16_t storage_format_version;
    uint16_t header_length;
    uint32_t record_type;
    uint32_t payload_schema_version;
    uint32_t payload_length;
    uint32_t flags;
    uint64_t generation;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t crc32;
    uint32_t commit_marker;
} ConfigRecord_Header_t;

typedef enum
{
    CONFIG_RECORD_STATUS_VALID = 0,
    CONFIG_RECORD_STATUS_ERASED,
    CONFIG_RECORD_STATUS_UNCOMMITTED,
    CONFIG_RECORD_STATUS_INVALID_ARGUMENT,
    CONFIG_RECORD_STATUS_CORRUPT_MAGIC,
    CONFIG_RECORD_STATUS_UNSUPPORTED_STORAGE_VERSION,
    CONFIG_RECORD_STATUS_INVALID_HEADER_LENGTH,
    CONFIG_RECORD_STATUS_UNSUPPORTED_RECORD_TYPE,
    CONFIG_RECORD_STATUS_UNSUPPORTED_PAYLOAD_SCHEMA,
    CONFIG_RECORD_STATUS_INVALID_PAYLOAD_LENGTH,
    CONFIG_RECORD_STATUS_INVALID_FLAGS,
    CONFIG_RECORD_STATUS_INVALID_RESERVED_WORDS,
    CONFIG_RECORD_STATUS_TRUNCATED_PAYLOAD,
    CONFIG_RECORD_STATUS_CRC_MISMATCH
} ConfigRecord_Status_t;

typedef enum
{
    CONFIG_RECORD_GENERATION_EQUAL = 0,
    CONFIG_RECORD_GENERATION_A_NEWER,
    CONFIG_RECORD_GENERATION_A_OLDER,
    CONFIG_RECORD_GENERATION_AMBIGUOUS
} ConfigRecord_GenerationOrder_t;

bool ConfigRecord_InitV1Header(ConfigRecord_Header_t *header,
                               uint64_t generation,
                               uint32_t payload_schema_version,
                               uint32_t payload_length);

bool ConfigRecord_EncodeHeaderV1(
    const ConfigRecord_Header_t *header,
    uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE]);

bool ConfigRecord_DecodeHeaderV1(
    const uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE],
    ConfigRecord_Header_t *header);

bool ConfigRecord_CalculateCrc32IsoHdlc(const uint8_t *data,
                                        size_t length,
                                        uint32_t *crc_out);

bool ConfigRecord_CalculateRecordCrcV1(
    const ConfigRecord_Header_t *header,
    const uint8_t *payload,
    uint32_t *crc_out);

bool ConfigRecord_FinalizeV1(ConfigRecord_Header_t *header,
                             const uint8_t *payload);

ConfigRecord_Status_t ConfigRecord_ValidateV1(
    const uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE],
    const uint8_t *payload,
    size_t payload_buffer_length,
    ConfigRecord_Header_t *decoded_header);

ConfigRecord_GenerationOrder_t ConfigRecord_CompareGenerations(uint64_t a,
                                                                uint64_t b);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RECORD_H */
