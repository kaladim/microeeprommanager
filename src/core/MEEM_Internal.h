/*!
 * \file    MEEM_Internal.h
 * \brief   Internally visible defs, types and operations.
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_INTERNAL_H
#define MEEM_INTERNAL_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include "MEEM_Linkage.h"
#include "MEEM_GenConfig.h"
#include "MEEM_UserCallbacks.h"
#include "MEEM_Checksum.h"
#include "MEEM_EEAIF.h"

/******************************************************************************/
/*    Internal macros                                                         */
/******************************************************************************/
#define MEEM_INVALID_PROFILE_INSTANCE 0xF /* Set to 0xFF if you need more than 14 instances in the future. */

/******************************************************************************/
/*    Internal types                                                          */
/******************************************************************************/
/** General status */
typedef enum {
    MEEM_OK = 0,
    MEEM_NOK,
    MEEM_BUSY
} MEEM_status_t;

/** Block management types */
typedef enum {
    MEEM_MGMT_BASIC,
    MEEM_MGMT_BACKUP_COPY,
    MEEM_MGMT_MULTI_PROFILE,
    MEEM_MGMT_WEAR_LEVELING
} MEEM_blockManagementType_t;

/** Data recovery strategy in case of initialization failure */
typedef enum {
    MEEM_RECOVER_DEFAULTS_AND_REPAIR = 0,
    MEEM_RECOVER_DEFAULTS            = 1
} MEEM_dataRecoveryStrategy_t;

typedef enum {
    MEEM_OPR_NONE,
    MEEM_OPR_INIT,
    MEEM_OPR_WRITE
} MEEM_currentOperation_t;

/** Initialization stages */
typedef enum {
    MEEM_INIT_PREPARE,
    MEEM_INIT_FETCH_INSTANCE,
    MEEM_INIT_EVALUATE_INSTANCE,
    MEEM_INIT_ANALYZE,
    MEEM_INIT_CACHE,
    MEEM_INIT_RECOVER_DATA,
    MEEM_INIT_READY
} MEEM_initStage_t;

/** Read/Write stages */
typedef enum {
    MEEM_IO_INITIATE,
    MEEM_IO_WAITING,
    MEEM_IO_FINALIZE,
    MEEM_IO_COMPLETE
} MEEM_ioStage_t;

typedef struct {
    MEEM_currentOperation_t current_operation;
    uint8_t                 block_id;              /**< ID of currently processed block */
    uint8_t                 next_block_to_process; /**< ID of next block to process */

    union {
        MEEM_ioStage_t   write_stage;
        MEEM_initStage_t init_stage;
    };
    uint8_t accept_new_requests : 1;

    /** Read/write request */
    struct {
        uint8_t*       data;
        uint16_t       offset_in_eeprom;
        uint16_t       size;
        MEEM_ioStage_t stage;
        MEEM_status_t  status;
    } io_request;
} MEEM_globalStatus_t;

/** Runtime block status */
typedef struct {
    uint8_t recovered                : 1; /**< Set by the core once if initialization fails and the cache is populated with defaults */
    uint8_t write_complete           : 1; /**< Set by the core when a write operation completes. Cleared at the start of operation */
    uint8_t write_failed             : 1; /**< Set once by the core when a write operation fails */
    uint8_t write_pending            : 1; /**< Set by the user to initiate a write in the EEPROM */
    uint8_t fetch_pending            : 1; /**< Set by the core when the user requests a read from the EEPROM. */
    uint8_t reserved_0               : 3;
    uint8_t index_of_active_instance : 4;
    uint8_t reserved_1               : 4;
} MEEM_blockStatusPrivate_t;

/** Block's static configuration */
typedef struct {
    uint8_t*       cache;
    const uint8_t* defaults;
    uint16_t       offset_in_eeprom;
    uint16_t       data_size;
    uint8_t        default_pattern_length; /**< Length of default pattern, bytes  */
    uint8_t        instance_count         : 4;
    uint8_t        management_type        : 2;
    uint8_t        data_recovery_strategy : 2; /**< Actions taken on init failure */
} MEEM_blockConfig_t;

/******************************************************************************/
/*    Internal variables                                                      */
/******************************************************************************/
EXTERN_C MEEM_globalStatus_t       MEEM_global_status;
EXTERN_C MEEM_blockStatusPrivate_t MEEM_block_status[MEEM_BLOCK_COUNT];
EXTERN_C uint8_t                   MEEM_work_buffer[MEEM_WORKBUFFER_SIZE];

/******************************************************************************/
/*    Internal constants                                                      */
/******************************************************************************/
EXTERN_C const MEEM_blockConfig_t MEEM_block_config[MEEM_BLOCK_COUNT];

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/* Block read/initialization-related operations */
EXTERN_C bool          MEEM_IsDataValid(uint8_t block_id);
EXTERN_C void          MEEM_RecoverBlockData(uint8_t block_id);
EXTERN_C void          MEEM_StartReadOperation(uint8_t block_id);
EXTERN_C void          MEEM_InitializeBasicBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeBackupCopyBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeWearLevelingBlock(uint8_t block_id);
EXTERN_C uint8_t       MEEM_FindIndexOfMostRecentInstance(const uint8_t sequence_counters[], uint8_t instance_count);
EXTERN_C bool          MEEM_InitMultiProfileBlockTask(void);
EXTERN_C MEEM_status_t MEEM_ReadOperationTask(void);

/* Block write-related operations */
EXTERN_C void           MEEM_StartWriteOperationCachedBlock(uint8_t block_id);
EXTERN_C void           MEEM_CalculateAndSetChecksum(void);
EXTERN_C void           MEEM_WriteInitiate(void);
EXTERN_C MEEM_ioStage_t MEEM_WriteWaitToComplete(void);
EXTERN_C MEEM_ioStage_t MEEM_WriteFinalize(void);
EXTERN_C bool           MEEM_WriteTask(void);

EXTERN_C uint8_t MEEM_IncrementAndWrapAround(uint8_t number, uint8_t exclusive_upper_limit);

/* Generated */
EXTERN_C void MEEM_ValidateConfiguration(void);

#endif /* MEEM_INTERNAL_H */
