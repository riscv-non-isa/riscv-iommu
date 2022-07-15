// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
// Match address to a NAPOT range
uint8_t
match_address_range(
    uint64_t ADDR, uint64_t PPN, uint8_t S) {
    uint64_t mask = PPN * PAGESIZE;
    mask = (S == 0) ? ~0xFFF : ~(PPN ^ (PPN + 1));
    if ( (ADDR & mask) == (PPN & mask) ) { 
        return 1;
    } else {
        return 0;
    }
}
