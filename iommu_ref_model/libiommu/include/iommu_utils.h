// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_UTILS_H__
#define __IOMMU_UTILS_H__
#define get_bits(__MS_BIT, __LS_BIT, __FIELD)\
    ((__FIELD >> __LS_BIT) & (((uint64_t)1 << (((__MS_BIT - __LS_BIT) + 1))) - 1))
extern uint8_t match_address_range( uint64_t ADDR, uint8_t ADDR_S, uint64_t PPN, uint8_t S);
#endif // __IOMMU_UTILS_H__
