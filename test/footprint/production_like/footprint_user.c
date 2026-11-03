/*!
 * \file    footprint_user.c
 * \brief   Footprint harness: minimal user side of mEEM (EEPROM access driver, callbacks).
 *          All driver state is volatile, so that the compiler can't constant-fold any path of the core.
 *          The footprint script counts this file as user code, not as mEEM.
 */
#include <stdbool.h>
#include <stdint.h>
#include "MEEM_EEAIF.h"
#include "MEEM_UserCallbacks.h"

volatile uint8_t        footprint_eeaif_status;
volatile uint8_t        footprint_eeaif_accept;
volatile uint8_t        footprint_sink;
volatile uint16_t       footprint_offset;
volatile uint16_t       footprint_size;
uint8_t* volatile       footprint_dest;
const uint8_t* volatile footprint_source;

/*------------------------ EEPROM access driver ------------------------*/
void EEAIF_Init(void)
{
    footprint_eeaif_status = EEAIF_IDLE;
}

void EEAIF_DeInit(void)
{
    footprint_eeaif_status = EEAIF_IDLE;
}

void EEAIF_Task(void)
{
    footprint_sink++;
}

bool EEAIF_BeginRead(uint16_t offset_in_eeprom, uint8_t* dest, uint16_t size)
{
    footprint_offset = offset_in_eeprom;
    footprint_dest   = dest;
    footprint_size   = size;
    return (footprint_eeaif_accept != 0u);
}

bool EEAIF_BeginWrite(uint16_t offset_in_eeprom, const uint8_t* source, uint16_t size)
{
    footprint_offset = offset_in_eeprom;
    footprint_source = source;
    footprint_size   = size;
    return (footprint_eeaif_accept != 0u);
}

EEAIF_status_t EEAIF_GetStatus(void)
{
    return (EEAIF_status_t) footprint_eeaif_status;
}

/*------------------------ User callbacks ------------------------*/
uint8_t MEEM_SelectInitiallyActiveProfile(uint8_t block_id)
{
    return (uint8_t) (footprint_sink + block_id);
}

void MEEM_OnBlockInitComplete(uint8_t block_id)
{
    footprint_sink = block_id;
}

void MEEM_OnBlockWriteStarted(uint8_t block_id)
{
    footprint_sink = block_id;
}

void MEEM_OnBlockWriteComplete(uint8_t block_id)
{
    footprint_sink = block_id;
}

void MEEM_OnMultiProfileBlockFetchStarted(uint8_t block_id)
{
    footprint_sink = block_id;
}

void MEEM_OnMultiProfileBlockFetchComplete(uint8_t block_id)
{
    footprint_sink = block_id;
}
