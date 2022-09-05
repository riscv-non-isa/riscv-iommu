// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

uint8_t
locate_process_context(
    process_context_t *PC, device_context_t *DC, uint32_t device_id, uint32_t process_id, 
    uint32_t *cause, uint64_t *iotval2, uint8_t TTYP) {
    uint64_t a, gst_page_sz;
    uint8_t i, LEVELS, status;
    uint8_t GR, GW, GX, GD, GPBMT;
    pdte_t pdte;
    uint8_t PDI[3];

    // The device-context provides the PDT root page PPN (pdtp.ppn). 
    // When DC.iohgatp.mode is not Bare, pdtp.PPN as well as pdte.PPN
    // are Guest Physical Addresses (GPA) which must be translated into
    // System Physical Addresses (SPA) using the G-stage page table 
    // determined by DC.iohgatp.

    // The PDT may be configured to be a 1, 2, or 3 level radix table 
    // depending on the maximum width of the process_id supported for 
    // that device. The partitioning of the process_id to obtain the process
    // directory indices (PDI) to traverse the PDT radix-tree table are as follows:
    PDI[0] = get_bits(7,   0, process_id);
    PDI[1] = get_bits(16,  8, process_id);
    PDI[2] = get_bits(19, 17, process_id);

    // The following diagrams illustrate the PDT radix-tree. The root
    // process-directory page number is located using the process-directory-table
    // pointer (`pdtp`) field of the device-context. Each non-leaf (`NL`) entry
    // provides the PPN of the next level process-directory-table. The leaf
    // process-directory-table entry holds the process-context (`PC`).
    // .Three, two and single-level process directory
    //   +-------+-------+-------+      +-------+-------+   +-------+
    //   |PDI[2] |PDI[1] |PDI[0] |      |PDI[1] |PDI[0] |   |PDI[0] |
    //   +--+----+--+----+--+----+      +-+-----+-+-----+   +-+-----+
    //      |       |       |             |       |           |
    //      +-3-bit +-9-bit +-8-bit       +-9-bit +-8-bit     +-8-bit
    //      |       |       |             |       |           |
    //      |  +--+ |  +--+ |  +--+       |  +--+ |  +--+     |   +--+
    //      |  |  | |  |  | |  |  |       |  |  | |  |  |     |   |  |
    //      |  |  | |  |  | |  +--+       |  |  | |  +--+     |   |  |
    //      |  |  | |  |  | +->|PC|       |  |  | +->|PC|     |   |  |
    //      |  |  | |  +--+    +--+       |  |  |    +--+     |   |  |
    //      |  |  | +->|NL+-+  |  |       |  +--+    |  |     |   |  |
    //      |  |  |    +--+ |  |  |       +->|NL+-+  |  |     |   +--+
    //      +->+--+    |  | |  |  |          +--+ |  |  |     +-->|PC|
    //         |NL+-+  |  | |  |  |          |  | |  |  |         +--+
    //         +--+ |  |  | |  |  |          |  | |  |  |         |  |
    //         |  | |  |  | |  |  |          |  | |  |  |         |  |
    // pdtp--->+--+ +->+--+ +->+--+  pdtp--->+--+ +->+--+ pdtp--->+--+

    // The process to locate the Process-context for a transaction 
    // using its process_id is as follows:

    // Determine if there is a cached device context
    if ( lookup_ioatc_pc(device_id, process_id, PC) == IOATC_HIT )
        return 0;

    // 1. Let a be pdtp.PPN x 2^12 and let i = LEVELS - 1. When pdtp.MODE 
    //    is PD20, LEVELS is three. When pdtp.MODE is PD17, LEVELS is two. 
    //    When pdtp.MODE is PD8, LEVELS is one.
    a = DC->fsc.pdtp.PPN * PAGESIZE;
    if ( DC->fsc.pdtp.MODE == PD20 ) LEVELS = 3;
    if ( DC->fsc.pdtp.MODE == PD17 ) LEVELS = 2;
    if ( DC->fsc.pdtp.MODE == PD8  ) LEVELS = 1;
    i = LEVELS - 1;

step_2:
    a = a + ((i == 0) ? (PDI[i] * 16) : (PDI[i] * 8));
    // 2. If `DC.iohgatp.mode != Bare`, then `a` is a GPA. Invoke the process
    //    to translate `a` to a SPA as an implicit memory access. If faults 
    //    occur during G-stage address translation of `a` then stop and the fault
    //    detected by the G-stage address translation process. The translated `a`
    //    is used in subsequent steps.
    if ( g_stage_address_translation(a, 1, 0, 0, 1,
            DC->iohgatp, cause, iotval2, &a, &gst_page_sz, &GR, &GW, &GX, &GD, &GPBMT,
            1, process_id, 0, 0, device_id, ((DC->iohgatp.MODE == IOHGATP_Bare) ? 0 : 1), 
            DC->iohgatp.GSCID, TTYP, DC->tc.GADE) )
        return 1;

    // 3. If `i == 0` go to step 9.
    if ( i == 0 ) goto step_9;

    // Count walks in PDT
    count_events(1, process_id, 0, 0, device_id, 0, 0, PDT_WALKS);

    // 4. Let `pdte` be value of eight bytes at address `a + PDI[i] x 8`. If
    //    accessing `pdte` violates a PMA or PMP check, then stop and report
    //    "PDT entry load access fault" (cause = 265).
    status = read_memory(a, 8, (char *)&pdte.raw);
    if ( status & ACCESS_FAULT ) {
        *cause = 265;     // PDT entry load access fault
        return 1;
    }

    // 5. If `pdte` access detects a data corruption (a.k.a. poisoned data), then
    //     stop and report "PDT data corruption" (cause = 269).
    if ( status & DATA_CORRUPTION ) {
        *cause = 269;     // PDT data corruption
        return 1;
    }

    // 6. If `pdte.V == 0`, stop and report "PDT entry not valid" (cause = 266).
    if ( pdte.V == 0 ) {
        *cause = 266;     // PDT entry not valid
        return 1;
    }

    // 7. If if any bits or encoding that are reserved for future standard use are
    //    set within `pdte`, stop and report "PDT entry misconfigured" (cause = 267).
    if ( pdte.reserved0 != 0 || pdte.reserved1 != 0 ) {
        *cause = 267;     // PDT entry misconfigured
        return 1;
    }

    // 8. Let `i = i - 1` and let `a = pdte.PPN x 2^12`. Go to step 2.
    i = i - 1;
    a = pdte.PPN * PAGESIZE;
    goto step_2;

step_9:
    // Count walks in PDT
    count_events(1, process_id, 0, 0, device_id, 0, 0, PDT_WALKS);

    // 9. Let `PC` be value of 16-bytes at address `a + PDI[0] x 16`. If accessing `PC`
    //    violates a PMA or PMP check, then stop and report "PDT entry load access
    //    fault" (cause = 265).If `PC` access detects a data corruption
    //    (a.k.a. poisoned data), then stop and report "PDT data corruption"
    //    (cause = 269).
    status = read_memory(a, 16, (char *)PC);
    if ( status & ACCESS_FAULT ) {
        *cause = 265;     // PDT entry load access fault
        return 1;
    }
    if ( status & DATA_CORRUPTION ) {
        *cause = 269;     // PDT data corruption
        return 1;
    }
    //10. If `PC.ta.V == 0`, stop and report "PDT entry not valid" (cause = 266).
    if ( PC->ta.V == 0 ) {
        *cause = 266;     // PDT entry not valid
        return 1;
    }
    //11. If any bits or encoding that are reserved for future standard use are set
    //     within `PC`, stop and report "PDT entry misconfigured" (cause = 267).
    if ( PC->ta.reserved0 != 0 || PC->ta.reserved1 != 0 || 
         PC->fsc.iosatp.reserved != 0 ||
         ((PC->fsc.iosatp.MODE != IOSATP_Bare) &&
          (PC->fsc.iosatp.MODE != IOSATP_Sv32) &&
          (PC->fsc.iosatp.MODE != IOSATP_Sv39) &&
          (PC->fsc.iosatp.MODE != IOSATP_Sv48) &&
          (PC->fsc.iosatp.MODE != IOSATP_Sv57)) ) {
        *cause = 267;     // PDT entry not misconfigured
        return 1;
    } 
    //12. If any of the following conditions are true then stop and report
    //     "PDT entry misconfigured" (cause = 267).
    //    a. `capabilities.Sv32` is 0 and `PC.fsc.MODE` is `Sv32`
    //    b. `capabilities.Sv39` is 0 and `PC.fsc.MODE` is `Sv39`
    //    c. `capabilities.Sv48` is 0 and `PC.fsc.MODE` is `Sv48`
    //    d. `capabilities.Sv57` is 0 and `PC.fsc.MODE` is `Sv57`
    if ( ((PC->fsc.iosatp.MODE == IOSATP_Sv32) && (g_reg_file.capabilities.Sv32 == 0)) ||
         ((PC->fsc.iosatp.MODE == IOSATP_Sv39) && (g_reg_file.capabilities.Sv39 == 0)) ||
         ((PC->fsc.iosatp.MODE == IOSATP_Sv48) && (g_reg_file.capabilities.Sv48 == 0)) ||
         ((PC->fsc.iosatp.MODE == IOSATP_Sv57) && (g_reg_file.capabilities.Sv57 == 0)) ) {
        *cause = 267;     // PDT entry not misconfigured
        return 1;
    }
    //13. The Process-context has been successfully located.
    cache_ioatc_pc(device_id, process_id, PC);
    return 0;
}


