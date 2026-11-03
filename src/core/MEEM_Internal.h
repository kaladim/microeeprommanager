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
#include "MEEM_EEAIF.h"

/******************************************************************************/
/*    Internal macros                                                         */
/******************************************************************************/
#define MEEM_INVALID_PROFILE_INSTANCE 0xF /* Set to 0xFF if you need more than 14 instances in the future. */

/* MEEM_FindIndexOfMostRecentInstance(): the sequence counter of an invalid 'wear-leveling' instance, and the result if no instance is valid */
#define MEEM_INVALID_WL_INSTANCE 0xFFu

/* Entries of a legacy model's source table, besides an index into its legacy block array (0x00 ... 0xFC): what MEEM_Init() does with a block */
#define MEEM_SOURCE_ROUTINE   0xFDu /**< Another parameter list: the generated routine converts the source, through MEEM_MigrateBlock() */
#define MEEM_SOURCE_NONE      0xFEu /**< No source, a new or renamed block: it gets its defaults */
#define MEEM_SOURCE_UNCHANGED 0xFFu /**< The same block descriptor at the same offset: the block is initialized as in v1 */

/* The data model whose identity header the EEPROM starts with, besides the index of a legacy model */
#define MEEM_MODEL_CURRENT 0xFFu /**< The current one, or the header couldn't be read: the blocks are initialized as in v1 */
#define MEEM_MODEL_UNKNOWN 0xFEu /**< None that the generator got: every block is reset */

/* The direction of a block in the order of pass 2, which follows a legacy model's source table with MEEM_LEGACY_USING_PASS_2 */
#define MEEM_PASS2_ASCENDING  0u /**< Profile 0 first */
#define MEEM_PASS2_DESCENDING 1u /**< The highest profile first */

/* Values of the generated MEEM_CRC_IMPLEMENTATION: how the instance checksum is calculated */
#define MEEM_CRC_BITWISE  1 /**< Built-in routine without a table */
#define MEEM_CRC_TABLE    2 /**< Built-in routine with the generated table MEEM_crc_table */
#define MEEM_CRC_HARDWARE 3 /**< The MCU's CRC unit */

#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_HARDWARE)
#include "MEEM_CRCIF.h"
#endif

/******************************************************************************/
/*    Internal types                                                          */
/******************************************************************************/
/** Instance checksum: CRC-16/IBM-3740, the same for every data model */
typedef uint16_t MEEM_checksum_t;

/** General status */
typedef enum {
    MEEM_OK = 0,
    MEEM_NOK,
    MEEM_BUSY,
    MEEM_INVALID /**< Read, but the instance failed its check */
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
    MEEM_OPR_WRITE,
    MEEM_OPR_COMMIT /**< Write of the identity header after a reset or a migration */
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
    MEEM_IO_INITIATE,           /**< Read: begin the EEPROM read of an instance, which is checked after the read */
    MEEM_IO_INITIATE_UNCHECKED, /**< Read: begin the EEPROM read of a region that isn't checked, such as the data of an instance only */
    MEEM_IO_WAITING,
    MEEM_IO_WAITING_UNCHECKED,
    MEEM_IO_BEGIN_CHECKSUM,   /**< Read: check the instance. Write: set the checksum and begin the write. The built-in CRC completes within this stage. */
    MEEM_IO_WAITING_CHECKSUM, /**< Hardware CRC only: wait for the result of the CRC unit */
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
    uint8_t remaining_instances : 4; /**< Write: count of instances still to write after the current one (both copies, or a rewrite) */
    uint8_t accept_new_requests : 1;
    uint8_t identity_pending    : 1; /**< The identity header is to be written once no rewrite is pending. Cleared when its write starts */

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
    uint8_t recovered                  : 1; /**< Set by the core once if initialization fails and the cache is populated with defaults */
    uint8_t write_complete             : 1; /**< Set by the core when a write operation completes. Cleared at the start of operation */
    uint8_t write_failed               : 1; /**< Set once by the core when a write operation fails */
    uint8_t write_pending              : 1; /**< Set by the user to initiate a write in the EEPROM */
    uint8_t fetch_pending              : 1; /**< Set by the core when the user requests a read from the EEPROM. */
    uint8_t migrated                   : 1; /**< Set once by the core when MEEM_Init() fills the cache from a legacy model */
    uint8_t reserved_0                 : 1;
    uint8_t rewrite_all                : 1; /**< Set with write_pending: the write covers every instance (a rewrite). Cleared when it completes */
    uint8_t index_of_active_instance   : 4;
    uint8_t index_of_requested_profile : 4; /**< Multi-profile blocks only: target of a requested switch. Becomes active when its fetch starts */
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

/** A legacy block that the core reads: the source of a copied block, or of a block that a routine converts */
typedef struct {
    uint16_t offset_in_eeprom;
    uint16_t data_size;
    uint8_t  instance_count  : 4;
    uint8_t  management_type : 2;
} MEEM_legacyBlockConfig_t;

/** A legacy model, from which MEEM_Init() migrates when the EEPROM starts with its identity header.
 *  Its source table holds MEEM_BLOCK_COUNT entries, by the current block ID. With MEEM_LEGACY_USING_PASS_2, the order of pass 2 follows
 *  them: a count, then per block its ID and MEEM_PASS2_ASCENDING or MEEM_PASS2_DESCENDING. */
typedef struct {
    uint8_t                         identity_header[6];
    const uint8_t*                  sources; /**< The source table: per current block an index into blocks, or MEEM_SOURCE_* */
    const MEEM_legacyBlockConfig_t* blocks;  /**< Only the sources that the core reads. NULL if it reads none. */
} MEEM_legacyModel_t;

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
EXTERN_C void          MEEM_RecoverBlockData(uint8_t block_id);
EXTERN_C void          MEEM_RequestRewrite(uint8_t block_id);
EXTERN_C void          MEEM_StartReadOperation(uint8_t block_id);
EXTERN_C void          MEEM_InitializeBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeBasicBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeBackupCopyBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeWearLevelingBlock(uint8_t block_id);
EXTERN_C void          MEEM_InitializeMultiProfileBlock(uint8_t block_id);
EXTERN_C uint8_t       MEEM_FindIndexOfMostRecentInstance(const uint8_t sequence_counters[], uint8_t instance_count);
EXTERN_C bool          MEEM_InitMultiProfileBlockTask(void);
EXTERN_C MEEM_status_t MEEM_ReadOperationTask(void);

#if (MEEM_USING_MULTI_PROFILE_BLOCKS == true)
EXTERN_C void MEEM_SelectInitialProfile(uint8_t block_id);
#else
#define MEEM_SelectInitialProfile(block_id) /* No block has profiles */
#endif

/* Block write-related operations */
EXTERN_C void           MEEM_StartWriteOperationCachedBlock(uint8_t block_id);
EXTERN_C void           MEEM_WriteInitiate(void);
EXTERN_C MEEM_ioStage_t MEEM_WriteWaitToComplete(void);
EXTERN_C MEEM_ioStage_t MEEM_WriteFinalize(void);
EXTERN_C bool           MEEM_WriteTask(void);

EXTERN_C uint8_t MEEM_IncrementAndWrapAround(uint8_t number, uint8_t exclusive_upper_limit);

/* Readers of the legacy sources and the migration. Compiled only with legacy models. */
EXTERN_C const uint8_t* MEEM_ReadLegacyInstance(const MEEM_legacyBlockConfig_t* source, uint8_t profile);
EXTERN_C bool           MEEM_CopyLegacyBlock(uint8_t block_id, const MEEM_legacyBlockConfig_t* source, uint8_t profile);
EXTERN_C uint8_t        MEEM_FindLegacyModel(void);
EXTERN_C void           MEEM_MigrateFromLegacyModel(uint8_t model);

/* Checksum */
#if (MEEM_CRC_IMPLEMENTATION == MEEM_CRC_HARDWARE)
EXTERN_C MEEM_status_t MEEM_ChecksumTask(MEEM_ioStage_t* stage, MEEM_checksum_t* checksum);
#else
EXTERN_C MEEM_checksum_t MEEM_CalculateChecksum(const uint8_t* data, uint16_t data_size);
#endif

/* Generated */
EXTERN_C void MEEM_ValidateConfiguration(void);

#endif /* MEEM_INTERNAL_H */
