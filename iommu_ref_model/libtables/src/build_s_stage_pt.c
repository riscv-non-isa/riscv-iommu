// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#include "tables_api.h"
uint64_t
add_s_stage_pte (
    iosatp_t satp, uint64_t va, pte_t pte, uint8_t add_level, uint8_t SXL) {

    uint16_t vpn[5];
    uint64_t a;
    uint8_t i, PTESIZE, LEVELS;
    pte_t nl_pte;

    PTESIZE = 8;
    if ( satp.MODE == IOSATP_Sv32 && SXL == 1) {
        vpn[0] = get_bits(21, 12, va);
        vpn[1] = get_bits(31, 22, va);
        LEVELS = 2;
        PTESIZE = 4;
    }
    if ( satp.MODE == IOSATP_Sv39 && SXL == 0) {
        vpn[0] = get_bits(20, 12, va);
        vpn[1] = get_bits(29, 21, va);
        vpn[2] = get_bits(38, 30, va);
        LEVELS = 3;
    }
    if ( satp.MODE == IOSATP_Sv48 ) {
        vpn[0] = get_bits(20, 12, va);
        vpn[1] = get_bits(29, 21, va);
        vpn[2] = get_bits(38, 30, va);
        vpn[3] = get_bits(47, 39, va);
        LEVELS = 4;
    }
    if ( satp.MODE == IOSATP_Sv57 ) {
        vpn[0] = get_bits(20, 12, va);
        vpn[1] = get_bits(29, 21, va);
        vpn[2] = get_bits(38, 30, va);
        vpn[3] = get_bits(47, 39, va);
        vpn[4] = get_bits(56, 48, va);
        LEVELS = 5;
    }
    i = LEVELS - 1;
    a = satp.PPN * PAGESIZE;
    while ( i > add_level ) {
        nl_pte.raw = 0;
        if ( read_memory((a | (vpn[i] * PTESIZE)), PTESIZE, (char *)&nl_pte.raw)) return -1;
        if ( nl_pte.V == 0 ) {
            nl_pte.V = 1;
            nl_pte.PPN = get_free_ppn(1);
            if ( write_memory((char *)&nl_pte.raw, (a | (vpn[i] * PTESIZE)), PTESIZE) ) return -1;
        }
        i = i - 1;
        if ( i < 0 ) return -1;
        a = nl_pte.PPN * PAGESIZE;
    }
    if ( write_memory((char *)&pte.raw, (a | (vpn[i] * PTESIZE)), PTESIZE) ) return -1;
    return (a | (vpn[i] * PTESIZE));
}
