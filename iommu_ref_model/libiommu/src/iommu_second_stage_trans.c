// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"
uint8_t
second_stage_address_translation(
    uint64_t gpa, uint8_t check_access_perms, uint32_t DID,
    uint8_t is_read, uint8_t is_write, uint8_t is_exec, uint8_t is_implicit,
    uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    uint8_t GV, uint32_t GSCID, iohgatp_t iohgatp, uint8_t GADE, uint8_t SADE,
    uint8_t SXL, uint64_t *pa, uint64_t *gst_page_sz, gpte_t *gpte,
    uint32_t rcid, uint32_t mcid) {

    uint16_t vpn[5];
    uint16_t ppn[5];
    gpte_t amo_gpte;
    uint8_t PTESIZE, LEVELS, status, gpte_changed;
    int8_t i;
    uint64_t a;
    uint64_t gpa_upper_bits;
    uint64_t pa_mask = ((1UL << (g_reg_file.capabilities.pas)) - 1);

    *gst_page_sz = PAGESIZE;

    if ( iohgatp.MODE == IOHGATP_Bare ) {
        // No translation or protection.
        gpte->raw = 0;
        gpte->PPN = gpa / PAGESIZE;
        gpte->D = gpte->A = gpte->U = 1;
        gpte->X = gpte->W = gpte->R = gpte->V = 1;
        gpte->N = gpte->G = 0;
        gpte->PBMT = PMA;
        // The translation range size returned in a Success response to
        // an ATS translation request, when either stages of address
        // translation are Bare, is implementation-defined. However, it
        // is recommended that the translation range size be large, such
        // as 2 MiB or 1 GiB.
        if ( g_reg_file.capabilities.Sv57x4 == 1 )
            *gst_page_sz = g_sv57_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv48x4 == 1 )
            *gst_page_sz = g_sv48_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv39x4 == 1 && g_reg_file.fctl.gxl == 0)
            *gst_page_sz = g_sv39_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv32x4 == 1 && g_reg_file.fctl.gxl == 1)
            *gst_page_sz = g_sv32_bare_pg_sz;
        goto step_8;
    }

    // 1. Let a be satp.ppn × PAGESIZE, and let i = LEVELS − 1. PAGESIZE is 2^12. (For Sv32,
    //    LEVELS=2, For Sv39 LEVELS=3, For Sv48 LEVELS=4, For Sv57 LEVELS=5.) The satp register
    //    must be active, i.e., the effective privilege mode must be S-mode or U-mode.
    if ( iohgatp.MODE == IOHGATP_Sv32x4 && g_reg_file.fctl.gxl == 1 ) {
        vpn[0] = get_bits(21, 12, gpa);
        vpn[1] = get_bits(34, 22, gpa);
        gpa_upper_bits = 0;
        LEVELS = 2;
        PTESIZE = 4;
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 && g_reg_file.fctl.gxl == 0) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(40, 30, gpa);
        gpa_upper_bits = get_bits(63, 41, gpa);
        LEVELS = 3;
        PTESIZE = 8;
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 && g_reg_file.fctl.gxl == 0) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(49, 39, gpa);
        gpa_upper_bits = get_bits(63, 50, gpa);
        LEVELS = 4;
        PTESIZE = 8;
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 && g_reg_file.fctl.gxl == 0) {
        vpn[0] = get_bits(20, 12, gpa);
        vpn[1] = get_bits(29, 21, gpa);
        vpn[2] = get_bits(38, 30, gpa);
        vpn[3] = get_bits(47, 39, gpa);
        vpn[4] = get_bits(58, 48, gpa);
        gpa_upper_bits = get_bits(63, 59, gpa);
        LEVELS = 5;
        PTESIZE = 8;
    }
    if ( SXL == 1 ) {
        // When `SXL` is 1, the following rules apply:
        // * If the G-stage page table is not `Bare`, then
        //   a guest page fault corresponding to the original
        //   access type occurs if the incoming GPA has bits
        //   set beyond bit 33.
        gpa_upper_bits = get_bits(63, 34, gpa);
    }
    // Address bits 63:MAX_GPA must all be zeros, or else a
    // guest-page-fault exception occurs.
    if ( gpa_upper_bits != 0 ) return GST_PAGE_FAULT;

    i = LEVELS - 1;

    // The root page table as determined by `iohgatp.PPN` is 16 KiB and must be aligned
    // to a 16-KiB boundary.  If the root page table is not aligned to 16 KiB as
    // required, then all entries in that G-stage root page table appear to an IOMMU as
    // `UNSPECIFIED` and any address an IOMMU may compute and use for accessing an
    // entry in the root page table is also `UNSPECIFIED`.
    a = iohgatp.PPN * PAGESIZE;

step_2:
    // Count G stage page walks
    count_events(PV, PID, PSCV, PSCID, DID, GV, GSCID, G_PT_WALKS);

    // 2. Let gpte be the value of the PTE at address a+gpa.vpn[i]×PTESIZE. (For
    //    Sv32x4 PTESIZE=4. and for all other modes PTESIZE=8). If accessing pte
    //    violates a PMA or PMP check, raise an access-fault exception
    //    corresponding to the original access type.
    //    If the address is beyond the maximum physical address width of the machine
    //    then an access fault occurs
    if ( a & ~pa_mask ) return GST_ACCESS_FAULT;
    gpte->raw = 0;
    status = read_memory((a | (vpn[i] * PTESIZE)), PTESIZE, (char *)&gpte->raw,
                         rcid, mcid);
    if ( status & ACCESS_FAULT ) return GST_ACCESS_FAULT;
    if ( status & DATA_CORRUPTION) return GST_DATA_CORRUPTION;

    // 3. If pte.v = 0, or if pte.r = 0 and pte.w = 1, or if any bits or
    //    encodings that are reserved for future standard use are set within pte,
    //    stop and raise a page-fault exception to the original access type.
    if ( (gpte->V == 0) || (gpte->R == 0 && gpte->W == 1) ||
         ((gpte->PBMT != 0) && (g_reg_file.capabilities.Svpbmt == 0)) ||
         (gpte->PBMT == 3) ||
         (gpte->reserved != 0) )
        return GST_PAGE_FAULT;

    // NAPOT PTEs behave identically to non-NAPOT PTEs within the address-translation
    // algorithm in Section 4.3.2, except that:
    // a. If the encoding in pte is valid according to Table 5.1, then instead of
    //    returning the original value of pte, implicit reads of a NAPOT PTE
    //    return a copy of pte in which pte.ppn[pte.napot bits − 1 : 0] is replaced
    //    by vpn[i][pte.napot bits − 1 : 0]. If the encoding in pte is reserved
    //    according to Table 5.1, then a page-fault exception must be raised.
    //    i     pte.ppn[i]     Description                   pte.napot bits
    //    0    x xxxx xxx1      Reserved                           −
    //    0    x xxxx xx1x      Reserved                           −
    //    0    x xxxx x1xx      Reserved                           −
    //    0    x xxxx 1000      64 KiB contiguous region           4
    //    0    x xxxx 0xxx      Reserved                           −
    //    ≥ 1  x xxxx xxxx      Reserved                           −
    // b. Implicit reads of NAPOT page table entries may create address-translation
    //    cache entries mapping a + va.vpn[j] × PTESIZE to a copy of pte in which
    //    pte.ppn[pte.napot bits − 1 : 0] is replaced by vpn[0][pte.napot bits − 1 : 0],
    //    for any or all j such that j[8 : napot bits] = i[8 : napot bits], all for
    //    the address space identified in satp as loaded by step 0.
    if ( i != 0 && gpte->N ) return GST_PAGE_FAULT;

    // 4. Otherwise, the PTE is valid. If gpte.r = 1 or gpte.x = 1, go to step 5.
    //    Otherwise, this PTE is a pointer to the next level of the page table.
    //    Let i = i − 1. If i < 0, stop and raise a page-fault exception
    //    corresponding to the original access type. Otherwise, let
    //    a = gpte.ppn × PAGESIZE and go to step 2.
    if ( gpte->R == 1 || gpte->X == 1 ) goto step_5;

    // For non-leaf PTEs, bits 62–61 are reserved for future standard use. Until
    // their use is defined by a standard extension, they must be cleared by
    // software for forward compatibility, or else a page-fault exception is raised.
    if ( gpte->PBMT != 0 ) return GST_PAGE_FAULT;

    // For non-leaf PTEs, the D, A, and U bits are reserved for future standard use.
    if ( gpte->D != 0 || gpte->A != 0 || gpte->U != 0) return GST_PAGE_FAULT;

    i = i - 1;
    if ( i < 0 ) return GST_PAGE_FAULT;
    a = gpte->PPN * PAGESIZE;
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
    // Implicit accesses dont check permission against original access type
    if ( check_access_perms == 1 ) {
        if ( is_exec  && (gpte->X == 0) ) return GST_PAGE_FAULT;
        if ( is_read  && (gpte->R == 0) ) return GST_PAGE_FAULT;
        if ( is_write && (gpte->W == 0) ) return GST_PAGE_FAULT;
    }
    if ( gpte->U == 0 ) return GST_PAGE_FAULT;

    ppn[4] = ppn[3] = ppn[2] = ppn[1] = ppn[0] = 0;
    if ( iohgatp.MODE == IOHGATP_Sv32x4 && g_reg_file.fctl.gxl == 1) {
        ppn[0] = get_bits(19, 10, gpte->raw);
        ppn[1] = get_bits(31, 20, gpte->raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv39x4 && g_reg_file.fctl.gxl == 0) {
        ppn[0] = get_bits(18, 10, gpte->raw);
        ppn[1] = get_bits(27, 19, gpte->raw);
        ppn[2] = get_bits(53, 28, gpte->raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv48x4 && g_reg_file.fctl.gxl == 0) {
        ppn[0] = get_bits(18, 10, gpte->raw);
        ppn[1] = get_bits(27, 19, gpte->raw);
        ppn[2] = get_bits(36, 28, gpte->raw);
        ppn[3] = get_bits(53, 37, gpte->raw);
    }
    if ( iohgatp.MODE == IOHGATP_Sv57x4 && g_reg_file.fctl.gxl == 0) {
        ppn[0] = get_bits(18, 10, gpte->raw);
        ppn[1] = get_bits(27, 19, gpte->raw);
        ppn[2] = get_bits(36, 28, gpte->raw);
        ppn[3] = get_bits(45, 37, gpte->raw);
        ppn[4] = get_bits(53, 46, gpte->raw);
    }
    // 6. If i > 0 and gpte.ppn[i − 1 : 0] ̸= 0, this is a misaligned superpage;
    // stop and raise a page-fault exception corresponding to the original
    // access type.
    *gst_page_sz = PAGESIZE;
    if ( i > 0 ) {
        switch ( i ) {
            case 4: if ( ppn[3] ) return GST_PAGE_FAULT;
                    *gst_page_sz *= 512UL; // 256TiB
            case 3: if ( ppn[2] ) return GST_PAGE_FAULT;
                    *gst_page_sz *= 512UL; // 512GiB
            case 2: if ( ppn[1] ) return GST_PAGE_FAULT;
                    *gst_page_sz *= 512UL; // 1GiB
            case 1: if ( ppn[0] ) return GST_PAGE_FAULT;
                    *gst_page_sz *= 512UL; // 2MiB
                    if ( iohgatp.MODE == IOHGATP_Sv32x4 &&
                         g_reg_file.fctl.gxl == 1 ) {
                        *gst_page_sz *= 2UL; // 4MiB
                    }
        }
    }

    // a. If the encoding in pte is valid according to Table 5.1, then instead of
    //    returning the original value of pte, implicit reads of a NAPOT PTE
    //    return a copy of pte in which pte.ppn[pte.napot bits − 1 : 0] is replaced
    //    by vpn[i][pte.napot bits − 1 : 0]. If the encoding in pte is reserved
    //    according to Table 5.1, then a page-fault exception must be raised.
    //    i     pte.ppn[i]     Description                   pte.napot bits
    //    0    x xxxx xxx1      Reserved                           −
    //    0    x xxxx xx1x      Reserved                           −
    //    0    x xxxx x1xx      Reserved                           −
    //    0    x xxxx 1000      64 KiB contiguous region           4
    //    0    x xxxx 0xxx      Reserved                           −
    //    ≥ 1  x xxxx xxxx      Reserved                           −
    if ( i == 0 && gpte->N && ((gpte->PPN & 0xF) != 0x8) )
        return GST_PAGE_FAULT;

    // 7. If pte.a = 0, or if the original memory access is a store and pte.d = 0,
    //    If `GADE` is 1, the IOMMU updates A and D bits in G-stage PTEs atomically. If
    //    `GADE` is 0, the IOMMU causes a guest-page-fault corresponding to the original
    //    access type if A bit is 0 or if the memory access is a store and the D bit is 0.
    //    If the G-stage was invoked for a implicit walk then set D bit if its
    //    not already 0, if HW A/D updating for first stage is enabled (SADE is 1),
    //    HW A/D updating for G-stage is enabled (GADE is 1), and PTE provides
    //    write permission.
    //    For IOMMU updating of A/D bits the following steps are performed:
    //    - If a store to pte would violate a PMA or PMP check, raise an access-fault exception
    //      corresponding to the original access type.
    //    Perform the following steps atomically:
    //    – Compare pte to the value of the PTE at address a + va.vpn[i] × PTESIZE.
    //    – If the values match, set pte.a to 1 and, if the original memory access is a store,
    //      also set pte.d to 1.
    //    – If the comparison fails, return to step 2
    if ( (gpte->A == 1) && (gpte->D == 1 || is_write == 0) &&
         (gpte->D == 1 || is_implicit == 0 || gpte->W == 0 || SADE == 0) ) goto step_8;

    // A and/or D bit update needed
    if ( GADE == 0 ) return GST_PAGE_FAULT;

    // Count G stage page walks
    count_events(PV, PID, PSCV, PSCID, DID, GV, GSCID, G_PT_WALKS);
    amo_gpte.raw = 0;
    status = read_memory_for_AMO((a + (vpn[i] * PTESIZE)), PTESIZE,
                                 (char *)&amo_gpte.raw, rcid, mcid);

    if ( status & ACCESS_FAULT ) return GST_ACCESS_FAULT;
    if ( status & DATA_CORRUPTION) return GST_DATA_CORRUPTION;

    gpte_changed = (amo_gpte.raw == gpte->raw) ? 0 : 1;

    if ( gpte_changed == 0 ) {
        amo_gpte.A = 1;
        // The case for is_write == 1 && pte.W == 0 is to address ATS translation
        // requests that may request write permission when write permission does not
        // exist. If write permission exists then the D bit is set else D bit is not
        // set and the write permission is returned in responses as 0.
        if ( (is_write == 1 || is_implicit == 1) && (amo_gpte.W == 1) ) amo_gpte.D = 1;
    }

    status = write_memory((char *)&amo_gpte.raw, (a + (vpn[i] * PTESIZE)),
                          PTESIZE, rcid, mcid);

    if ( status & ACCESS_FAULT ) return GST_ACCESS_FAULT;
    if ( status & DATA_CORRUPTION) return GST_DATA_CORRUPTION;

    if ( gpte_changed == 1) goto step_2;

step_8:
    // 8. The translation is successful.

    // b. Implicit reads of NAPOT page table entries may create address-translation
    //    cache entries mapping a + va.vpn[j] × PTESIZE to a copy of pte in which
    //    pte.ppn[pte.napot bits − 1 : 0] is replaced by vpn[0][pte.napot bits − 1 : 0],
    //    for any or all j such that j[8 : napot bits] = i[8 : napot bits], all for
    //    the address space identified in satp as loaded by step 0.
    if ( gpte->N )
        gpte->PPN = (gpte->PPN & ~0xF) | ((gpa / PAGESIZE) & 0xF);

    // The translated physical address is given as follows:
    // pa.pgoff = va.pgoff.
    // If i > 0, then this is a superpage translation and pa.ppn[i − 1 : 0] = va.vpn[i − 1 : 0].
    // pa.ppn[LEVELS − 1 : i] = pte.ppn[LEVELS − 1 : i]
    *pa = ((gpte->PPN * PAGESIZE) & ~(*gst_page_sz - 1)) | (gpa & (*gst_page_sz - 1));
    return 0;
}
