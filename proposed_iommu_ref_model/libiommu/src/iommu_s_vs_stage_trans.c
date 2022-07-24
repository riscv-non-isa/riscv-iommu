// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

uint8_t
s_vs_stage_address_translation(
    uint64_t iova,
    uint8_t priv, uint8_t is_read, uint8_t is_write, uint8_t is_exec,
    uint8_t SUM, iosatp_t iosatp, uint32_t PSCID, iohgatp_t iohgatp, 
    uint32_t *cause, uint64_t *iotval2, uint64_t *resp_pa, uint64_t *page_sz,
    uint8_t *R, uint8_t *W, uint8_t *X, uint8_t *G, uint8_t *PBMT, uint8_t *UNTRANSLATED_ONLY,
    uint8_t pid_valid, uint32_t process_id, uint32_t device_id, uint8_t TTYP, uint8_t T2GPA) {

    uint16_t vpn[5];
    uint16_t ppn[5];
    pte_t pte;
    uint8_t NL_G = 1;
    uint8_t i, PTESIZE, LEVELS, status, do_page_fault;
    uint64_t a, masked_upper_bits, mask, napot_ppn, napot_iova, napot_gpa;
    uint8_t is_implicit_write = 0;
    uint64_t gst_page_sz, resp_gpa;
    uint8_t GR, GW, GX, GD, GPBMT;
    uint8_t ioatc_status, GV, PSCV;
    uint16_t GSCID;

    *R = *W = *X = *G = *PBMT = *UNTRANSLATED_ONLY = 0;
    GR = GW = GX = GD = 0;
    GPBMT = PMA;
    //*page_sz = PAGESIZE;
    *page_sz = (1UL << g_reg_file.capabilities.pas);
    *iotval2 = 0;

    // Lookup IOATC to determine if there is a cached translation
    PSCV = (iosatp.MODE == IOSATP_Bare) ? 0 : 1;
    GV = (iohgatp.MODE == IOHGATP_Bare) ? 0 : 1;
    GSCID = iohgatp.GSCID;
    if ( (ioatc_status = lookup_ioatc_iotlb(iova, priv, is_read, is_write, is_exec, SUM, PSCV, 
                        PSCID, GV, GSCID, cause, resp_pa, page_sz, R, W, X, G, PBMT)) == IOATC_FAULT )
        goto page_fault;

    // Hit in IOATC - complete translation.
    if ( ioatc_status == IOATC_HIT )
        return 0;

    // Count misses in TLB
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, IOATC_TLB_MISS);

    // Miss in IOATC - Walk page tables
    if ( iosatp.MODE == IOSATP_Bare ) {
        // No translation or protection.
        i = 0;
        pte.raw = 0;
        pte.PPN = iova / PAGESIZE;
        pte.D = pte.A = pte.G = pte.U = pte.X = pte.W = pte.R = pte.V = 1;
        pte.N = 0;
        pte.PBMT = PMA;
        goto step_8;
    }
    // 1. Let a be satp.ppn × PAGESIZE, and let i = LEVELS − 1. PAGESIZE is 2^12. (For Sv32, 
    //    LEVELS=2, For Sv39 LEVELS=3, For Sv48 LEVELS=4, For Sv57 LEVELS=5.) The satp register 
    //    must be active, i.e., the effective privilege mode must be S-mode or U-mode.
    if ( iosatp.MODE == IOSATP_Sv32 ) {
        vpn[0] = get_bits(21, 12, iova);
        vpn[1] = get_bits(31, 22, iova);
        LEVELS = 2;
        PTESIZE = 4;
        mask = 0;
        masked_upper_bits = 0;
    }
    if ( iosatp.MODE == IOSATP_Sv39 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        LEVELS = 3;
        PTESIZE = 8;
        mask = (1UL << (64 - 39)) - 1;
        masked_upper_bits = (iova >> 38) & mask;
    }
    if ( iosatp.MODE == IOSATP_Sv48 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        vpn[3] = get_bits(47, 39, iova);
        LEVELS = 4;
        PTESIZE = 8;
        mask = (1UL << (64 - 48)) - 1;
        masked_upper_bits = (iova >> 47) & mask;
    }
    if ( iosatp.MODE == IOSATP_Sv57 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        vpn[3] = get_bits(47, 39, iova);
        vpn[4] = get_bits(56, 48, iova);
        LEVELS = 5;
        PTESIZE = 8;
        mask = (1UL << (64 - 57)) - 1;
        masked_upper_bits = (iova >> 56) & mask;
    }
    // Instruction fetch addresses and load and store effective addresses, 
    // which are 64 bits, must have bits 63:<VASIZE> all equal to bit 
    // (VASIZE-1), or else a page-fault exception will occur.
    // Do the address is canonical check
    if ( masked_upper_bits != 0  && masked_upper_bits != mask ) goto page_fault;

    i = LEVELS - 1;
    a = iosatp.PPN * PAGESIZE;
step_2:
    // 2. Let pte be the value of the PTE at address a+va.vpn[i]×PTESIZE. (For 
    //    Sv32 PTESIZE=4. and for all other modes PTESIZE=8). If accessing pte
    //    violates a PMA or PMP check, raise an access-fault exception 
    //    corresponding to the original access type.
    pte.raw = 0;

    // Invoke G-stage page table to translate the PTE address if G-stage page
    // table is active.
    // If IOMMU supports HW A/D bit update the implicit accesses are treated
    // as writes. This avoids the IOMMU needing to go back in time to set D bit
    // in G-stage page tables if A or D bit needs to be set in VS stage page
    // table.
    is_implicit_write = ( g_reg_file.capabilities.amo == 0 ) ? 0 : 1;
    if ( g_stage_address_translation(a, 1, is_implicit_write, 0, 1,
            iohgatp, cause, iotval2, &a, &gst_page_sz, &GR, &GW, &GX, &GD, &GPBMT,
            pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, TTYP) ) 
        return 1;

    // Count S/VS stage page walks
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, S_VS_PT_WALKS);

    status = read_memory((a + (vpn[i] * PTESIZE)), PTESIZE, (char *)&pte.raw);
    if ( status != 0 ) goto access_fault;

    // 3. If pte.v = 0, or if pte.r = 0 and pte.w = 1, or if any bits or 
    //    encodings that are reserved for future standard use are set within pte,
    //    stop and raise a page-fault exception to the original access type.
    if ( (pte.V == 0) || (pte.R == 0 && pte.W == 1) || 
         ((pte.N == 1) && (g_reg_file.capabilities.Svnapot == 0)) ||
         ((pte.PBMT != 0) && (g_reg_file.capabilities.Svpbmt == 0)) ||
         (pte.reserved != 0) )
        goto page_fault;

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
    if ( i != 0 && pte.N ) goto page_fault;


    // 4. Otherwise, the PTE is valid. If pte.r = 1 or pte.x = 1, go to step 5. 
    //    Otherwise, this PTE is a pointer to the next level of the page table. 
    //    Let i = i − 1. If i < 0, stop and raise a page-fault exception 
    //    corresponding to the original access type. Otherwise, let 
    //    a = pte.ppn × PAGESIZE and go to step 2.
    if ( pte.R == 1 || pte.X == 1 ) goto step_5;

    // The G bit designates a global mapping. Global mappings are those that exist 
    // in all address spaces.  For non-leaf PTEs, the global setting implies that 
    // all mappings in the subsequent levels of the page table are global.
    NL_G = NL_G & pte.G;

    // For non-leaf PTEs, bits 62–61 are reserved for future standard use. Until 
    // their use is defined by a standard extension, they must be cleared by 
    // software for forward compatibility, or else a page-fault exception is raised.
    if ( pte.PBMT != 0 ) goto page_fault;

    i = i - 1;
    if ( i < 0 ) goto page_fault;
    a = pte.PPN * PAGESIZE;
    goto step_2;

step_5:
    // 5. A leaf PTE has been found. Determine if the requested memory access 
    //    is allowed by the pte.r, pte.w, pte.x, and pte.u bits, given the current 
    //    privilege mode and the value of the SUM and MXR fields of the mstatus 
    //    register. If not, stop and raise a page-fault exception corresponding to 
    //    the original access type.
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
    if ( TTYP != PCIE_ATS_TRANSLATION_REQUEST ) {
        if ( is_exec  && (pte.X == 0) ) goto page_fault;
        if ( is_read  && (pte.R == 0) ) goto page_fault;
        if ( is_write && (pte.W == 0) ) goto page_fault;
    }
    if ( (priv == U_MODE) && (pte.U == 0) ) goto page_fault;

    // When ENS is 1, supervisor privilege transactions that read with 
    // execute intent to pages mapped with U bit in PTE set to 1 will fault, 
    // regardless of the state of SUM.
    if ( is_exec && (priv == S_MODE) && (pte.U == 1) ) goto page_fault;

    // When ENS is 1, the SUM (permit Supervisor User Memory access) bit modifies 
    // the privilege with which supervisor privilege transactions access virtual 
    // memory. When SUM is 0, supervisor privilege transactions to pages mapped 
    // with U-bit in PTE set to 1 will fault.
    if ( (priv == S_MODE) && !is_exec && SUM == 0 && pte.U == 1 ) goto page_fault;

    ppn[4] = ppn[3] = ppn[2] = ppn[1] = ppn[0] = 0;
    if ( iosatp.MODE == IOSATP_Sv32 ) {
        ppn[0] = get_bits(19, 10, pte.raw);
        ppn[1] = get_bits(31, 20, pte.raw);
    }
    if ( iosatp.MODE == IOSATP_Sv39 ) {
        ppn[0] = get_bits(18, 10, pte.raw);
        ppn[1] = get_bits(27, 19, pte.raw);
        ppn[2] = get_bits(53, 28, pte.raw);
    }
    if ( iosatp.MODE == IOSATP_Sv48 ) {
        ppn[0] = get_bits(18, 10, pte.raw);
        ppn[1] = get_bits(27, 19, pte.raw);
        ppn[2] = get_bits(36, 28, pte.raw);
        ppn[3] = get_bits(53, 37, pte.raw);
    }
    if ( iosatp.MODE == IOSATP_Sv57 ) {
        ppn[0] = get_bits(18, 10, pte.raw);
        ppn[1] = get_bits(27, 19, pte.raw);
        ppn[2] = get_bits(36, 28, pte.raw);
        ppn[3] = get_bits(45, 37, pte.raw);
        ppn[4] = get_bits(53, 46, pte.raw);
    }
    // 6. If i > 0 and pte.ppn[i − 1 : 0] = 0, this is a misaligned superpage; 
    // stop and raise a page-fault exception corresponding to the original 
    // access type.
    if ( i > 0 ) {
        switch ( (i - 1) ) {
            case 3: if ( ppn[3] ) goto page_fault;
            case 2: if ( ppn[2] ) goto page_fault;
            case 1: if ( ppn[1] ) goto page_fault;
            case 0: if ( ppn[0] ) goto page_fault;
        }
        // Determine page size
        if ( iosatp.MODE == IOSATP_Sv32 ) {
            *page_sz = 4 * 1024 * 1024;  // 4M;
        } else {
            *page_sz = PAGESIZE;
            switch (i) {
                case 4: *page_sz *= 512; // 256TiB
                case 3: *page_sz *= 512; // 512GiB
                case 2: *page_sz *= 512; //   1GiB
                case 1: *page_sz *= 512; //   2MiB
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
    if ( i == 0 && pte.N && ((pte.PPN & 0xF) != 0x8) ) goto page_fault;

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
    if ( (pte.A == 1) && ((pte.D == 1) | (is_write == 0)) ) goto step_8;

    // Count S/VS stage page walks
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, S_VS_PT_WALKS);

    // Set A/D bits if needed
    do_page_fault = 0;
    status = read_memory_for_AMO((a + (vpn[i] * PTESIZE)), PTESIZE, (char *)&pte.raw);
    if ( status != 0 ) goto access_fault;
    if ( TTYP != PCIE_ATS_TRANSLATION_REQUEST ) {
        // Determine if the reloaded PTE causes a fault
        // See note about PCIe ATS translation requests in step 5
        if ( is_exec  && (pte.X == 0) ) do_page_fault = 1;
        if ( is_read  && (pte.R == 0) ) do_page_fault = 1;
        if ( is_write && (pte.W == 0) ) do_page_fault = 1;
    }
    if ( (priv == U_MODE) && (pte.U == 0) ) do_page_fault = 1;
    if ( is_exec && (priv == S_MODE) && (pte.U == 1) ) do_page_fault = 1;
    if ( (priv == S_MODE) && !is_exec && SUM == 0 && pte.U == 1 ) do_page_fault = 1;
    // If no faults detected then set A and if required D bit
    if ( do_page_fault == 0 ) pte.A = 1;
    if ( do_page_fault == 0 && is_write ) pte.D = 1;
    status = write_memory((char *)&pte.raw, (a + (vpn[i] * PTESIZE)), PTESIZE);
    if ( status != 0 ) goto access_fault;
    if ( do_page_fault == 1) goto page_fault;

step_8:
    // 8. The translation is successful.

    // b. Implicit reads of NAPOT page table entries may create address-translation
    //    cache entries mapping a + va.vpn[j] × PTESIZE to a copy of pte in which 
    //    pte.ppn[pte.napot bits − 1 : 0] is replaced by vpn[0][pte.napot bits − 1 : 0], 
    //    for any or all j such that j[8 : napot bits] = i[8 : napot bits], all for 
    //    the address space identified in satp as loaded by step 0.
    if ( pte.N ) 
        pte.PPN = (pte.PPN & ~0xF) | ((iova / PAGESIZE) & 0xF);

    // The G bit designates a global mapping. Global mappings are those that exist 
    // in all address spaces.  For non-leaf PTEs, the global setting implies that 
    // all mappings in the subsequent levels of the page table are global.
    *G = NL_G & pte.G;

    // The page-based memory types (PBMT), if Svpbmt is supported, obtained 
    // from the IOMMU address translation page tables.
    *PBMT = pte.PBMT;

    // The translated physical address is given as follows:
    // pa.pgoff = va.pgoff.
    // If i > 0, then this is a superpage translation and pa.ppn[i − 1 : 0] = va.vpn[i − 1 : 0].
    // pa.ppn[LEVELS − 1 : i] = pte.ppn[LEVELS − 1 : i]
    *resp_pa = ((pte.PPN * PAGESIZE) & ~(*page_sz - 1)) | (iova & (*page_sz - 1));
    resp_gpa = *resp_pa;

    // Invoke G-stage page table to translate the PTE address if G-stage page
    // table is active.
    if ( g_stage_address_translation(*resp_pa, is_read, is_write, is_exec, 0,
            iohgatp, cause, iotval2, resp_pa, &gst_page_sz, &GR, &GW, &GX, &GD, &GPBMT,
            pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, TTYP) ) 
        return 1;

    // The page size is smaller of VS or G stage page table size
    if ( gst_page_sz < *page_sz ) {
        *page_sz = gst_page_sz;
    }

    // The page-based memory types (PBMT), if Svpbmt is supported, obtained 
    // from the IOMMU address translation page tables. When two-stage address
    // translation is performed the IOMMU provides the page-based memory type
    // as resolved between the G-stage and VS-stage page table
    // The G-stage page tables provide the PBMT as PMA, IO, or NC
    // If VS-stage page tables have PBMT as PMA, then G-stage PBMT is used
    // else VS-stage PBMT overrides.
    // The IO bridge resolves the final memory type
    *PBMT = ( *PBMT != PMA ) ? *PBMT : GPBMT;

    // Updated resp PA if needed based on the resolved S/VS and G-stage page size
    *resp_pa = (*resp_pa & ~(*page_sz - 1)) | (iova & (*page_sz - 1));
    *R = pte.R & GR;
    *W = pte.W & GW;
    *X = pte.X & GX;

    // Cache the translation in the IOATC
    // In the IOTLB the IOVA & PPN is stored in the NAPOT format
    napot_ppn = (((*resp_pa & ~(*page_sz - 1)) | ((*page_sz/2) - 1))/PAGESIZE);
    napot_iova = (((iova & ~(*page_sz - 1)) | ((*page_sz/2) - 1))/PAGESIZE);
    napot_gpa = (((resp_gpa & ~(*page_sz - 1)) | ((*page_sz/2) - 1))/PAGESIZE);
    if ( TTYP != PCIE_ATS_TRANSLATION_REQUEST ) {
        // ATS translation requests are cached in Device TLB. For Untranslated
        // Requests cache the translations for future re-use
        cache_ioatc_iotlb(napot_iova, 
                    ((iohgatp.MODE == IOHGATP_Bare) ? 0 : 1),             // GV
                    ((iosatp.MODE == IOSATP_Bare) ? 0 : 1),               // PSCV
                    iohgatp.GSCID, PSCID,                                 // GSCID, PSCID tags
                    pte.R, pte.W, pte.X, pte.U, *G, pte.D,                // VS stage attributes
                    *PBMT, GR, GW, GX, GD,                                // G stage attributes
                    napot_ppn,                                            // PPN
                    ((*page_sz > PAGESIZE) ? 1 : 0)                       // S - size
                   );
    } 
    if ( TTYP == PCIE_ATS_TRANSLATION_REQUEST && T2GPA == 1 ) {
        // If in T2GPA mode, cache the final GPA->SPA translation as 
        // the translated requests may hit on this 
        cache_ioatc_iotlb(napot_gpa, 
                    ((iohgatp.MODE == IOHGATP_Bare) ? 0 : 1),             // GV
                    0,                                                    // PSCV
                    iohgatp.GSCID, 0,                                     // GSCID, PSCID tags
                    pte.R, pte.W, pte.X, pte.U, *G, pte.D,                // VS stage attributes
                    *PBMT, GR, GW, GX, GD,                                // G stage attributes
                    napot_ppn,                                            // PPN
                    ((*page_sz > PAGESIZE) ? 1 : 0)                       // S - size
                   );
        // Return the GPA as translation response if T2GPA is 1
        *resp_pa = resp_gpa;
    }
    return 0;

page_fault:
    // Stop and raise a page-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) *cause = 12;      // Instruction page fault
    else if ( is_read ) *cause = 13; // Read page fault
    else *cause = 15;                // Write/AMO page fault
    return 1;

access_fault:    
    // Stop and raise a access-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) *cause = 1;       // Instruction access fault
    else if ( is_read ) *cause = 5;  // Read access fault
    else *cause = 7;                 // Write/AMO access fault
    return 1;
}
