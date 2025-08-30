// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#include "tables_api.h"

uint64_t
add_process_context(
    device_context_t *DC, process_context_t *PC, uint32_t process_id) {
    uint64_t a;
    uint8_t i, LEVELS;
    pdte_t pdte;
    uint16_t PDI[3];
    uint8_t is_GIPC = (DC->tc.GIPC == 1) && (g_reg_file.capabilities.GIPC == 1);

    PDI[0] = get_bits(7,   0, process_id);
    PDI[1] = get_bits(16,  8, process_id);
    PDI[2] = get_bits(19, 17, process_id);

    if (is_GIPC) {
        PDI[0] = get_bits(6,   0, process_id);
        PDI[1] = get_bits(15,  7, process_id);
        PDI[2] = get_bits(19, 16, process_id);
    }

    if ( DC->fsc.pdtp.MODE == PD20 ) LEVELS = 3;
    if ( DC->fsc.pdtp.MODE == PD17 ) LEVELS = 2;
    if ( DC->fsc.pdtp.MODE == PD8  ) LEVELS = 1;

    a = DC->fsc.pdtp.PPN * PAGESIZE;
    i = LEVELS - 1;
    while ( i > 0 ) {
        if (!is_GIPC) if ( translate_gpa(DC->iohgatp, a, &a) == -1 ) return -1;
        pdte.raw = 0;
        if ( read_memory_test((a + (PDI[i] * 8)), 8, (char *)&pdte.raw) ) return -1;
        if ( pdte.V == 0 ) {
            pdte.V = 1;
            pdte.reserved0 = pdte.reserved1 = 0;
            if (DC->iohgatp.MODE != IOHGATP_Bare && !is_GIPC) {
                gpte_t gpte;

                pdte.PPN = get_free_gppn(1, DC->iohgatp);

                gpte.raw = 0;
                gpte.V = 1;
                gpte.R = 1;
                gpte.W = 0;
                gpte.X = 0;
                gpte.U = 1;
                gpte.G = 0;
                gpte.A = 0;
                gpte.D = 0;
                gpte.PBMT = PMA;
                gpte.PPN = get_free_ppn(1);

                if ( add_g_stage_pte(DC->iohgatp, (PAGESIZE * pdte.PPN), gpte, 0) == -1 ) return -1;
            } else {
                pdte.PPN = get_free_ppn(1);
            }
            if ( write_memory_test((char *)&pdte.raw, (a + (PDI[i] * 8)), 8) ) return -1;
        }
        i = i - 1;
        a = pdte.PPN * PAGESIZE;
    }

    if (is_GIPC) {
        PC->iohgatp = DC->iohgatp;
        if ( write_memory_test((char *)PC, (a + (PDI[0] * 32)), 32) )
            return -1;
        else
            return (a + (PDI[0] * 32));
    }

    if ( translate_gpa(DC->iohgatp, a, &a) == -1 ) return -1;
    if ( write_memory_test((char *)PC, (a + (PDI[0] * 16)), 16) ) return -1;
    return (a + (PDI[0] * 16));
}
