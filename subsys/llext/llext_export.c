/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/llext/symbol.h>

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);

#ifdef __CCAC__
extern void __ac_mc_va(void);
extern void __ac_pop_13_to_14v(void);
extern void __ac_push_13_to_16(void);
extern void __ac_pop_13_to_16v(void);
extern void __ac_push_13_to_17(void);
extern void __ac_pop_13_to_17v(void);

EXPORT_SYMBOL(__ac_mc_va);
EXPORT_SYMBOL(__ac_pop_13_to_14v);
EXPORT_SYMBOL(__ac_push_13_to_16);
EXPORT_SYMBOL(__ac_pop_13_to_16v);
EXPORT_SYMBOL(__ac_push_13_to_17);
EXPORT_SYMBOL(__ac_pop_13_to_17v);
#endif

#include <zephyr/syscall_exports_llext.c>
