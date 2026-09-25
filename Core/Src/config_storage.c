#include "config_storage.h"

#include "main.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_STORAGE_EXPECTED_A_START             UINT32_C(0x08040000)
#define CONFIG_STORAGE_EXPECTED_A_END               UINT32_C(0x08060000)
#define CONFIG_STORAGE_EXPECTED_B_START             UINT32_C(0x08060000)
#define CONFIG_STORAGE_EXPECTED_B_END               UINT32_C(0x08080000)
#define CONFIG_STORAGE_HEADER_PROGRAM_SIZE          44U
#define CONFIG_STORAGE_COMMIT_OFFSET                44U
#define CONFIG_STORAGE_ERASE_SECTOR_OK              UINT32_C(0xFFFFFFFF)

extern uint8_t __config_a_start__;
extern uint8_t __config_a_end__;
extern uint8_t __config_b_start__;
extern uint8_t __config_b_end__;

_Static_assert(CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE ==
               NODE_CONFIG_V1_PAYLOAD_SIZE,
               "Record and node payload sizes must match");
_Static_assert(CONFIG_STORAGE_V1_RECORD_SIZE == 304U,
               "Frozen V1 meaningful record size must be 304 bytes");
_Static_assert((CONFIG_STORAGE_HEADER_PROGRAM_SIZE % 4U) == 0U,
               "Header programming length must be word aligned");
_Static_assert((NODE_CONFIG_V1_PAYLOAD_SIZE % 4U) == 0U,
               "Payload programming length must be word aligned");
_Static_assert(CONFIG_STORAGE_COMMIT_OFFSET ==
               CONFIG_STORAGE_HEADER_PROGRAM_SIZE,
               "Commit word must immediately follow programmed header bytes");

typedef struct
{
    uintptr_t a_start;
    uintptr_t a_end;
    uintptr_t b_start;
    uintptr_t b_end;
} ConfigStorage_Bounds_t;

typedef struct
{
    uint8_t payload[NODE_CONFIG_V1_PAYLOAD_SIZE];
    uint8_t comparison_payload[NODE_CONFIG_V1_PAYLOAD_SIZE];
    uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE];
    uint8_t readback_header[CONFIG_RECORD_V1_HEADER_SIZE];
    ConfigRecord_Header_t header;
    ConfigRecord_Header_t readback_decoded_header;
    NodeConfig_t decoded_config;
    ConfigStorage_LoadResult_t load_result;
    ConfigStorage_SlotResult_t final_slot_result;
} ConfigStorage_SaveWorkspace_t;

static void InitializeSlotResult(ConfigStorage_SlotId_t slot,
                                 ConfigStorage_SlotResult_t *result)
{
    (void)memset(result, 0, sizeof(*result));
    result->slot = slot;
    result->state = CONFIG_STORAGE_SLOT_STATE_NOT_INSPECTED;
    result->record_status = CONFIG_RECORD_STATUS_INVALID_ARGUMENT;
}

static bool GetValidatedBounds(ConfigStorage_Bounds_t *bounds)
{
    uintptr_t a_size;
    uintptr_t b_size;

    if (bounds == NULL)
    {
        return false;
    }

    bounds->a_start = (uintptr_t)&__config_a_start__;
    bounds->a_end = (uintptr_t)&__config_a_end__;
    bounds->b_start = (uintptr_t)&__config_b_start__;
    bounds->b_end = (uintptr_t)&__config_b_end__;

    if ((bounds->a_end <= bounds->a_start) ||
        (bounds->b_end <= bounds->b_start))
    {
        return false;
    }

    a_size = bounds->a_end - bounds->a_start;
    b_size = bounds->b_end - bounds->b_start;
    if ((a_size < (uintptr_t)CONFIG_STORAGE_V1_RECORD_SIZE) ||
        (b_size < (uintptr_t)CONFIG_STORAGE_V1_RECORD_SIZE))
    {
        return false;
    }

    if (!((bounds->a_end <= bounds->b_start) ||
          (bounds->b_end <= bounds->a_start)))
    {
        return false;
    }

    return true;
}

static bool WriteLayoutIsExact(const ConfigStorage_Bounds_t *bounds)
{
    return (bounds != NULL) &&
           (bounds->a_start ==
            (uintptr_t)CONFIG_STORAGE_EXPECTED_A_START) &&
           (bounds->a_end ==
            (uintptr_t)CONFIG_STORAGE_EXPECTED_A_END) &&
           (bounds->b_start ==
            (uintptr_t)CONFIG_STORAGE_EXPECTED_B_START) &&
           (bounds->b_end ==
            (uintptr_t)CONFIG_STORAGE_EXPECTED_B_END);
}

static uint32_t ReadU32LE(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static bool FlashBytesAreErased(uintptr_t address, size_t length)
{
    const uint8_t *flash = (const uint8_t *)address;

    for (size_t i = 0U; i < length; i++)
    {
        if (flash[i] != 0xFFU)
        {
            return false;
        }
    }

    return true;
}

static bool FlashBytesMatch(uintptr_t address,
                            const uint8_t *expected,
                            size_t length)
{
    const uint8_t *flash = (const uint8_t *)address;

    for (size_t i = 0U; i < length; i++)
    {
        if (flash[i] != expected[i])
        {
            return false;
        }
    }

    return true;
}

static HAL_StatusTypeDef ProgramWords(uintptr_t address,
                                      const uint8_t *data,
                                      size_t length,
                                      uint32_t *failed_address)
{
    HAL_StatusTypeDef hal_status = HAL_OK;

    for (size_t offset = 0U; offset < length; offset += 4U)
    {
        const uintptr_t program_address = address + offset;
        const uint32_t word = ReadU32LE(&data[offset]);

        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                      (uint32_t)program_address,
                                      (uint64_t)word);
        if (hal_status != HAL_OK)
        {
            if (failed_address != NULL)
            {
                *failed_address = (uint32_t)program_address;
            }
            break;
        }
    }

    return hal_status;
}

static uint32_t GetSectorForSlot(ConfigStorage_SlotId_t slot)
{
    if (slot == CONFIG_STORAGE_SLOT_A)
    {
        return FLASH_SECTOR_6;
    }
    if (slot == CONFIG_STORAGE_SLOT_B)
    {
        return FLASH_SECTOR_7;
    }

    return CONFIG_STORAGE_ERASE_SECTOR_OK;
}

static uintptr_t GetSlotStart(ConfigStorage_SlotId_t slot,
                              const ConfigStorage_Bounds_t *bounds)
{
    if (slot == CONFIG_STORAGE_SLOT_A)
    {
        return bounds->a_start;
    }
    if (slot == CONFIG_STORAGE_SLOT_B)
    {
        return bounds->b_start;
    }

    return (uintptr_t)0U;
}

static ConfigStorage_SlotState_t ClassifyRecordStatus(
    ConfigRecord_Status_t status)
{
    switch (status)
    {
        case CONFIG_RECORD_STATUS_VALID:
            return CONFIG_STORAGE_SLOT_STATE_USABLE;

        case CONFIG_RECORD_STATUS_ERASED:
            return CONFIG_STORAGE_SLOT_STATE_ERASED;

        case CONFIG_RECORD_STATUS_UNCOMMITTED:
            return CONFIG_STORAGE_SLOT_STATE_UNCOMMITTED;

        case CONFIG_RECORD_STATUS_UNSUPPORTED_STORAGE_VERSION:
        case CONFIG_RECORD_STATUS_UNSUPPORTED_RECORD_TYPE:
        case CONFIG_RECORD_STATUS_UNSUPPORTED_PAYLOAD_SCHEMA:
            return CONFIG_STORAGE_SLOT_STATE_INCOMPATIBLE;

        default:
            return CONFIG_STORAGE_SLOT_STATE_STRUCTURALLY_CORRUPT;
    }
}

static bool InspectSlotWithBounds(ConfigStorage_SlotId_t slot,
                                  const ConfigStorage_Bounds_t *bounds,
                                  ConfigStorage_SlotResult_t *result,
                                  NodeConfig_t *config)
{
    uint8_t encoded_header[CONFIG_RECORD_V1_HEADER_SIZE];
    const uint8_t *slot_bytes;
    const uint8_t *payload;
    uintptr_t slot_start;

    InitializeSlotResult(slot, result);

    slot_start = GetSlotStart(slot, bounds);
    if (slot_start == (uintptr_t)0U)
    {
        result->state = CONFIG_STORAGE_SLOT_STATE_LAYOUT_INVALID;
        return false;
    }

    slot_bytes = (const uint8_t *)slot_start;
    payload = &slot_bytes[CONFIG_RECORD_V1_HEADER_SIZE];
    (void)memcpy(encoded_header, slot_bytes, sizeof(encoded_header));

    result->record_status = ConfigRecord_ValidateV1(
        encoded_header,
        payload,
        CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE,
        &result->header);
    result->state = ClassifyRecordStatus(result->record_status);

    if ((result->record_status != CONFIG_RECORD_STATUS_ERASED) &&
        (result->record_status != CONFIG_RECORD_STATUS_INVALID_ARGUMENT))
    {
        result->header_available = true;
    }

    if (result->header_available &&
        (result->header.magic == CONFIG_RECORD_MAGIC) &&
        (result->header.storage_format_version ==
         CONFIG_RECORD_STORAGE_FORMAT_VERSION_V1) &&
        (result->header.header_length == CONFIG_RECORD_V1_HEADER_SIZE))
    {
        result->generation_available = true;
        result->generation = result->header.generation;
    }

    if (result->record_status != CONFIG_RECORD_STATUS_VALID)
    {
        return true;
    }

    if (!NodeConfig_DecodeV1(payload, config) ||
        !NodeConfig_Validate(config))
    {
        result->state = CONFIG_STORAGE_SLOT_STATE_PAYLOAD_INVALID;
        return true;
    }

    result->state = CONFIG_STORAGE_SLOT_STATE_USABLE;
    return true;
}

static bool MeaningfulRecordsAreEqual(
    const ConfigStorage_Bounds_t *bounds)
{
    const uint8_t *record_a = (const uint8_t *)bounds->a_start;
    const uint8_t *record_b = (const uint8_t *)bounds->b_start;

    for (size_t i = 0U; i < CONFIG_STORAGE_V1_RECORD_SIZE; i++)
    {
        if (record_a[i] != record_b[i])
        {
            return false;
        }
    }

    return true;
}

static ConfigStorage_LoadStatus_t UsePersistentConfig(
    const NodeConfig_t *source,
    ConfigStorage_SlotId_t slot,
    NodeConfig_t *config,
    ConfigStorage_LoadResult_t *result)
{
    *config = *source;
    result->selected_slot = slot;
    result->status = CONFIG_STORAGE_LOAD_PERSISTENT;
    return result->status;
}

static ConfigStorage_LoadStatus_t UseDefaults(
    ConfigStorage_LoadStatus_t status,
    NodeConfig_t *config,
    ConfigStorage_LoadResult_t *result)
{
    NodeConfig_SetDefaults(config);
    result->selected_slot = CONFIG_STORAGE_SLOT_NONE;
    result->status = status;
    return result->status;
}

bool ConfigStorage_InspectSlot(ConfigStorage_SlotId_t slot,
                               ConfigStorage_SlotResult_t *result,
                               NodeConfig_t *config)
{
    ConfigStorage_Bounds_t bounds;
    NodeConfig_t decoded_config;
    NodeConfig_t *config_target;

    if ((result == NULL) ||
        ((slot != CONFIG_STORAGE_SLOT_A) &&
         (slot != CONFIG_STORAGE_SLOT_B)))
    {
        return false;
    }

    InitializeSlotResult(slot, result);
    if (!GetValidatedBounds(&bounds))
    {
        result->state = CONFIG_STORAGE_SLOT_STATE_LAYOUT_INVALID;
        return false;
    }

    config_target = (config != NULL) ? config : &decoded_config;
    return InspectSlotWithBounds(slot, &bounds, result, config_target);
}

ConfigStorage_LoadStatus_t ConfigStorage_Load(
    NodeConfig_t *config,
    ConfigStorage_LoadResult_t *result)
{
    ConfigStorage_Bounds_t bounds;
    NodeConfig_t config_b;
    bool a_usable;
    bool b_usable;
    ConfigRecord_GenerationOrder_t generation_order;

    if ((config == NULL) || (result == NULL))
    {
        return CONFIG_STORAGE_LOAD_INVALID_ARGUMENT;
    }

    (void)memset(result, 0, sizeof(*result));
    result->status = CONFIG_STORAGE_LOAD_INVALID_ARGUMENT;
    result->selected_slot = CONFIG_STORAGE_SLOT_NONE;
    InitializeSlotResult(CONFIG_STORAGE_SLOT_A, &result->slot_a);
    InitializeSlotResult(CONFIG_STORAGE_SLOT_B, &result->slot_b);

    if (!GetValidatedBounds(&bounds))
    {
        result->slot_a.state = CONFIG_STORAGE_SLOT_STATE_LAYOUT_INVALID;
        result->slot_b.state = CONFIG_STORAGE_SLOT_STATE_LAYOUT_INVALID;
        return UseDefaults(CONFIG_STORAGE_LOAD_DEFAULTS_LAYOUT_INVALID,
                           config,
                           result);
    }

    (void)InspectSlotWithBounds(CONFIG_STORAGE_SLOT_A,
                                &bounds,
                                &result->slot_a,
                                config);
    (void)InspectSlotWithBounds(CONFIG_STORAGE_SLOT_B,
                                &bounds,
                                &result->slot_b,
                                &config_b);

    a_usable = result->slot_a.state == CONFIG_STORAGE_SLOT_STATE_USABLE;
    b_usable = result->slot_b.state == CONFIG_STORAGE_SLOT_STATE_USABLE;

    if (a_usable && !b_usable)
    {
        return UsePersistentConfig(config,
                                   CONFIG_STORAGE_SLOT_A,
                                   config,
                                   result);
    }
    if (b_usable && !a_usable)
    {
        return UsePersistentConfig(&config_b,
                                   CONFIG_STORAGE_SLOT_B,
                                   config,
                                   result);
    }
    if (!a_usable && !b_usable)
    {
        return UseDefaults(CONFIG_STORAGE_LOAD_DEFAULTS_NO_USABLE_SLOT,
                           config,
                           result);
    }

    generation_order = ConfigRecord_CompareGenerations(
        result->slot_a.generation,
        result->slot_b.generation);
    if (generation_order == CONFIG_RECORD_GENERATION_A_NEWER)
    {
        return UsePersistentConfig(config,
                                   CONFIG_STORAGE_SLOT_A,
                                   config,
                                   result);
    }
    if (generation_order == CONFIG_RECORD_GENERATION_A_OLDER)
    {
        return UsePersistentConfig(&config_b,
                                   CONFIG_STORAGE_SLOT_B,
                                   config,
                                   result);
    }
    if ((generation_order == CONFIG_RECORD_GENERATION_EQUAL) &&
        MeaningfulRecordsAreEqual(&bounds))
    {
        return UsePersistentConfig(config,
                                   CONFIG_STORAGE_SLOT_A,
                                   config,
                                   result);
    }

    return UseDefaults(CONFIG_STORAGE_LOAD_DEFAULTS_CONFLICT,
                       config,
                       result);
}

static void InitializeSaveResult(ConfigStorage_SaveResult_t *result)
{
    (void)memset(result, 0, sizeof(*result));
    result->status = CONFIG_STORAGE_SAVE_INVALID_ARGUMENT;
    result->operation_status = CONFIG_STORAGE_SAVE_INVALID_ARGUMENT;
    result->previous_active_slot = CONFIG_STORAGE_SLOT_NONE;
    result->target_slot = CONFIG_STORAGE_SLOT_NONE;
    result->target_sector = CONFIG_STORAGE_ERASE_SECTOR_OK;
    result->erase_sector_error = CONFIG_STORAGE_ERASE_SECTOR_OK;
    result->final_slot_state = CONFIG_STORAGE_SLOT_STATE_NOT_INSPECTED;
    result->final_record_status = CONFIG_RECORD_STATUS_INVALID_ARGUMENT;
}

static ConfigStorage_SaveStatus_t FinishBeforeUnlock(
    ConfigStorage_SaveStatus_t status,
    ConfigStorage_SaveResult_t *result)
{
    result->operation_status = status;
    result->status = status;
    return status;
}

ConfigStorage_SaveStatus_t ConfigStorage_Save(
    const NodeConfig_t *config,
    ConfigStorage_SaveResult_t *result)
{
    static ConfigStorage_SaveWorkspace_t workspace;
    ConfigStorage_Bounds_t bounds;
    FLASH_EraseInitTypeDef erase_init;
    ConfigStorage_LoadStatus_t load_status;
    ConfigStorage_SaveStatus_t operation_status;
    ConfigStorage_SlotResult_t *active_result;
    uintptr_t target_start;
    const uint8_t *flash_payload;
    uint32_t calculated_crc;
    uint32_t commit_word;
    HAL_StatusTypeDef hal_status;
    bool final_inspection_completed;

    if (result == NULL)
    {
        return CONFIG_STORAGE_SAVE_INVALID_ARGUMENT;
    }

    InitializeSaveResult(result);
    if (config == NULL)
    {
        return result->status;
    }

    if (!NodeConfig_Validate(config))
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_INVALID_NODE_CONFIG,
                                  result);
    }

    if (!NodeConfig_EncodeV1(config, workspace.payload))
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_ENCODE_FAILURE,
                                  result);
    }

    load_status = ConfigStorage_Load(&workspace.decoded_config,
                                     &workspace.load_result);
    result->slot_a_before = workspace.load_result.slot_a;
    result->slot_b_before = workspace.load_result.slot_b;

    if (load_status == CONFIG_STORAGE_LOAD_DEFAULTS_LAYOUT_INVALID)
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_LAYOUT_UNSAFE, result);
    }
    if (load_status == CONFIG_STORAGE_LOAD_DEFAULTS_CONFLICT)
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_EXISTING_AB_CONFLICT,
                                  result);
    }
    if (load_status == CONFIG_STORAGE_LOAD_INVALID_ARGUMENT)
    {
        return FinishBeforeUnlock(
            CONFIG_STORAGE_SAVE_SLOT_INSPECTION_FAILURE,
            result);
    }

    if (!GetValidatedBounds(&bounds) || !WriteLayoutIsExact(&bounds))
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_LAYOUT_UNSAFE, result);
    }

    if (load_status == CONFIG_STORAGE_LOAD_DEFAULTS_NO_USABLE_SLOT)
    {
        result->previous_active_slot = CONFIG_STORAGE_SLOT_NONE;
        result->target_slot = CONFIG_STORAGE_SLOT_A;
        result->new_generation = UINT64_C(1);
    }
    else if (load_status == CONFIG_STORAGE_LOAD_PERSISTENT)
    {
        result->previous_active_slot = workspace.load_result.selected_slot;
        if (result->previous_active_slot == CONFIG_STORAGE_SLOT_A)
        {
            active_result = &workspace.load_result.slot_a;
            result->target_slot = CONFIG_STORAGE_SLOT_B;
        }
        else if (result->previous_active_slot == CONFIG_STORAGE_SLOT_B)
        {
            active_result = &workspace.load_result.slot_b;
            result->target_slot = CONFIG_STORAGE_SLOT_A;
        }
        else
        {
            return FinishBeforeUnlock(
                CONFIG_STORAGE_SAVE_SLOT_INSPECTION_FAILURE,
                result);
        }

        if ((active_result->state != CONFIG_STORAGE_SLOT_STATE_USABLE) ||
            !active_result->generation_available)
        {
            return FinishBeforeUnlock(
                CONFIG_STORAGE_SAVE_SLOT_INSPECTION_FAILURE,
                result);
        }
        result->new_generation = active_result->generation + UINT64_C(1);
    }
    else
    {
        return FinishBeforeUnlock(
            CONFIG_STORAGE_SAVE_SLOT_INSPECTION_FAILURE,
            result);
    }

    target_start = GetSlotStart(result->target_slot, &bounds);
    result->target_sector = GetSectorForSlot(result->target_slot);
    if ((target_start == (uintptr_t)0U) ||
        ((target_start & (uintptr_t)0x3U) != (uintptr_t)0U) ||
        (result->target_sector == CONFIG_STORAGE_ERASE_SECTOR_OK))
    {
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_LAYOUT_UNSAFE, result);
    }

    if (!ConfigRecord_InitV1Header(
            &workspace.header,
            result->new_generation,
            CONFIG_RECORD_PAYLOAD_SCHEMA_VERSION_V1,
            CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE) ||
        !ConfigRecord_FinalizeV1(&workspace.header, workspace.payload) ||
        !ConfigRecord_EncodeHeaderV1(&workspace.header,
                                     workspace.encoded_header) ||
        (ReadU32LE(&workspace.encoded_header[
             CONFIG_STORAGE_COMMIT_OFFSET]) !=
         CONFIG_RECORD_COMMIT_MARKER))
    {
        return FinishBeforeUnlock(
            CONFIG_STORAGE_SAVE_HEADER_PREPARATION_FAILURE,
            result);
    }

    hal_status = HAL_FLASH_Unlock();
    result->unlock_hal_status = (uint32_t)hal_status;
    if (hal_status != HAL_OK)
    {
        result->flash_error = HAL_FLASH_GetError();
        return FinishBeforeUnlock(CONFIG_STORAGE_SAVE_FLASH_UNLOCK_FAILURE,
                                  result);
    }

    operation_status = CONFIG_STORAGE_SAVE_SUCCESS;
    (void)memset(&erase_init, 0, sizeof(erase_init));
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = result->target_sector;
    erase_init.NbSectors = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    hal_status = HAL_FLASHEx_Erase(&erase_init,
                                   &result->erase_sector_error);
    result->erase_hal_status = (uint32_t)hal_status;
    if ((hal_status != HAL_OK) ||
        (result->erase_sector_error != CONFIG_STORAGE_ERASE_SECTOR_OK))
    {
        operation_status = CONFIG_STORAGE_SAVE_SECTOR_ERASE_FAILURE;
        goto lock_flash;
    }

    if (!FlashBytesAreErased(target_start,
                             CONFIG_STORAGE_V1_RECORD_SIZE))
    {
        operation_status = CONFIG_STORAGE_SAVE_ERASE_VERIFICATION_FAILURE;
        goto lock_flash;
    }

    hal_status = ProgramWords(target_start,
                              workspace.encoded_header,
                              CONFIG_STORAGE_HEADER_PROGRAM_SIZE,
                              &result->failed_program_address);
    result->program_hal_status = (uint32_t)hal_status;
    if (hal_status != HAL_OK)
    {
        operation_status = CONFIG_STORAGE_SAVE_HEADER_PROGRAM_FAILURE;
        goto lock_flash;
    }

    hal_status = ProgramWords(
        target_start + CONFIG_RECORD_V1_HEADER_SIZE,
        workspace.payload,
        NODE_CONFIG_V1_PAYLOAD_SIZE,
        &result->failed_program_address);
    result->program_hal_status = (uint32_t)hal_status;
    if (hal_status != HAL_OK)
    {
        operation_status = CONFIG_STORAGE_SAVE_PAYLOAD_PROGRAM_FAILURE;
        goto lock_flash;
    }

    FLASH_FlushCaches();

    (void)memcpy(workspace.readback_header,
                 (const void *)target_start,
                 sizeof(workspace.readback_header));
    if (!FlashBytesMatch(target_start,
                         workspace.encoded_header,
                         CONFIG_STORAGE_HEADER_PROGRAM_SIZE) ||
        !FlashBytesMatch(target_start + CONFIG_RECORD_V1_HEADER_SIZE,
                         workspace.payload,
                         NODE_CONFIG_V1_PAYLOAD_SIZE) ||
        (ReadU32LE(&workspace.readback_header[
             CONFIG_STORAGE_COMMIT_OFFSET]) !=
         CONFIG_RECORD_ERASED_FLASH_WORD))
    {
        operation_status = CONFIG_STORAGE_SAVE_READBACK_VERIFICATION_FAILURE;
        goto lock_flash;
    }

    flash_payload = (const uint8_t *)(
        target_start + CONFIG_RECORD_V1_HEADER_SIZE);
    if (!ConfigRecord_DecodeHeaderV1(workspace.readback_header,
                                     &workspace.readback_decoded_header) ||
        !ConfigRecord_CalculateRecordCrcV1(
            &workspace.readback_decoded_header,
            flash_payload,
            &calculated_crc) ||
        (calculated_crc != workspace.readback_decoded_header.crc32))
    {
        operation_status = CONFIG_STORAGE_SAVE_CRC_VERIFICATION_FAILURE;
        goto lock_flash;
    }

    hal_status = ProgramWords(
        target_start + CONFIG_STORAGE_COMMIT_OFFSET,
        &workspace.encoded_header[CONFIG_STORAGE_COMMIT_OFFSET],
        4U,
        &result->failed_program_address);
    result->program_hal_status = (uint32_t)hal_status;
    if (hal_status != HAL_OK)
    {
        operation_status = CONFIG_STORAGE_SAVE_COMMIT_PROGRAM_FAILURE;
        goto lock_flash;
    }

    FLASH_FlushCaches();

    commit_word = ReadU32LE((const uint8_t *)(
        target_start + CONFIG_STORAGE_COMMIT_OFFSET));
    if (commit_word != CONFIG_RECORD_COMMIT_MARKER)
    {
        operation_status = CONFIG_STORAGE_SAVE_COMMIT_PROGRAM_FAILURE;
    }

lock_flash:
    result->operation_status = operation_status;
    result->flash_error = HAL_FLASH_GetError();
    hal_status = HAL_FLASH_Lock();
    result->lock_hal_status = (uint32_t)hal_status;

    final_inspection_completed = InspectSlotWithBounds(
        result->target_slot,
        &bounds,
        &workspace.final_slot_result,
        &workspace.decoded_config);
    if (final_inspection_completed)
    {
        result->final_slot_state = workspace.final_slot_result.state;
        result->final_record_status =
            workspace.final_slot_result.record_status;
    }

    if (hal_status != HAL_OK)
    {
        result->status = CONFIG_STORAGE_SAVE_FLASH_LOCK_FAILURE;
        return result->status;
    }

    result->status = operation_status;
    if (operation_status != CONFIG_STORAGE_SAVE_SUCCESS)
    {
        return result->status;
    }

    if (!final_inspection_completed ||
        (workspace.final_slot_result.state !=
         CONFIG_STORAGE_SLOT_STATE_USABLE) ||
        !NodeConfig_EncodeV1(&workspace.decoded_config,
                             workspace.comparison_payload) ||
        (memcmp(workspace.payload,
                workspace.comparison_payload,
                NODE_CONFIG_V1_PAYLOAD_SIZE) != 0))
    {
        result->operation_status =
            CONFIG_STORAGE_SAVE_FINAL_SLOT_VALIDATION_FAILURE;
        result->status = CONFIG_STORAGE_SAVE_FINAL_SLOT_VALIDATION_FAILURE;
    }

    return result->status;
}
