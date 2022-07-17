// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"
uint8_t
g_stage_address_translation(
    uint64_t gpa, uint8_t is_read, uint8_t is_write, uint8_t is_exec, uint8_t implicit,
    iohgatp_t iohgatp, uint32_t *cause, uint64_t *iotval2, 
    uint64_t *resp_pa, uint64_t *gst_page_sz,
    uint8_t *GR, uint8_t *GW, uint8_t *GX, uint8_t *GD, uint8_t *GPBMT,
    uint8_t pid_valid, uint32_t process_id, uint8_t PSCV, uint32_t PSCID, uint32_t device_id,
    uint8_t GV, uint32_t GSCID, uint8_t TTYP) {

    uint16_t vpn[5];
    uint16_t ppn[5];
    gpte_t gpte;
    uint8_t i, PTESIZE, LEVELS, status, do_guest_page_fault;
    uint64_t a;
    uint64_t gpa_upper_bits;

    *GR = *GW = *GX = *GPBMT = 0;
    *gst_page_sz = PAGESIZE;

    if ( iohgatp.MODE == IOHGATP_Bare ) {
        // No translation or protection.
        gpte.raw = 0;
        gpte.PPN = gpa / PAGESIZE;
        gpte.D = gpte.A = gpte.G = gpte.U = gpte.X = gpte.W = gpte.R = gpte.V = 1;
        gpte.PBMT = PMA;
        // Indicate G-stage page size as largest possible page size
        *gst_page_sz = (uint64_t)512 * (uint64_t)512 * (uint64_t)512 * (uint64_t)512 * PAGESIZE;
        goto step_8;
    }
    // 1. Let a be satp.ppn × PAGESIZE, and let i = LEVELS − 1. PAGESIZE is 2^12. (For Sv32, 
    //    LEVELS=2, For Sv39 LEVELS=3, For Sv48 LEVELS=4, For Sv57 LEVELS=5.) The satp register 
    //    must be active, i.e., the effective privilege mode must be S-mode or U-mode.
    if ( iohgatp.MODE == IOHGATP_Sv32x4 ) {
        vpn[0] = get_bits(21, 12, gpa);
        vpn[1] = get_bits(34, 22, gpa);
        gpa_upper_bits = 0;
        LEVELS = 2;
        PTESIZE = 4;
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(40, 30, gpa);
        gpa_upper_bits = get_bits(63, 41, gpa);
        LEVELS = 3;
        PTESIZE = 8;
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(49, 39, gpa);
        gpa_upper_bits = get_bits(63, 50, gpa);
        LEVELS = 4;
        PTESIZE = 8;
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 ) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(47, 39, gpa);
        vpn[4] = get_bits(58, 48, gpa);
        gpa_upper_bits = get_bits(63, 59, gpa);
        LEVELS = 5;
        PTESIZE = 8;
    }
    // Address bits 63:MAX_GPA must all be zeros, or else a 
    // guest-page-fault exception occurs.
    if ( gpa_upper_bits != 0 ) goto guest_page_fault;

    i = LEVELS - 1;

    // The root page table as determined by `iohgatp.PPN` is 16 KiB and must be aligned
    // to a 16-KiB boundary.  If the root page table is not aligned to 16 KiB as 
    // required, then all entries in that G-stage root page table appear to an IOMMU as
    // `UNSPECIFIED` and any address an IOMMU may compute and use for accessing an
    // entry in the root page table is also `UNSPECIFIED`.
    a = iohgatp.PPN * PAGESIZE;

step_2:
    // Count G stage page walks
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, G_PT_WALKS);

    // 2. Let gpte be the value of the PTE at address a+gpa.vpn[i]×PTESIZE. (For 
    //    Sv32x4 PTESIZE=4. and for all other modes PTESIZE=8). If accessing pte
    //    violates a PMA or PMP check, raise an access-fault exception 
    //    corresponding to the original access type.
    gpte.raw = 0;
    status = read_memory((a | (vpn[i] * PTESIZE)), PTESIZE, (char *)&gpte.raw);
    if ( status != 0 ) goto access_fault;

    // 3. If pte.v = 0, or if pte.r = 0 and pte.w = 1, or if any bits or 
    //    encodings that are reserved for future standard use are set within pte,
    //    stop and raise a page-fault exception to the original access type.
    if ( (gpte.V == 0) || (gpte.R == 0 && gpte.W == 1) || 
         ((gpte.PBMT != 0) && (g_reg_file.capabilities.Svpbmt == 0)) ||
         (gpte.reserved0 != 0) ||
         (gpte.reserved1 != 0) )
        goto guest_page_fault;

    // 4. Otherwise, the PTE is valid. If gpte.r = 1 or gpte.x = 1, go to step 5. 
    //    Otherwise, this PTE is a pointer to the next level of the page table. 
    //    Let i = i − 1. If i < 0, stop and raise a page-fault exception 
    //    corresponding to the original access type. Otherwise, let 
    //    a = gpte.ppn × PAGESIZE and go to step 2.
    if ( gpte.R == 1 || gpte.X == 1 ) goto step_5;

    i = i - 1;
    if ( i < 0 ) goto guest_page_fault;
    a = gpte.PPN * PAGESIZE;
    goto step_2;

step_5:
    // 5. A leaf PTE has been found. Determine if the requested memory access 
    //    is allowed by the pte.r, pte.w, pte.x, and pte.u bits, given the current 
    //    privilege mode and the value of the SUM and MXR fields of the mstatus 
    //    register. If not, stop and raise a page-fault exception corresponding to 
    //    the original access type.
    // g-stage page table specifc notes:
    //    when checking the U bit, the current privilege mode is always taken 
    //    to be U-mode; - impiies that U must be always 1 to be legal
    // For PCIe ATS Translation Requests:
    //   If the translation could be successfully completed but the requested 
    //   permissions are not present (Execute requested but no execute permission; 
    //   no-write not requested and no write permission; no read permission) then a 
    //   Success response is returned with the denied permission (R, W or X) set to 0
    //   and the other permission bits set to value determined from the page tables. 
    //   The X permission is granted only if the R permission is also granted. 
    //   Execute-only translations are not compatible with PCIe ATS as PCIe requires 
    //   read permission to be granted if the execute permission is granted.
    //   No faults are caused here - the denied permissions will be reported back in
    //   the ATS completion
    if ( (TTYP != PCIE_ATS_TRANSLATION_REQUEST) && (implicit == 0) ) {
        if ( is_exec  && (gpte.X == 0) ) goto guest_page_fault;
        if ( is_read  && (gpte.R == 0) ) goto guest_page_fault;
        if ( is_write && (gpte.W == 0) ) goto guest_page_fault;
    }
    if ( gpte.U == 0 )               goto guest_page_fault;

    ppn[4] = ppn[3] = ppn[2] = ppn[1] = ppn[0] = 0;
    if ( iohgatp.MODE == IOHGATP_Sv32x4 ) {
        ppn[0] = get_bits(19, 10, gpte.raw);
        ppn[1] = get_bits(31, 20, gpte.raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 ) {
        ppn[0] = get_bits(18, 10, gpte.raw);
        ppn[1] = get_bits(27, 19, gpte.raw);
        ppn[2] = get_bits(53, 28, gpte.raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 ) {
        ppn[0] = get_bits(18, 10, gpte.raw);
        ppn[1] = get_bits(27, 19, gpte.raw);
        ppn[2] = get_bits(36, 28, gpte.raw);
        ppn[3] = get_bits(53, 37, gpte.raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 ) {
        ppn[0] = get_bits(18, 10, gpte.raw);
        ppn[1] = get_bits(27, 19, gpte.raw);
        ppn[2] = get_bits(36, 28, gpte.raw);
        ppn[3] = get_bits(45, 37, gpte.raw);
        ppn[4] = get_bits(53, 46, gpte.raw);
    }
    // 6. If i > 0 and gpte.ppn[i − 1 : 0] ̸= 0, this is a misaligned superpage; 
    // stop and raise a page-fault exception corresponding to the original 
    // access type.
    if ( i > 0 ) {
        switch ( (i - 1) ) {
            case 3: if ( ppn[3] ) goto guest_page_fault;
            case 2: if ( ppn[2] ) goto guest_page_fault;
            case 1: if ( ppn[1] ) goto guest_page_fault;
            case 0: if ( ppn[0] ) goto guest_page_fault;
        }
        // Determine page size
        if ( iohgatp.MODE == IOHGATP_Sv32x4 ) {
            *gst_page_sz = 4 * 1024 * 1024;  // 4M;
        } else {
            *gst_page_sz = PAGESIZE;
            switch (i) {
                case 4: *gst_page_sz *= 512; // 256TiB
                case 3: *gst_page_sz *= 512; // 512GiB
                case 2: *gst_page_sz *= 512; //   1GiB
                case 1: *gst_page_sz *= 512; //   2MiB
            }
        }
    }

    // IOMMU A/D bit behavior:
    //    When `capabilities.AMO` is 1, the IOMMU supports updating the A and D bits in
    //    PTEs atomically. If `capabilities.AMO` is 0, the IOMMU ignores the A and D bits
    //    in the PTEs; the IOMMU does not update the A or D bits and does not cause any
    //    faults based on A and/or D bit being 0.
    //    The A and/or D bit updates by the IOMMU must follow the rules specified by the
    //    Privileged specification for validity, permission checking, and atomicity.
    //    The PTE update must be globally visible before a memory access using the
    //    translated address provided by the IOMMU becomes globally visible.
    //    Specifically, When the translated address is provided to a device in an ATS
    //    Translation completion, the PTE update must be globally visible before a memory
    //    access from the device using the translated address becomes globally visible.
    if ( g_reg_file.capabilities.amo == 0 ) goto step_8;

    // If pte.a = 0, or if the original memory access is a store and pte.d = 0,
    // then set A/D bits.
    if ( (gpte.A == 1) && ((gpte.D == 1) | (is_write == 0)) ) goto step_8;

    // Count G stage page walks
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, G_PT_WALKS);

    // Set A/D bits if needed
    do_guest_page_fault = 0;
    status = read_memory_for_AMO((a + (vpn[i] * PTESIZE)), PTESIZE, (char *)&gpte.raw);
    if ( status != 0 ) goto access_fault;
    if ( (TTYP != PCIE_ATS_TRANSLATION_REQUEST) && (implicit == 0) ) {
        // Determine if the reloaded PTE causes a fault
        // See note about PCIe ATS translation requests in step 5
        if ( is_exec  && (gpte.X == 0) ) do_guest_page_fault = 1;
        if ( is_read  && (gpte.R == 0) ) do_guest_page_fault = 1;
        if ( is_write && (gpte.W == 0) ) do_guest_page_fault = 1;
    }
    if ( gpte.U == 0 )               do_guest_page_fault = 1;
    // If no faults detected then set A and if required D bit
    if ( do_guest_page_fault == 0 ) gpte.A = 1;
    if ( do_guest_page_fault == 0 && is_write ) gpte.D = 1;
    status = write_memory((char *)&gpte.raw, (a + (vpn[i] * PTESIZE)), PTESIZE);
    if ( status != 0 ) goto access_fault;
    if ( do_guest_page_fault == 1) goto guest_page_fault;

step_8:
    // 8. The translation is successful.
    // The translated physical address is given as follows:
    // pa.pgoff = va.pgoff.
    // If i > 0, then this is a superpage translation and pa.ppn[i − 1 : 0] = va.vpn[i − 1 : 0].
    // pa.ppn[LEVELS − 1 : i] = pte.ppn[LEVELS − 1 : i]
    *resp_pa = ((gpte.PPN * PAGESIZE) & ~(*gst_page_sz - 1)) | (gpa & (*gst_page_sz - 1));
    *GR = gpte.R;
    *GW = gpte.W;
    *GX = gpte.X;
    *GD = gpte.D;
    *GPBMT = gpte.PBMT;
    return 0;

guest_page_fault:
    // Stop and raise a page-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) *cause = 20;      // Instruction guest page fault
    else if ( is_read ) *cause = 21; // Read guest page fault
    else *cause = 23;                // Write/AMO guest page fault
    // If the CAUSE is a guest-page fault then the guest-physical-address 
    // right shifted by 2 is reported in iotval2[63:2]. If bit 0 of 
    // iotval2 is 1, then guest-page-fault was caused by an implicit 
    // memory access for VS-stage address translation. If bit 0 of 
    // iotval2 is 1, and the implicit access was a write then bit 1 
    // is set to 1 else its set to 0.
    *iotval2 = (gpa >> 2) << 2;
    *iotval2 |= implicit;
    *iotval2 |= ((implicit & is_write) << 1);
    return 1;

access_fault:    
    // Stop and raise a access-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) *cause = 1;       // Instruction access fault
    else if ( is_read ) *cause = 5;  // Read access fault
    else *cause = 7;                 // Write/AMO access fault
    *iotval2 = 0;
    return 1;
}
