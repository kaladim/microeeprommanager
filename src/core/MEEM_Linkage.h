/*!
 * \file    MEEM_Linkage.h
 * \brief   C/C++ compiler linkage compatibility defs
 * \author  Kaloyan Dimitrov
 * \copyright Copyright (c) 2025 Kaloyan Dimitrov
 *            https://github.com/kaladim
 *            SPDX-License-Identifier: MIT
 */
#ifndef MEEM_LINKAGE_H
#define MEEM_LINKAGE_H

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

#endif /* MEEM_LINKAGE_H */