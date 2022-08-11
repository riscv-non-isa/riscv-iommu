// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

uint8_t
locate_device_context(
    device_context_t *DC, uint32_t device_id, 
    uint8_t pid_valid, uint32_t process_id, uint32_t *cause) {
    uint64_t a;
    uint8_t i, LEVELS, status, DC_SIZE;
    ddte_t ddte;
    uint8_t DDI[3];

    // The following diagrams illustrate the DDT radix-tree. The PPN of the root
    // device-directory-table is held in a memory-mapped register called the
    // device-directory-table pointer (`ddtp`).
    // Each valid non-leaf (`NL`) entry is 8-bytes in size and holds the PPN of the
    // next device-directory-table.
    // A valid leaf device-directory-table entry holds the device-context (`DC`).
    // .Three, two and single-level device directory with extended format `DC`
    //   +-------+-------+-------+      +-------+-------+    +-------+
    //   |DDI[2] |DDI[1] |DDI[0] |      |DDI[1] |DDI[0] |    |DDI[0] |
    //   +--+----+--+----+--+----+      +-+-----+-+-----+    +-+-----+
    //      |       |       |             |       |            |
    //      +-9-bit +-9-bit +-6-bit       +-9-bit +-6-bit      +-6-bit
    //      |       |       |             |       |            |
    //      |  +--+ |  +--+ |  +--+       |  +--+ |  +--+      |   +--+
    //      |  |  | |  |  | |  |  |       |  |  | |  |  |      |   |  |
    //      |  |  | |  |  | |  +--+       |  |  | |  +--+      |   |  |
    //      |  |  | |  |  | +->|DC|       |  |  | +->|DC|      |   |  |
    //      |  |  | |  +--+    +--+       |  |  |    +--+      |   |  |
    //      |  |  | +->|NL+-+  |  |       |  +--+    |  |      |   |  |
    //      |  |  |    +--+ |  |  |       +->|NL+-+  |  |      |   +--+
    //      +->+--+    |  | |  |  |          +--+ |  |  |      +-->|DC|
    //         |NL+-+  |  | |  |  |          |  | |  |  |          +--+
    //         +--+ |  |  | |  |  |          |  | |  |  |          |  |
    //         |  | |  |  | |  |  |          |  | |  |  |          |  |
    // ddtp--->+--+ +->+--+ +->+--+  ddtp--->+--+ +->+--+  ddtp--->+--+
    //
    // .Three, two and single-level device directory with base format `DC`
    //  +-------+-------+-------+      +-------+-------+    +-------+
    //  |DDI[2] |DDI[1] |DDI[0] |      |DDI[1] |DDI[0] |    |DDI[0] |
    //  +--+----+--+----+--+----+      +-+-----+-+-----+    +-+-----+
    //     |       |       |             |       |            |
    //     +-8-bit +-9-bit +-7-bit       +-9-bit +-7-bit      +-7-bit
    //     |       |       |             |       |            |
    //     |  +--+ |  +--+ |  +--+       |  +--+ |  +--+      |   +--+
    //     |  |  | |  |  | |  |  |       |  |  | |  |  |      |   |  |
    //     |  |  | |  |  | |  +--+       |  |  | |  +--+      |   |  |
    //     |  |  | |  |  | +->|DC|       |  |  | +->|DC|      |   |  |
    //     |  |  | |  +--+    +--+       |  |  |    +--+      |   |  |
    //     |  |  | +->|NL+-+  |  |       |  +--+    |  |      |   |  |
    //     |  |  |    +--+ |  |  |       +->|NL+-+  |  |      |   +--+
    //     +->+--+    |  | |  |  |          +--+ |  |  |      +-->|DC|
    //        |NL+-+  |  | |  |  |          |  | |  |  |          +--+
    //        +--+ |  |  | |  |  |          |  | |  |  |          |  |
    //        |  | |  |  | |  |  |          |  | |  |  |          |  |
    //ddtp--->+--+ +->+--+ +->+--+  ddtp--->+--+ +->+--+  ddtp--->+--+
    //

    // The DDT used to locate the DC may be configured to be a 1, 2, or 3 level 
    // radix-table depending on the maximum width of the device_id supported. 
    // The partitioning of the device_id to obtain the device directory indexes
    // (DDI) to traverse the DDT radix-tree table are as follows:
    // If `capabilities.MSI_FLAT` is 0 then the IOMMU uses base-format device
    // context. Let `DDI[0]` be `device_id[6:0]`, `DDI[1]` be `device_id[15:7]`, and
    // `DDI[2]` be `device_id[23:16]`.
    if ( g_reg_file.capabilities.msi_flat == 0 ) {
        DDI[0] = get_bits(6,   0, device_id);
        DDI[1] = get_bits(15,  7, device_id);
        DDI[2] = get_bits(23, 16, device_id);
    }
    // If `capabilities.MSI_FLAT` is 1 then the IOMMU uses extended-format device
    // context. Let `DDI[0]` be `device_id[5:0]`, `DDI[1]` be `device_id[14:6]`, and
    // `DDI[2]` be `device_id[23:15]`.
    if ( g_reg_file.capabilities.msi_flat == 1 ) {
        DDI[0] = get_bits(5,   0, device_id);
        DDI[1] = get_bits(14,  6, device_id);
        DDI[2] = get_bits(23, 15, device_id);
    }

    // Determine if there is a cached device context
    if ( lookup_ioatc_dc(device_id, DC) == IOATC_HIT )
        return 0;

    // The process to locate the Device-context for transaction 
    // using its `device_id` is as follows:
    // 1. Let `a` be `ddtp.PPN x 2^12^` and let `i = LEVELS - 1`. When
    //    `ddtp.iommu_mode` is `3LVL`, `LEVELS` is three. When `ddtp.iommu_mode` is
    //    `2LVL`, `LEVELS` is two. When `ddtp.iommu_mode` is `1LVL`, `LEVELS` is one.
    a = g_reg_file.ddtp.ppn * PAGESIZE;
    if ( g_reg_file.ddtp.iommu_mode == DDT_3LVL ) LEVELS = 3;
    if ( g_reg_file.ddtp.iommu_mode == DDT_2LVL ) LEVELS = 2;
    if ( g_reg_file.ddtp.iommu_mode == DDT_1LVL ) LEVELS = 1;
    i = LEVELS - 1;

step_2:
    // 2. If `i == 0` go to step 8.
    if ( i == 0 ) goto step_8;

    // Count walks in DDT
    count_events(pid_valid, process_id, 0, 0, device_id, 0, 0, DDT_WALKS);

    // 3. Let `ddte` be value of eight bytes at address `a + DDI[i] x 8`. If accessing
    //    `ddte` violates a PMA or PMP check, then stop and report "DDT entry load
    //     access fault" (cause = 257).
    status = read_memory((a + (DDI[i] * 8)), 8, (char *)&ddte.raw);
    if ( status & ACCESS_FAULT ) {
        *cause = 257;     // DDT entry load access fault
        return 1;
    }

    // 4. If `ddte` access detects a data corruption (a.k.a. poisoned data), then
    //    stop and report "DDT data corruption" (cause = 268). This fault is detected
    //    if the IOMMU supports the RAS capability (`capabilities.RAS == 1`).
    if ( (status & DATA_CORRUPTION) ) {
        *cause = 268;     // DDT data corruption
        return 1;
    }

    // 5. If `ddte.V == 0`, stop and report "DDT entry not valid" (cause = 258).
    if ( ddte.V == 0 ) {
        *cause = 258;     // DDT entry not valid
        return 1;
    }

    // 6. If if any bits or encoding that are reserved for future standard use are
    //    set within `ddte`, stop and report "DDT entry misconfigured"
    //    (cause = 259).
    if ( ddte.reserved0 != 0 || ddte.reserved1 != 0 ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }

    // 7. Let `i = i - 1` and let `a = ddte.PPN x 2^12^`. Go to step 2.
    i = i - 1;
    a = ddte.PPN * PAGESIZE;
    goto step_2;

step_8:
    // Count walks in DDT
    count_events(pid_valid, process_id, 0, 0, device_id, 0, 0, DDT_WALKS);

    // 8. Let `DC` be value of `DC_SIZE` bytes at address `a + DDI[0] * DC_SIZE`. If
    //    `capabilities.MSI_FLAT` is 1 then `DC_SIZE` is 64-bytes else it is 32-bytes.
    //    If accessing `DC` violates a PMA or PMP check, then stop and report
    //    "DDT entry load access fault" (cause = 257). If `DC` access detects a data
    //    corruption (a.k.a. poisoned data), then stop and report "DDT data corruption"
    //    (cause = 268). This fault is detected if the IOMMU supports the RAS capability
    //    (`capabilities.RAS == 1`).
    DC_SIZE = ( g_reg_file.capabilities.msi_flat == 1 ) ? EXT_FORMAT_DC_SIZE : BASE_FORMAT_DC_SIZE;
    status = read_memory((a + (DDI[0] * DC_SIZE)), DC_SIZE, (char *)DC);
    if ( status & ACCESS_FAULT ) {
        *cause = 257;     // DDT entry load access fault
        return 1;
    }
    if ( status & DATA_CORRUPTION ) {
        *cause = 268;     // DDT data corruption
        return 1;
    }
    // 9. If `DC.tc.V == 0`, stop and report "DDT entry not valid" (cause = 258).
    if ( DC->tc.V == 0 ) {
        *cause = 258;     // DDT entry not valid
        return 1;
    }
    //10. If any bits or encoding that are reserved for future standard use are set
    //    within `DC`, stop and report "DDT entry misconfigured" (cause = 259).
    if ( ((g_reg_file.capabilities.msi_flat == 1) && (DC->reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msiptp.reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msi_addr_mask.reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msi_addr_pattern.reserved != 0)) ||
         (DC->tc.reserved != 0) ||
         (DC->fsc.pdtp.reserved != 0 && DC->tc.PDTV == 1) ||
         (DC->fsc.iosatp.reserved != 0 && DC->tc.PDTV == 0) ||
         (DC->ta.reserved != 0) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    if ( (DC->iohgatp.MODE != IOHGATP_Bare) &&
         (DC->iohgatp.MODE != IOHGATP_Sv32x4) &&
         (DC->iohgatp.MODE != IOHGATP_Sv39x4) &&
         (DC->iohgatp.MODE != IOHGATP_Sv48x4) &&
         (DC->iohgatp.MODE != IOHGATP_Sv57x4) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    if ( (DC->tc.PDTV == 0) && 
         ((DC->fsc.iosatp.MODE != IOSATP_Bare) &&
          (DC->fsc.iosatp.MODE != IOSATP_Sv32) &&
          (DC->fsc.iosatp.MODE != IOSATP_Sv39) &&
          (DC->fsc.iosatp.MODE != IOSATP_Sv48) &&
          (DC->fsc.iosatp.MODE != IOSATP_Sv57)) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. If any of the following conditions are true then stop and report 
    //    "DDT entry misconfigured" (cause = 259).
    //    a. `capabilities.ATS` is 0 and `DC.tc.EN_ATS`, or `DC.tc.EN_PRI`, 
    //       or `DC.tc.PRPR` is 1
    //    b. `DC.tc.EN_ATS` is 0 and `DC.tc.T2GPA` is 1
    //    c. `DC.tc.EN_ATS` is 0 and `DC.tc.EN_PRI` is 1
    //    d. `DC.tc.EN_PRI` is 0 and `DC.tc.PRPR` is 1
    //    e. `capabilities.T2GPA` is 0 and `DC.tc.T2GPA` is 1
    if ( ((DC->tc.EN_ATS || DC->tc.EN_PRI || DC->tc.PRPR) &&
          (g_reg_file.capabilities.ats == 0)) ||
         ((DC->tc.EN_ATS == 0) && (DC->tc.T2GPA == 1 || DC->tc.EN_PRI == 1)) ||
         ((DC->tc.EN_PRI == 0) && (DC->tc.PRPR == 1)) ||
         (DC->tc.T2GPA && (g_reg_file.capabilities.t2gpa == 0)) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. If any of the following conditions are true then stop and report 
    //    "DDT entry misconfigured" (cause = 259).
    //    f. `DC.tc.PDTV` is 1 and `DC.fsc.pdtp.MODE` is not a supported mode
    //        (Table 2)
    if ( (DC->tc.PDTV == 1) && 
         ((DC->fsc.pdtp.MODE != PDTP_Bare) &&
          (DC->fsc.pdtp.MODE != PD20) &&
          (DC->fsc.pdtp.MODE != PD17) &&
          (DC->fsc.pdtp.MODE != PD8)) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. If any of the following conditions are true then stop and report 
    //    "DDT entry misconfigured" (cause = 259).
    //    g. DC.tc.PDTV is 0 and DC.fsc.iosatp.MODE is not one of the supported modes
    //         i. capabilities.Sv32 is 0 and DC.fsc.iosatp.MODE is Sv32
    //        ii. capabilities.Sv39 is 0 and DC.fsc.iosatp.MODE is Sv39
    //       iii. capabilities.Sv48 is 0 and DC.fsc.iosatp.MODE is Sv48
    //        iv. capabilities.Sv57 is 0 and DC.fsc.iosatp.MODE is Sv57
    if ( (DC->tc.PDTV == 0) && 
         (((DC->fsc.iosatp.MODE == IOSATP_Sv32) && (g_reg_file.capabilities.Sv32 == 0)) ||
          ((DC->fsc.iosatp.MODE == IOSATP_Sv39) && (g_reg_file.capabilities.Sv39 == 0)) ||
          ((DC->fsc.iosatp.MODE == IOSATP_Sv48) && (g_reg_file.capabilities.Sv48 == 0)) ||
          ((DC->fsc.iosatp.MODE == IOSATP_Sv57) && (g_reg_file.capabilities.Sv57 == 0))) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. If any of the following conditions are true then stop and report 
    //    "DDT entry misconfigured" (cause = 259).
    //    h. `capabilities.Sv32x4` is 0 and `DC.iohgatp.MODE` is `Sv32x4`
    //    i. `capabilities.Sv39x4` is 0 and `DC.iohgatp.MODE` is `Sv39x4`
    //    j. `capabilities.Sv48x4` is 0 and `DC.iohgatp.MODE` is `Sv48x4`
    //    k. `capabilities.Sv57x4` is 0 and `DC.iohgatp.MODE` is `Sv57x4`
    if ( ((DC->iohgatp.MODE == IOHGATP_Sv32x4) && (g_reg_file.capabilities.Sv32x4 == 0)) ||
         ((DC->iohgatp.MODE == IOHGATP_Sv39x4) && (g_reg_file.capabilities.Sv39x4 == 0)) ||
         ((DC->iohgatp.MODE == IOHGATP_Sv48x4) && (g_reg_file.capabilities.Sv48x4 == 0)) ||
         ((DC->iohgatp.MODE == IOHGATP_Sv57x4) && (g_reg_file.capabilities.Sv57x4 == 0)) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. If any of the following conditions are true then stop and report 
    //    "DDT entry misconfigured" (cause = 259).
    //    l. `capabilities.MSI_FLAT` is 1 and `DC.msiptp.MODE` is not `Bare` 
    //       and not `Flat`  
    if ( (g_reg_file.capabilities.msi_flat == 1) && 
         ((DC->msiptp.MODE != MSIPTP_Bare) &&
          (DC->msiptp.MODE != MSIPTP_Flat)) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //11. The device-context has been successfully located and may be cached.
    cache_ioatc_dc(device_id, DC);
    return 0;
}
