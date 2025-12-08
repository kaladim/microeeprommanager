/*!
 * \file    EEPROM_Driver_PIC16.h
 * \author  Kaloyan Dimitrov
 * \brief   Interface of the EEPROM driver for PIC16/18 on-chip EEPROM.
 */
#ifndef EEPROM_DRIVER_PIC16F_H
#define EEPROM_DRIVER_PIC16F_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdbool.h>
#include <stdint.h>

/******************************************************************************/
/*    Types                                                                   */
/******************************************************************************/
typedef enum
{
    EED_OK = 0,
    EED_NOK,
    EED_BUSY,
    EED_IDLE
} EED_status_t;

/******************************************************************************/
/*    Exported operations                                                     */
/******************************************************************************/
/*!
 * \brief   Driver initializer.
 * \pre     Call once or after EED_DeInit().
 */
extern void EED_Init(void);

/*!
 * \brief   Driver de-initializer.
 */
extern void EED_DeInit(void);

/*!
 * \brief  Manages read and write async operations. The final status of the operations is set here.
 * \note   For best performance, should be called as often as possible.
 */
extern void EED_Task(void);

/*!
 * \brief   Gets the actual status of the driver.
 * \retval  EEP_OK when the last operation completed successfully.
 * \retval  EED_NOK when the last operation failed.
 * \retval  EEP_BUSY when there is currently an operation in progress
 * \retval  EED_IDLE immediately after init, until first accepted request
 */
extern EED_status_t EED_GetStatus(void);

/*!
 * \brief      Initiates an async read of EEPROM.
 * \pre        The driver has to be initialized.
 * \param[in]  eeprom_address start address in EEPROM
 * \param[out] dest pointer to destination buffer
 * \param[in]  size count of bytes to read
 * \retval     true if the request is accepted
 * \retval     false if the request is rejected: the driver is busy with another operation
 */
extern bool EED_BeginRead(uint16_t eeprom_address, uint8_t *dest, uint16_t size);

/*!
 * \brief      Initiates an async write to the EEPROM.
 * \pre        The driver have to be initialized.
 * \param[in]  eeprom_address start address in EEPROM
 * \param[in]  source pointer to source buffer
 * \param[in]  size count of bytes to write
 * \retval     true if the request is accepted
 * \retval     false if the request is rejected: either the driver is busy with another operation
 */
extern bool EED_BeginWrite(uint16_t eeprom_address, const uint8_t *source, uint16_t size);

#endif /* EEPROM_DRIVER_PIC16F_H */
