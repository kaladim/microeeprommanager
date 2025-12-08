/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_EEAIF.h"
#include "EEPROM_Driver_PIC16F.h"

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
void EEAIF_Init(void)
{
    EED_Init();
}

void EEAIF_DeInit(void)
{
    EED_DeInit();
}

void EEAIF_Task(void)
{
    EED_Task();
}

bool EEAIF_BeginRead(uint16_t offset_in_eeprom, uint8_t *dest, uint16_t size)
{
    return EED_BeginRead(offset_in_eeprom, dest, size);
}

bool EEAIF_BeginWrite(uint16_t offset_in_eeprom, const uint8_t *source, uint16_t size)
{
    return EED_BeginWrite(offset_in_eeprom, source, size);
}

EEAIF_status_t EEAIF_GetStatus(void)
{
    return (EEAIF_status_t)EED_GetStatus();
}
