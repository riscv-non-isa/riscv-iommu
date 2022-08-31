// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
// Match address to a NAPOT range
uint8_t
match_address_range(
    uint64_t VPN, uint64_t BASE_PN, uint8_t S) {

    uint64_t RANGE_MASK;

    RANGE_MASK = (S == 0) ? ~0UL : ~((BASE_PN) ^ (BASE_PN + 1));
    return ((VPN & RANGE_MASK) == (BASE_PN & RANGE_MASK)) ? 1 : 0;
}
