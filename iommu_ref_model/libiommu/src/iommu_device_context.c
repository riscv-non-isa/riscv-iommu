// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"
uint8_t do_device_context_configuration_checks(device_context_t *DC);

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
    if ( do_device_context_configuration_checks(DC) ) {
        *cause = 259;     // DDT entry misconfigured
        return 1;
    }
    //12. The device-context has been successfully located and may be cached.
    cache_ioatc_dc(device_id, DC);
    return 0;
}
uint8_t 
do_device_context_configuration_checks(
    device_context_t *DC) {

    // A `DC` with `V=1` is determined to be misconfigured if any of the following
    // conditions are true. If misconfigured then stop and report "DDT entry
    // misconfigured" (cause = 259).

    // 1. If any bits or encoding that are reserved for future standard use are set.
    if ( ((g_reg_file.capabilities.msi_flat == 1) && (DC->reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msiptp.reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msi_addr_mask.reserved != 0)) ||
         ((g_reg_file.capabilities.msi_flat == 1) && (DC->msi_addr_pattern.reserved != 0)) ||
         (DC->tc.reserved0 != 0) ||
         (DC->tc.reserved1 != 0) ||
         (DC->fsc.pdtp.reserved != 0 && DC->tc.PDTV == 1) ||
         (DC->fsc.iosatp.reserved != 0 && DC->tc.PDTV == 0) ||
         (DC->ta.reserved0 != 0) ||
         (DC->ta.reserved1 != 0) ) {
        return 1;
    }
    // 2. `capabilities.ATS` is 0 and `DC.tc.EN_ATS`, or `DC.tc.EN_PRI`,
    //     or `DC.tc.PRPR` is 1
    if ( ((DC->tc.EN_ATS == 1 || DC->tc.EN_PRI == 1 || DC->tc.PRPR == 1) &&
          (g_reg_file.capabilities.ats == 0)) ) {
        return 1;
    }
    // 3. `DC.tc.EN_ATS` is 0 and `DC.tc.T2GPA` is 1
    if ( (DC->tc.EN_ATS == 0) && ( DC->tc.T2GPA == 1 ) ) {
        return 1;
    }
    // 4. `DC.tc.EN_ATS` is 0 and `DC.tc.EN_PRI` is 1
    if ( (DC->tc.EN_ATS == 0) && ( DC->tc.EN_PRI == 1 ) ) {
        return 1;
    }
    // 5. `DC.tc.EN_PRI` is 0 and `DC.tc.PRPR` is 1
    if ( (DC->tc.EN_PRI == 0) && (DC->tc.PRPR == 1) ) {
        return 1;
    }
    // 6. `capabilities.T2GPA` is 0 and `DC.tc.T2GPA` is 1
    if ( DC->tc.T2GPA && (g_reg_file.capabilities.t2gpa == 0) ) {
        return 1;
    }
    // 7. `DC.tc.T2GPA` is 1 and `DC.iohgatp.MODE` is `Bare`
    if ( DC->tc.T2GPA && (DC->iohgatp.MODE == IOHGATP_Bare) ) {
        return 1;
    }
    // 8. `DC.tc.PDTV` is 1 and `DC.fsc.pdtp.MODE` is not a supported mode
    //    a. `capabilities.PD20` is 0 and `DC.fsc.pdtp.MODE` is `PD20`
    //    b. `capabilities.PD17` is 0 and `DC.fsc.pdtp.MODE` is `PD17`
    //    c. `capabilities.PD8` is 0 and `DC.fsc.pdtp.MODE` is `PD8`
    if ( (DC->tc.PDTV == 1) && 
         ((DC->fsc.pdtp.MODE != PDTP_Bare) &&
          (DC->fsc.pdtp.MODE != PD20) &&
          (DC->fsc.pdtp.MODE != PD17) &&
          (DC->fsc.pdtp.MODE != PD8)) ) {
        return 1;
    }
    if ( (DC->tc.PDTV == 1) && 
         (((DC->fsc.pdtp.MODE == PD20) && (g_reg_file.capabilities.pd20 == 0)) ||
          ((DC->fsc.pdtp.MODE == PD17) && (g_reg_file.capabilities.pd17 == 0)) ||
          ((DC->fsc.pdtp.MODE == PD8) && (g_reg_file.capabilities.pd8 == 0))) ) {
        return 1;
    }
    // 9. `DC.tc.PDTV` is 0 and `DC.fsc.iosatp.MODE` encoding is not valid
    //    encoding as determined by <<IOSATP_MODE_ENC>>
    if ( (DC->tc.PDTV == 0) && (DC->tc.SXL == 0) && 
         (DC->fsc.iosatp.MODE != IOSATP_Bare) &&
         (DC->fsc.iosatp.MODE != IOSATP_Sv39) &&
         (DC->fsc.iosatp.MODE != IOSATP_Sv48) &&
         (DC->fsc.iosatp.MODE != IOSATP_Sv57) ) {
        return 1;
    }
    if ( (DC->tc.PDTV == 0) && (DC->tc.SXL == 1) && 
         (DC->fsc.iosatp.MODE != IOSATP_Bare) &&
         (DC->fsc.iosatp.MODE != IOSATP_Sv32) ) {
        return 1;
    }
    //10. `DC.tc.PDTV` is 0 and and `DC.tc.SXL` is 0 `DC.fsc.iosatp.MODE` is not one of the
    //    supported modes
    //    .. `capabilities.Sv39` is 0 and `DC.fsc.iosatp.MODE` is `Sv39`
    //    .. `capabilities.Sv48` is 0 and `DC.fsc.iosatp.MODE` is `Sv48`
    //    .. `capabilities.Sv57` is 0 and `DC.fsc.iosatp.MODE` is `Sv57`
    if ( (DC->tc.PDTV == 0) &&  (DC->tc.SXL == 0) && 
         (((DC->fsc.iosatp.MODE == IOSATP_Sv39) && (g_reg_file.capabilities.Sv39 == 0)) ||
          ((DC->fsc.iosatp.MODE == IOSATP_Sv48) && (g_reg_file.capabilities.Sv48 == 0)) ||
          ((DC->fsc.iosatp.MODE == IOSATP_Sv57) && (g_reg_file.capabilities.Sv57 == 0))) ) {
        return 1;
    }
    //11. `DC.tc.PDTV` is 0 and and `DC.tc.SXL` is 1 `DC.fsc.iosatp.MODE` is not one of the
    //    supported modes
    //    .. `capabilities.Sv32` is 0 and `DC.fsc.iosatp.MODE` is `Sv32`
    if ( (DC->tc.PDTV == 0) &&  (DC->tc.SXL == 1) && 
         ((DC->fsc.iosatp.MODE == IOSATP_Sv32) && (g_reg_file.capabilities.Sv32 == 0)) ) {
        return 1;
    }
    //12. `DC.tc.PDTV` is 0 and `DC.tc.DPE` is 1
    if ( (DC->tc.PDTV == 0) && (DC->tc.DPE == 1) ) {
        return 1;
    }
    //13. `DC.iohgatp.MODE` encoding is not a valid encoding as determined
    //    by <<IOHGATP_MODE_ENC>>
    if ( (DC->tc.PDTV == 0) && (g_reg_file.fctl.gxl == 0) && 
         (DC->iohgatp.MODE != IOHGATP_Bare) &&
         (DC->iohgatp.MODE != IOHGATP_Sv39x4) &&
         (DC->iohgatp.MODE != IOHGATP_Sv48x4) &&
         (DC->iohgatp.MODE != IOHGATP_Sv57x4) ) {
        return 1;
    }
    if ( (DC->tc.PDTV == 0) && (g_reg_file.fctl.gxl == 1) && 
         (DC->iohgatp.MODE != IOHGATP_Bare) &&
         (DC->iohgatp.MODE != IOHGATP_Sv32x4) ) {
        return 1;
    }
    //14. `fctl.GXL` is 0 and `DC.iohgatp.MODE` is not a supported mode
    //    a. `capabilities.Sv39x4` is 0 and `DC.iohgatp.MODE` is `Sv39x4`
    //    b. `capabilities.Sv48x4` is 0 and `DC.iohgatp.MODE` is `Sv48x4`
    //    c. `capabilities.Sv57x4` is 0 and `DC.iohgatp.MODE` is `Sv57x4`
    if ( (g_reg_file.fctl.gxl == 0) && 
         (((DC->iohgatp.MODE == IOHGATP_Sv39x4) && (g_reg_file.capabilities.Sv39x4 == 0)) ||
          ((DC->iohgatp.MODE == IOHGATP_Sv48x4) && (g_reg_file.capabilities.Sv48x4 == 0)) ||
          ((DC->iohgatp.MODE == IOHGATP_Sv57x4) && (g_reg_file.capabilities.Sv57x4 == 0))) ) {
        return 1;
    }
    //15. `fctl.GXL` is 1 and `DC.iohgatp.MODE` is not a supported mode
    //    a. `capabilities.Sv32x4` is 0 and `DC.iohgatp.MODE` is `Sv32x4`
    if ( (g_reg_file.fctl.gxl == 1) && 
         ((DC->iohgatp.MODE == IOHGATP_Sv32x4) && (g_reg_file.capabilities.Sv32x4 == 0)) ) {
        return 1;
    }
    //16. `capabilities.MSI_FLAT` is 1 and `DC.msiptp.MODE` is not `Bare`
    //    and not `Flat`
    if ( (g_reg_file.capabilities.msi_flat == 1) && 
         ((DC->msiptp.MODE != MSIPTP_Off) && (DC->msiptp.MODE != MSIPTP_Flat)) ) {
        return 1;
    }
    //17. `DC.iohgatp.MODE` is not `Bare` and the root page table determined by
    //    `DC.iohgatp.PPN` is not aligned to a 16-KiB boundary.
    if ( (DC->iohgatp.MODE != IOHGATP_Bare) && ((DC->iohgatp.PPN & 0x3UL) != 0) ) {
        return 1;
    }
    //18. `capabilities.AMO` is 0 and `DC.tc.SADE` or `DC.tc.GADE` is 1
    if ( g_reg_file.capabilities.amo == 0 && (DC->tc.SADE == 1 || DC->tc.GADE == 1) ) {
        return 1;
    }
    //19. `capabilities.END` is 0 and `fctl.be != DC.tc.SBE`
    if ( g_reg_file.capabilities.end == 0 && (g_reg_file.fctl.be != DC->tc.SBE) ) {
        return 1;
    }
    //20. `DC.tc.SXL` value is not a legal value. If `fctl.GXL` is 1, then
    //    `DC.tc.SXL` must be 1. If `fctl.GXL` is 0 and is writeable, then
    //    `DC.tc.SXL` may be 0 or 1. If `fctl.GXL` is 0 and is not writeable
    //    then `DC.tc.SXL` must be 0.
    if ( (g_reg_file.fctl.gxl == 1) && (DC->tc.SXL != 1) ) {
        return 1;
    }
    if ( (g_gxl_writeable == 0) && (DC->tc.SXL != 0) ) {
        return 1;
    }

    //21. `DC.tc.SBE` value is not a legal value. If `fctl.BE` is writeable
    //    then `DC.tc.SBE` may be 0 or 1. If `fctl.BE` is not writeable then
    //    `DC.tc.SBE` must be same as `fctl.BE`.
    if ( (g_fctl_be_writeable == 0) && (DC->tc.SBE != g_reg_file.fctl.be) ) {
        return 1;
    }

    return 0;
}
