/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include "MEEM_Checksum.h"
#include "CRC.h"

/******************************************************************************/
/*    Required operations by the mEEM core                                    */
/******************************************************************************/
MEEM_checksum_t MEEM_CalculateChecksum(const void* data, uint16_t data_size)
{
    return static_cast<MEEM_checksum_t>(CRC8_Compute(static_cast<const uint8_t*>(data), data_size));
}
