/*!
 * \file   CRC.h
 * \brief  CRC checksums
 */
#ifndef CRC_H
#define CRC_H

/******************************************************************************/
/*    Dependencies                                                            */
/******************************************************************************/
#include <stdint.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif

/******************************************************************************/
/*    Public operations prototypes                                            */
/******************************************************************************/
EXTERN_C uint8_t CRC8_Compute(const uint8_t *data, uint16_t data_length);

#endif /* CRC_H */
