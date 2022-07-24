// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#include "tables_api.h"

uint8_t
add_process_context(
    device_context_t *DC, process_context_t *PC, uint32_t process_id) {
    uint64_t a;
    uint8_t i, LEVELS;
    pdte_t pdte;
    uint8_t PDI[3];

    PDI[0] = get_bits(7,   0, process_id);
    PDI[1] = get_bits(16,  8, process_id);
    PDI[2] = get_bits(19, 17, process_id);

    if ( DC->fsc.pdtp.MODE == PD20 ) LEVELS = 3;
    if ( DC->fsc.pdtp.MODE == PD17 ) LEVELS = 2;
    if ( DC->fsc.pdtp.MODE == PD8  ) LEVELS = 1;

    a = DC->fsc.pdtp.PPN * PAGESIZE;
    i = LEVELS - 1;
    while ( i > 0 ) {
        if ( translate_gpa(DC->iohgatp, a, &a) != 0 ) return 1;
        read_memory((a + (PDI[i] * 8)), 8, (char *)&pdte.raw);
        if ( pdte.V == 0 ) {
            pdte.V = 1;
            pdte.reserved0 = pdte.reserved1 = 0;
            pdte.PPN = (DC->iohgatp.MODE == IOHGATP_Bare) ?
                       get_free_ppn(1) : get_free_gppn(1, 1, DC->iohgatp);
            write_memory((char *)&pdte.raw, (a + (PDI[i] * 8)), 8);
        }
        i = i - 1;
        a = pdte.PPN * PAGESIZE;
    }
    if ( translate_gpa(DC->iohgatp, a, &a) != 0 ) return 1;
    write_memory((char *)PC, (a + (PDI[0] * 16)), 16);
    return 0;
}
