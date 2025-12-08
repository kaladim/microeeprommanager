/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <memory>
#include "eeprom_simulator.hpp"
#include "MEEM_EEAIF.h"
#include "MEEM.h"

std::unique_ptr<EepromSimulator> eep_sim = std::make_unique<EepromSimulator>("./eeprom.bin", MEEM_AVAILABLE_EEPROM_BYTES);

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
void EEAIF_Init(void)
{
}

void EEAIF_DeInit(void)
{
}

void EEAIF_Task(void)
{
}

bool EEAIF_BeginRead(uint16_t offset_in_eeprom, uint8_t *dest, uint16_t size)
{
    return eep_sim->read(offset_in_eeprom, dest, size);
}

bool EEAIF_BeginWrite(uint16_t offset_in_eeprom, const uint8_t *source, uint16_t size)
{
    return eep_sim->write(offset_in_eeprom, source, size);
}

EEAIF_status_t EEAIF_GetStatus(void)
{
    return eep_sim->get_status();
}
