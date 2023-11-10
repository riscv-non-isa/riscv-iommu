// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#include "tables_api.h"
uint64_t
add_g_stage_pte (
    iohgatp_t iohgatp, uint64_t gpa, gpte_t gpte, uint8_t add_level) {

    uint16_t vpn[5];
    uint64_t a;
    uint8_t i, PTESIZE, LEVELS;
    gpte_t nl_gpte;

    PTESIZE = 8;
    if ( iohgatp.MODE == IOHGATP_Sv32x4 ) {
        vpn[0] = get_bits(21, 12, gpa);
        vpn[1] = get_bits(34, 22, gpa);
        LEVELS = 2;
        PTESIZE = 4;
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(40, 30, gpa);
        LEVELS = 3;
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(49, 39, gpa);
        LEVELS = 4;
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(47, 39, gpa);
        vpn[4] = get_bits(58, 48, gpa);
        LEVELS = 5;
    }
    i = LEVELS - 1;
    a = iohgatp.PPN * PAGESIZE;
    while ( i > add_level ) {
        nl_gpte.raw = 0;
        if ( read_memory((a | (vpn[i] * PTESIZE)), PTESIZE, (char *)&nl_gpte.raw) ) return -1;
        if ( nl_gpte.V == 0 ) {
            nl_gpte.V = 1;
            nl_gpte.PPN = get_free_ppn(1);
            if ( write_memory((char *)&nl_gpte.raw, (a | (vpn[i] * PTESIZE)), PTESIZE) ) return -1;
        }
        i = i - 1;
        if ( i < 0 ) return -1;
        a = nl_gpte.PPN * PAGESIZE;
    }
    if ( write_memory((char *)&gpte.raw, (a | (vpn[i] * PTESIZE)), PTESIZE) ) return -1;
    return (a | (vpn[i] * PTESIZE));
}
