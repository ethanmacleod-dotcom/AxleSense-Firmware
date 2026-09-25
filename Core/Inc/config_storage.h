#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "config_record.h"
#include "node_config.h"

#define CONFIG_STORAGE_V1_RECORD_SIZE \
    (CONFIG_RECORD_V1_HEADER_SIZE + CONFIG_RECORD_AXLESENSE_V1_PAYLOAD_SIZE)

typedef enum
{
    CONFIG_STORAGE_SLOT_NONE = 0,
    CONFIG_STORAGE_SLOT_A,
    CONFIG_STORAGE_SLOT_B
} ConfigStorage_SlotId_t;

typedef enum
{
    CONFIG_STORAGE_SLOT_STATE_NOT_INSPECTED = 0,
    CONFIG_STORAGE_SLOT_STATE_LAYOUT_INVALID,
    CONFIG_STORAGE_SLOT_STATE_ERASED,
    CONFIG_STORAGE_SLOT_STATE_UNCOMMITTED,
    CONFIG_STORAGE_SLOT_STATE_STRUCTURALLY_CORRUPT,
    CONFIG_STORAGE_SLOT_STATE_INCOMPATIBLE,
    CONFIG_STORAGE_SLOT_STATE_PAYLOAD_INVALID,
    CONFIG_STORAGE_SLOT_STATE_USABLE
} ConfigStorage_SlotState_t;

typedef struct
{
    ConfigStorage_SlotId_t slot;
    ConfigStorage_SlotState_t state;
    ConfigRecord_Status_t record_status;
    bool header_available;
    ConfigRecord_Header_t header;
    bool generation_available;
    uint64_t generation;
} ConfigStorage_SlotResult_t;

typedef enum
{
    CONFIG_STORAGE_LOAD_PERSISTENT = 0,
    CONFIG_STORAGE_LOAD_DEFAULTS_NO_USABLE_SLOT,
    CONFIG_STORAGE_LOAD_DEFAULTS_LAYOUT_INVALID,
    CONFIG_STORAGE_LOAD_DEFAULTS_CONFLICT,
    CONFIG_STORAGE_LOAD_INVALID_ARGUMENT
} ConfigStorage_LoadStatus_t;

typedef struct
{
    ConfigStorage_LoadStatus_t status;
    ConfigStorage_SlotId_t selected_slot;
    ConfigStorage_SlotResult_t slot_a;
    ConfigStorage_SlotResult_t slot_b;
} ConfigStorage_LoadResult_t;

typedef enum
{
    CONFIG_STORAGE_SAVE_SUCCESS = 0,
    CONFIG_STORAGE_SAVE_INVALID_ARGUMENT,
    CONFIG_STORAGE_SAVE_INVALID_NODE_CONFIG,
    CONFIG_STORAGE_SAVE_LAYOUT_UNSAFE,
    CONFIG_STORAGE_SAVE_EXISTING_AB_CONFLICT,
    CONFIG_STORAGE_SAVE_SLOT_INSPECTION_FAILURE,
    CONFIG_STORAGE_SAVE_ENCODE_FAILURE,
    CONFIG_STORAGE_SAVE_HEADER_PREPARATION_FAILURE,
    CONFIG_STORAGE_SAVE_FLASH_UNLOCK_FAILURE,
    CONFIG_STORAGE_SAVE_SECTOR_ERASE_FAILURE,
    CONFIG_STORAGE_SAVE_ERASE_VERIFICATION_FAILURE,
    CONFIG_STORAGE_SAVE_HEADER_PROGRAM_FAILURE,
    CONFIG_STORAGE_SAVE_PAYLOAD_PROGRAM_FAILURE,
    CONFIG_STORAGE_SAVE_READBACK_VERIFICATION_FAILURE,
    CONFIG_STORAGE_SAVE_CRC_VERIFICATION_FAILURE,
    CONFIG_STORAGE_SAVE_COMMIT_PROGRAM_FAILURE,
    CONFIG_STORAGE_SAVE_FINAL_SLOT_VALIDATION_FAILURE,
    CONFIG_STORAGE_SAVE_FLASH_LOCK_FAILURE
} ConfigStorage_SaveStatus_t;

typedef struct
{
    ConfigStorage_SaveStatus_t status;
    ConfigStorage_SaveStatus_t operation_status;
    ConfigStorage_SlotId_t previous_active_slot;
    ConfigStorage_SlotId_t target_slot;
    uint64_t new_generation;
    uint32_t target_sector;
    uint32_t unlock_hal_status;
    uint32_t erase_hal_status;
    uint32_t program_hal_status;
    uint32_t lock_hal_status;
    uint32_t flash_error;
    uint32_t erase_sector_error;
    uint32_t failed_program_address;
    ConfigStorage_SlotResult_t slot_a_before;
    ConfigStorage_SlotResult_t slot_b_before;
    ConfigStorage_SlotState_t final_slot_state;
    ConfigRecord_Status_t final_record_status;
} ConfigStorage_SaveResult_t;

/* Returns false only for an invalid request or invalid linker slot layout.
 * The decoded config output is optional and is written only for a usable slot.
 */
bool ConfigStorage_InspectSlot(ConfigStorage_SlotId_t slot,
                               ConfigStorage_SlotResult_t *result,
                               NodeConfig_t *config);

/* Loads the selected persistent configuration or safe compiled defaults.
 * The return value and result identify the source or reason defaults were used.
 */
ConfigStorage_LoadStatus_t ConfigStorage_Load(
    NodeConfig_t *config,
    ConfigStorage_LoadResult_t *result);

/* Blocking maintenance operation. The caller must intentionally pause
 * acquisition and time-sensitive communications before invoking this API.
 */
ConfigStorage_SaveStatus_t ConfigStorage_Save(
    const NodeConfig_t *config,
    ConfigStorage_SaveResult_t *result);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORAGE_H */
