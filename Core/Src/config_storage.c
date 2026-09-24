#include "config_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint8_t __config_a_start__;
extern uint8_t __config_a_end__;
extern uint8_t __config_b_start__;
extern uint8_t __config_b_end__;

_Static_assert(CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE ==
               NODE_CONFIG_V1_PAYLOAD_SIZE,
               "Record and node payload sizes must match");
_Static_assert(CONFIG_STORAGE_V1_RECORD_SIZE == 304U,
               "Frozen V1 meaningful record size must be 304 bytes");

typedef struct
{
    uintptr_t a_start;
    uintptr_t a_end;
    uintptr_t b_start;
    uintptr_t b_end;
} ConfigStorage_Bounds_t;

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
