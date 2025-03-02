// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
// Match address to a NAPOT range
uint8_t
match_address_range(
    uint64_t VPN, uint8_t VPN_S, uint64_t BASE_PN, uint8_t S) {

    uint64_t RANGE_MASK;
    uint64_t VPN_RANGE_MASK;
    VPN_RANGE_MASK = (VPN_S == 0) ? ~0UL : ~((VPN) ^ (VPN + 1));
    RANGE_MASK = (S == 0) ? ~0UL : ~((BASE_PN) ^ (BASE_PN + 1));
    return ((VPN & RANGE_MASK & VPN_RANGE_MASK) ==
            (BASE_PN & RANGE_MASK & VPN_RANGE_MASK)) ? 1 : 0;
}
