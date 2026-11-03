/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_CRCIF.h"
#include "MEEM_CRCIF_Mock.hpp"

// Define the static instance pointer (initialized to nullptr by default)
MockCRCIF* MockCRCIF::instance = nullptr;

// The CRC unit of the tests that don't set a mock: a working one
static CrcUnitModel default_crc_unit{};

/******************************************************************************/
/*    Required operations by the mEEM core ('crc_implementation' 'hardware')  */
/******************************************************************************/
CRCIF_status_t CRCIF_BeginCalculation(const uint8_t* data, uint16_t size)
{
    if (MockCRCIF::instance)
    {
        return MockCRCIF::instance->BeginCalculation(data, size);
    }
    return default_crc_unit.BeginCalculation(data, size);
}

CRCIF_status_t CRCIF_GetResult(uint16_t* crc)
{
    if (MockCRCIF::instance)
    {
        return MockCRCIF::instance->GetResult(crc);
    }
    return default_crc_unit.GetResult(crc);
}
