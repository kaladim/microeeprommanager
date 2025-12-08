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
    return (MEEM_checksum_t) CRC8_Compute(data, data_size);
}
