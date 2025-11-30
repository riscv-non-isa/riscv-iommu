// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
uint64_t
translate_gpa (
    iommu_t *iommu, iohgatp_t iohgatp, uint64_t gpa, uint64_t *spa) {

    uint16_t vpn[5];
    uint64_t a;
    uint8_t i, PTESIZE, LEVELS;
    gpte_t nl_gpte;
    uint64_t gst_page_sz;
    uint8_t gxl = iommu->reg_file.fctl.gxl;

    PTESIZE = 8;
    if ( iohgatp.MODE == IOHGATP_Bare ) {
        *spa = gpa;
        return -1;
    }
    if ( iohgatp.MODE == IOHGATP_Sv32x4 && gxl == 1) {
        vpn[0] = get_bits(21, 12, gpa);
        vpn[1] = get_bits(34, 22, gpa);
        LEVELS = 2;
        PTESIZE = 4;
        gst_page_sz = 4UL * 1024UL * 1024UL;
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 && gxl == 0) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(40, 30, gpa);
        gst_page_sz = 512UL * 512UL * PAGESIZE;
        LEVELS = 3;
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(49, 39, gpa);
        gst_page_sz = 512UL * 512UL * 512UL * PAGESIZE;
        LEVELS = 4;
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(47, 39, gpa);
        vpn[4] = get_bits(58, 48, gpa);
        gst_page_sz = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
        LEVELS = 5;
    }
    i = LEVELS - 1;
    a = iohgatp.PPN * PAGESIZE;
    while ( 1 ) {
        nl_gpte.raw = 0;
        if ( read_memory_test((a | (vpn[i] * PTESIZE)), PTESIZE, (char *)&nl_gpte.raw) ) return -1;
        if ( nl_gpte.V == 0 ) return -1;
        if ( nl_gpte.R != 0 || nl_gpte.X != 0 ) {
            *spa = nl_gpte.PPN;
            *spa = *spa * PAGESIZE;
            *spa = *spa & ~(gst_page_sz - 1);
            *spa = *spa | (gpa & (gst_page_sz - 1));
            return (a | (vpn[i] * PTESIZE));
        }
        i = i - 1;
        if ( i < 0 ) return -1;
        gst_page_sz = ( i == 0 ) ? PAGESIZE : (gst_page_sz / 512);
        a = nl_gpte.PPN * PAGESIZE;
    }
    return -1;
}
