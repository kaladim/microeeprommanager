/*!
 * \file    EEPROM_Driver.c
 * \brief   Driver for PIC16/18 on-chip EEPROM. Key features:
 *          - Expects Microchip's XC8 compiler
 *          - Serializes the access to EEPROM
 *          - Covers 16-bit EEPROM address space
 *          - Designed for non-preemptive environment
 *          - All operations follow asynchronous, non-blocking programming model
 *          - Does not use interrupts.
 *          - Remember: the EEPROM is written always byte-wise.
 * \author  Kaloyan Dimitrov
 */
/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <assert.h>
#include <xc.h>
#include "EEPROM_Driver_PIC16F.h"

/******************************************************************************/
/*    Macros                                                                  */
/******************************************************************************/
#define EEP_MAX_WRITE_RETRY_ATTEMPTS 2

/******************************************************************************/
/*    Types                                                                   */
/******************************************************************************/
/* Currently executed operation type */
enum EEP_operation_t {
    OPR_NONE,
    OPR_READ,
    OPR_WRITE
};

/* Read/Write request */
struct EEP_request_t {
    union {
        uint8_t*       data;
        const uint8_t* ro_data;
    };
    uint16_t address;
    uint16_t size;
};

/** Runtime status */
struct EEP_status_t {
    enum EEP_operation_t operation;           /**< Currently executed operation */
    EED_status_t         job_status;          /**< Internal status */
    uint8_t              write_retry_counter; /**< Counts the current write attempt */
};

/******************************************************************************/
/*    Private global variables                                                */
/******************************************************************************/
static struct EEP_request_t EEP_request; /**< Write/erase request */
static struct EEP_status_t  EEP_status;

/******************************************************************************/
/*    Private functions prototypes                                            */
/******************************************************************************/
static void    EEP_ReadTask(void);
static void    EEP_WriteTask(void);
static uint8_t EEP_ReadByte(uint16_t eeprom_address);
static void    EEP_StartWriteByte(uint8_t byte);

/******************************************************************************/
/*    Public operations                                                       */
/******************************************************************************/
void EED_Init(void)
{
    EECON1bits.WREN = 0;

    EEP_status.job_status = EED_IDLE;
    EEP_status.operation  = OPR_NONE;
}

void EED_DeInit(void)
{
    EED_Init();
}

void EED_Task(void)
{
    switch (EEP_status.operation)
    {
        case OPR_WRITE:
            EEP_WriteTask();
            break;

        case OPR_READ:
            EEP_ReadTask();
            break;

        default:
            break;
    }
}

EED_status_t EED_GetStatus(void)
{
    return EEP_status.job_status;
}

bool EED_BeginRead(uint16_t eeprom_address, uint8_t* dest, uint16_t size)
{
    assert((eeprom_address + size) <= _EEPROMSIZE);
    assert(dest != NULL);
    assert(size > 0);

    if (EEP_status.job_status == EED_BUSY)
    {
        return false;
    }

    EEP_request.address = eeprom_address;
    EEP_request.size    = size;
    EEP_request.data    = dest;

    EEP_status.operation  = OPR_READ;
    EEP_status.job_status = EED_BUSY;

    EED_Task();
    return true;
}

bool EED_BeginWrite(uint16_t eeprom_address, const uint8_t* source, uint16_t size)
{
    assert((eeprom_address + size) <= _EEPROMSIZE);
    assert(source != NULL);
    assert(size > 0);

    /* Check if there is a write in progress  */
    if (EEP_status.job_status == EED_BUSY)
    {
        return false;
    }

    /* Store the write request */
    EEP_request.address = eeprom_address;
    EEP_request.ro_data = source;
    EEP_request.size    = size;

    EEP_status.operation           = OPR_WRITE;
    EEP_status.job_status          = EED_BUSY;
    EEP_status.write_retry_counter = 0;

    /* We may start writing the first byte even now: */
    EEP_StartWriteByte(*EEP_request.ro_data);
    return true;
}

/******************************************************************************/
/*    Private operations                                                      */
/******************************************************************************/
static void EEP_ReadTask(void)
{
    uint8_t max_bytes_per_call = 32;

    while ((EEP_request.size != 0) && (max_bytes_per_call != 0))
    {
        *EEP_request.data = EEP_ReadByte(EEP_request.address);

        EEP_request.address++;
        EEP_request.data++;
        EEP_request.size--;
        max_bytes_per_call--;
    }

    if (EEP_request.size == 0)
    {
        /* Complete, set job status */
        EEP_status.job_status = EED_OK;
        EEP_status.operation  = OPR_NONE;
    }
}

/*!
 * \brief  Moves the write request one byte at a time.
 */
static void EEP_WriteTask(void)
{
    /* Check if byte write is complete */
    if (PIR2bits.EEIF == 0)
    {
        return;
    }

    /* Complete. Reset the write process */
    PIR2bits.EEIF   = 0;
    EECON1bits.WREN = 0;

    /* Verify the result */
    if (*EEP_request.ro_data == EEP_ReadByte(EEP_request.address))
    {
        /* OK! */
        EEP_status.write_retry_counter = 0;

        /* Update the request */
        EEP_request.address++;
        EEP_request.ro_data++;
        EEP_request.size--;

        /* Write more bytes if there's more */
        if (EEP_request.size > 0)
        {
            EEP_StartWriteByte(*EEP_request.ro_data);
        }
        else
        {
            /* Request is complete, set job status */
            EEP_status.job_status = EED_OK;
            EEP_status.operation  = OPR_NONE;
        }
    }
    else
    {
        /* Oops, the written byte is different */
        if (EEP_status.write_retry_counter < EEP_MAX_WRITE_RETRY_ATTEMPTS)
        {
            /* Try again. */
            EEP_status.write_retry_counter++;
            EEP_StartWriteByte(*EEP_request.ro_data);
        }
        else
        {
            /* All attempts failed, terminate the request. */
            EEP_status.job_status = EED_NOK;
            EEP_status.operation  = OPR_NONE;
        }
    }
}

/*!
 * \brief      Performs the actual read of a byte from the EEPROM.
 * \param[in]  eeprom_address address in EEPROM
 * \return     content of that EEPROM address
 */
static uint8_t EEP_ReadByte(uint16_t eeprom_address)
{
    EECON1bits.CFGS  = 0; /* Access the EEPROM */
    EECON1bits.EEPGD = 0; /* Access the EEPROM */

#if (_EEPROMSIZE > 256)
    EEADRH = (uint8_t) (eeprom_address >> 8);
#endif
    EEADR         = (uint8_t) eeprom_address;
    EECON1bits.RD = 1;
    NOP();

    return EEDATA;
}

/*!
 * \brief     Starts the actual write of a byte in the EEPROM.
 *            The write will be performed only if it's different from the current content at that EEPROM address.
 * \param[in] byte - the byte to be written
 */
static void EEP_StartWriteByte(uint8_t byte)
{
    /* Write only if there's a change! */
    /* Note: the access to EEPROM is already set up by the EEP_ReadByte() */
    if (byte != EEP_ReadByte(EEP_request.address))
    {
        uint8_t intcon_cache;

        EEDATA        = byte;
        PIR2bits.EEIF = 0;

        intcon_cache = INTCON;
        INTCON       = 0; /* Disable all interrupts */

        /* Do the write unlock sequence */
        EECON1bits.WREN = 1;
        EECON2          = 0x55;
        EECON2          = 0xAA;
        EECON1bits.WR   = 1; /* Start the actual write */

        INTCON = intcon_cache; /* Resume enabled interrupts */
    }
    else
    {
        /* Difference not found, write not necessary. */
        PIR2bits.EEIF = 1; /* Simulate successful write */
    }
}
