/*!
 * \file    MEEM_Legacy.c
 * \brief   Migration from a legacy model in MEEM_Init(): the readers of the legacy sources, the blocks of a legacy model that a current
 *          block is copied or converted from, and the passes that fill the caches from them.
 *          A source is read according to its old management type, at its old offset and with its old size.
 *          Compiled only with legacy models, only the readers of the management types that their sources have (MEEM_LEGACY_USING_*),
 *          and pass 2 only if a legacy model has a block in it (MEEM_LEGACY_USING_PASS_2).
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2026 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_GenConfig.h"
#include "MEEM_Internal.h"
#include "MEEM_Migration.h"
#include "MEEM.h"
#include <string.h>
#include <assert.h>

#if (MEEM_LEGACY_MODEL_COUNT > 0u)
/******************************************************************************/
/*    Private operations                                                      */
/******************************************************************************/
/*!
 * \brief     Reads an instance of a legacy source into the work buffer, and checks it.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] source - the legacy block
 * \param[in] instance_index - the copy, profile or 'wear-leveling' instance to read
 * \retval    true if the instance was read and is valid
 * \retval    false if it couldn't be read, or it failed its check
 */
static bool MEEM_FetchLegacyInstance(const MEEM_legacyBlockConfig_t* source, uint8_t instance_index)
{
    MEEM_status_t read_status;

    MEEM_global_status.io_request.size             = sizeof(MEEM_checksum_t) + source->data_size;
    MEEM_global_status.io_request.offset_in_eeprom = source->offset_in_eeprom + (MEEM_global_status.io_request.size * (uint16_t) instance_index);
    MEEM_global_status.io_request.data             = MEEM_work_buffer;
    MEEM_global_status.io_request.stage            = MEEM_IO_INITIATE;
    MEEM_global_status.io_request.status           = MEEM_BUSY;

    do
    {
        read_status = MEEM_ReadOperationTask();
    } while (MEEM_BUSY == read_status);

    return (MEEM_OK == read_status);
}

#if (MEEM_LEGACY_USING_PASS_2 == true)
/*!
 * \brief     Writes the cache of a multi-profile block to one of its profiles. Unlike a write of the periodic task, it calls no write
 *            callback and leaves 'write_complete' clear, as v1's init does. A failed write sets 'write_failed'.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] block_id - ID of the block
 * \param[in] profile - the profile to write, which becomes the active one
 */
static void MEEM_WriteProfile(uint8_t block_id, uint8_t profile)
{
    MEEM_block_status[block_id].index_of_active_instance = profile; /* A write goes to the active profile */
    MEEM_StartWriteOperationCachedBlock(block_id);

    while (false == MEEM_WriteTask())
    {
        EEAIF_Task(); /* As the periodic task does after each step */
    }

    MEEM_block_status[block_id].write_complete = false;
}
#endif

/*!
 * \brief     Restores the defaults of a block, then fills its cache from its source in a legacy model, as the block's entry in the model's
 *            source table says: a copy of the legacy block that it indexes, or the conversion of the generated routine. The parameters that
 *            a routine doesn't set keep their defaults.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] model - index of the legacy model. Not used without a source.
 * \param[in] block_id - ID of the current block
 * \param[in] source - the block's entry in the source table: an index into the legacy blocks, MEEM_SOURCE_ROUTINE or MEEM_SOURCE_NONE
 * \param[in] profile - the profile to read of a multi-profile source. Ignored for another management type.
 * \retval    true if the cache was filled from the legacy data
 * \retval    false if the block has no source, or its source has no valid instance: the cache holds the defaults
 */
static bool MEEM_FillFromLegacySource(uint8_t model, uint8_t block_id, uint8_t source, uint8_t profile)
{
    bool filled = false;

    MEEM_RestoreDefaults(block_id);

    if (source < MEEM_SOURCE_ROUTINE)
    {
        filled = MEEM_CopyLegacyBlock(block_id, &MEEM_legacy_models[model].blocks[source], profile);
    }
    else if (MEEM_SOURCE_ROUTINE == source)
    {
        filled = MEEM_MigrateBlock(model, block_id, profile);
    }

    return filled;
}

#if (MEEM_LEGACY_USING_PASS_2 == true)
/*!
 * \brief     Tells whether pass 2 migrates a block from a legacy model: whether the order of pass 2, which follows the model's source
 *            table, lists it. Pass 1 leaves such a block out.
 * \param[in] sources - the source table of the legacy model
 * \param[in] block_id - ID of the current block
 * \retval    true if pass 2 migrates it
 * \retval    false if pass 1 does
 */
static bool MEEM_IsInPass2(const uint8_t* sources, uint8_t block_id)
{
    const uint8_t* order = &sources[MEEM_BLOCK_COUNT + 1u]; /* After the count: per block its ID and its direction */

    for (uint8_t n = sources[MEEM_BLOCK_COUNT]; n > 0u; n--, order += 2)
    {
        if (block_id == order[0])
        {
            return true;
        }
    }
    return false;
}
#endif

#if (MEEM_LEGACY_USING_PASS_2 == true)
/*!
 * \brief     Pass 2 of a migration from a legacy model: the multi-profile blocks whose sources are multi-profile blocks too, in the order that
 *            follows the model's source table. Only the active profile fits the cache, so the profiles are filled and written one by one,
 *            in the generated direction, which reads each source profile before a write can overwrite it. Then the active profile is
 *            selected, and loaded from the new place as in v1.
 *            A block is 'migrated' if any of its profiles came from the legacy data, else 'recovered'. It needs no rewrite.
 * \note      This is a synchronous (blocking) operation! It lengthens MEEM_Init() by the write time of these profiles.
 * \param[in] model - index of the legacy model
 */
static void MEEM_MigratePass2(uint8_t model)
{
    const uint8_t* sources = MEEM_legacy_models[model].sources;
    const uint8_t* order   = &sources[MEEM_BLOCK_COUNT + 1u]; /* After the count: per block its ID and its direction */

    for (uint8_t n = sources[MEEM_BLOCK_COUNT]; n > 0u; n--, order += 2)
    {
        uint8_t                    block_id       = order[0];
        uint8_t                    instance_count = MEEM_block_config[block_id].instance_count;
        MEEM_blockStatusPrivate_t* block_status   = &MEEM_block_status[block_id];
        bool                       migrated       = false;
        uint8_t                    profile        = 0u;
        uint8_t                    step           = 1u;

        if (MEEM_PASS2_DESCENDING == order[1])
        {
            profile = instance_count - 1u; /* The highest profile first */
            step    = UINT8_MAX;           /* Adding it subtracts 1, modulo 256 */
        }

        for (uint8_t i = 0; i < instance_count; i++, profile += step)
        {
            if (MEEM_FillFromLegacySource(model, block_id, sources[block_id], profile))
            {
                migrated = true;
            }
            MEEM_WriteProfile(block_id, profile);
        }

        block_status->index_of_active_instance = MEEM_SelectInitiallyActiveProfile(block_id);
        MEEM_InitializeMultiProfileBlock(block_id);

        if (migrated)
        {
            block_status->migrated = true;
        }
        else
        {
            block_status->recovered = true;
        }
        MEEM_OnBlockInitComplete(block_id);
    }
}
#endif

/******************************************************************************/
/*    Internal operations                                                     */
/******************************************************************************/
/*!
 * \brief     Reads the valid instance of a legacy source into the work buffer: the single instance of a basic block, the first valid copy
 *            of a 'backup copy' block, the given profile of a multi-profile block, the most recent valid instance of a 'wear-leveling' block.
 *            Serves the generated migration routines.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] source - the legacy block
 * \param[in] profile - the profile to read of a multi-profile source. Ignored for another management type.
 * \return    a pointer to the parameters of the instance in the work buffer, without a 'wear-leveling' sequence counter,
 *            or NULL if the source has no valid instance. A profile above the old profile count has none.
 */
const uint8_t* MEEM_ReadLegacyInstance(const MEEM_legacyBlockConfig_t* source, uint8_t profile)
{
    bool    valid            = false;
    uint8_t parameter_offset = sizeof(MEEM_checksum_t);

    /* The generator sizes the work buffer for every source that the core reads */
    assert((sizeof(MEEM_checksum_t) + source->data_size) <= MEEM_WORKBUFFER_SIZE);

#if (MEEM_LEGACY_USING_MULTI_PROFILE_BLOCKS == false)
    (void) profile; /* Only a multi-profile source has profiles */
#endif

    switch (source->management_type)
    {
#if (MEEM_LEGACY_USING_BASIC_BLOCKS == true)
        case MEEM_MGMT_BASIC:
            valid = MEEM_FetchLegacyInstance(source, 0u);
            break;
#endif
#if (MEEM_LEGACY_USING_BACKUP_COPY_BLOCKS == true)
        case MEEM_MGMT_BACKUP_COPY:
            /* The second copy is read only if the first isn't valid */
            valid = MEEM_FetchLegacyInstance(source, 0u) || MEEM_FetchLegacyInstance(source, 1u);
            break;
#endif
#if (MEEM_LEGACY_USING_MULTI_PROFILE_BLOCKS == true)
        case MEEM_MGMT_MULTI_PROFILE:
            /* A profile that the source doesn't have isn't read: it keeps its defaults */
            valid = (profile < source->instance_count) && MEEM_FetchLegacyInstance(source, profile);
            break;
#endif
#if (MEEM_LEGACY_USING_WEAR_LEVELING_BLOCKS == true)
        case MEEM_MGMT_WEAR_LEVELING:
        {
            uint8_t sequence_counters[MEEM_MAX_WL_INSTANCE_COUNT];
            uint8_t index;

            /* The generator sizes the array for every 'wear-leveling' source that the core reads */
            assert(source->instance_count <= MEEM_MAX_WL_INSTANCE_COUNT);

            for (index = 0; index < source->instance_count; index++)
            {
                sequence_counters[index] = MEEM_FetchLegacyInstance(source, index) ? MEEM_work_buffer[sizeof(MEEM_checksum_t)] : MEEM_INVALID_WL_INSTANCE;
            }

            /* The scan went on past the most recent instance, so it's read again */
            index = MEEM_FindIndexOfMostRecentInstance(sequence_counters, source->instance_count);
            valid = (MEEM_INVALID_WL_INSTANCE != index) && MEEM_FetchLegacyInstance(source, index);

            parameter_offset++; /* Skip the sequence counter, which isn't a parameter */
        }
        break;
#endif
        default:
            break;
    }

    return valid ? &MEEM_work_buffer[parameter_offset] : NULL;
}

/*!
 * \brief     Copies the parameters of a legacy source into the cache of a block with the same parameter list (the same data identity).
 *            The source is read as by MEEM_ReadLegacyInstance().
 * \note      This is a synchronous (blocking) operation!
 * \param[in] block_id - ID of the current block
 * \param[in] source - its legacy source
 * \param[in] profile - the profile to read of a multi-profile source. Ignored for another management type.
 * \retval    true if the parameters were copied
 * \retval    false if the source has no valid instance. The cache is left unchanged.
 */
bool MEEM_CopyLegacyBlock(uint8_t block_id, const MEEM_legacyBlockConfig_t* source, uint8_t profile)
{
    const MEEM_blockConfig_t* block_cfg  = &MEEM_block_config[block_id];
    const uint8_t*            parameters = MEEM_ReadLegacyInstance(source, profile);
    uint8_t*                  cache      = block_cfg->cache;
    uint16_t                  size       = block_cfg->data_size;

#if (MEEM_USING_WEAR_LEVELING_BLOCKS == true)
    if (MEEM_MGMT_WEAR_LEVELING == block_cfg->management_type)
    {
        /* The cache keeps its sequence counter, which isn't a parameter */
        cache++;
        size--;
    }
#endif

    /* The same parameter list on both sides */
    assert(size == (source->data_size - ((MEEM_MGMT_WEAR_LEVELING == source->management_type) ? 1u : 0u)));

    if (NULL != parameters)
    {
        (void) memcpy(cache, parameters, size);
    }
    return (NULL != parameters);
}

/*!
 * \brief   Finds the legacy model whose identity header the work buffer holds. A header matches at most one, so their order doesn't matter.
 * \retval  [0..MEEM_LEGACY_MODEL_COUNT) - the index of that legacy model
 * \retval  MEEM_MODEL_UNKNOWN - none
 */
uint8_t MEEM_FindLegacyModel(void)
{
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        const uint8_t* header = MEEM_legacy_models[k].identity_header;
        uint8_t        i      = 0;

        /* A loop, since memcmp() would add library code */
        while ((i < sizeof(MEEM_identity_header)) && (MEEM_work_buffer[i] == header[i]))
        {
            i++;
        }

        if (sizeof(MEEM_identity_header) == i)
        {
            return k;
        }
    }
    return MEEM_MODEL_UNKNOWN;
}

/*!
 * \brief     Migrates every block from a legacy model, when the EEPROM starts with its identity header. Neither pass writes
 *            over old data that is still needed.
 *            Pass 1 is this function: every block in definition order, leaving out the blocks of pass 2. It only reads. An unchanged block is
 *            initialized as in v1. Any other gets the legacy data if there is any, else the defaults, and a rewrite. A rewrite stores the
 *            cache in every instance, whatever the recovery strategy, so a multi-profile block filled from another management type gets it
 *            in every profile.
 * \note      This is a synchronous (blocking) operation!
 * \param[in] model - index of the legacy model
 */
void MEEM_MigrateFromLegacyModel(uint8_t model)
{
    const uint8_t* sources = MEEM_legacy_models[model].sources;

    for (uint8_t i = 0; i < MEEM_BLOCK_COUNT; i++)
    {
#if (MEEM_LEGACY_USING_PASS_2 == true)
        if (MEEM_IsInPass2(sources, i))
        {
            continue; /* Migrated after every other block, profile by profile */
        }
#endif
        MEEM_SelectInitialProfile(i);

        if (MEEM_SOURCE_UNCHANGED == sources[i])
        {
            MEEM_InitializeBlock(i);
        }
        else
        {
            if (MEEM_FillFromLegacySource(model, i, sources[i], 0u))
            {
                MEEM_block_status[i].migrated = true;
            }
            else
            {
                /* No source, or no valid one: the defaults */
                MEEM_RecoverBlockData(i);
            }
            MEEM_RequestRewrite(i);
        }
        MEEM_OnBlockInitComplete(i);
    }

#if (MEEM_LEGACY_USING_PASS_2 == true)
    MEEM_MigratePass2(model);
#endif
}
#endif
