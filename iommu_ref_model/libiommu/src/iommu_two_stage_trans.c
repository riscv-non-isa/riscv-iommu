// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

uint8_t
two_stage_address_translation(
    uint64_t iova, uint8_t check_access_perms, uint32_t DID, uint8_t is_read,
    uint8_t is_write, uint8_t is_exec,
    uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    iosatp_t iosatp, uint8_t priv, uint8_t SUM, uint8_t SADE,
    uint8_t GV, uint32_t GSCID, iohgatp_t iohgatp, uint8_t GADE, uint8_t SXL,
    uint32_t *cause, uint64_t *iotval2, uint64_t *pa,
    uint64_t *page_sz, pte_t *pte, uint32_t rcid, uint32_t mcid) {

    uint16_t vpn[5];
    uint16_t ppn[5];
    pte_t amo_pte;
    gpte_t gpte;
    uint8_t NL_G = 0;
    uint8_t is_implicit;
    uint8_t PTESIZE, LEVELS, status, pte_changed, gst_fault;
    int8_t i;
    uint64_t a, masked_upper_bits, mask;
    uint64_t gst_page_sz;
    uint64_t pa_mask = ((1UL << (g_reg_file.capabilities.pas)) - 1);

    *iotval2 = 0;

    // Walk page tables
    if ( iosatp.MODE == IOSATP_Bare ) {
        // No translation or protection.
        i = 0;
        pte->raw = 0;
        pte->PPN = 0;
        pte->D = pte->A = pte->G = pte->U = 1;
        pte->X = pte->W = pte->R = pte->V = 1;
        pte->N = 0;
        pte->PBMT = PMA;
        *pa = iova;
        // The translation range size returned in a Success response to
        // an ATS translation request, when either stages of address
        // translation are Bare, is implementation-defined. However, it
        // is recommended that the translation range size be large, such
        // as 2 MiB or 1 GiB.
        if ( g_reg_file.capabilities.Sv57 == 1 )
            *page_sz = g_sv57_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv48 == 1 )
            *page_sz = g_sv48_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv39 == 1 && SXL == 0)
            *page_sz = g_sv39_bare_pg_sz;
        else if ( g_reg_file.capabilities.Sv32 == 1 && SXL == 1)
            *page_sz = g_sv32_bare_pg_sz;
        return 0;
    }

    // 1. Let a be satp.ppn × PAGESIZE, and let i = LEVELS − 1. PAGESIZE is 2^12. (For Sv32,
    //    LEVELS=2, For Sv39 LEVELS=3, For Sv48 LEVELS=4, For Sv57 LEVELS=5.) The satp register
    //    must be active, i.e., the effective privilege mode must be S-mode or U-mode.
    if ( iosatp.MODE == IOSATP_Sv32 && SXL == 1 ) {
        vpn[0] = get_bits(21, 12, iova);
        vpn[1] = get_bits(31, 22, iova);
        LEVELS = 2;
        PTESIZE = 4;
        // When `SXL` is 1, the following rules apply:
        // * If the S/VS-stage page table is not `Bare` then a page fault corresponding to
        //   the original access type occurs if the `IOVA` has bits set beyond bit 31.
        mask = (1UL << (64 - 32)) - 1;
        masked_upper_bits = (iova >> 32) & mask;
    }
    if ( iosatp.MODE == IOSATP_Sv39 && SXL == 0 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        LEVELS = 3;
        PTESIZE = 8;
        mask = (1UL << (64 - 38)) - 1;
        masked_upper_bits = (iova >> 38) & mask;
    }
    if ( iosatp.MODE == IOSATP_Sv48 && SXL == 0 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        vpn[3] = get_bits(47, 39, iova);
        LEVELS = 4;
        PTESIZE = 8;
        mask = (1UL << (64 - 47)) - 1;
        masked_upper_bits = (iova >> 47) & mask;
    }
    if ( iosatp.MODE == IOSATP_Sv57 && SXL == 0 ) {
        vpn[0] = get_bits(20, 12, iova);
        vpn[1] = get_bits(29, 21, iova);
        vpn[2] = get_bits(38, 30, iova);
        vpn[3] = get_bits(47, 39, iova);
        vpn[4] = get_bits(56, 48, iova);
        LEVELS = 5;
        PTESIZE = 8;
        mask = (1UL << (64 - 56)) - 1;
        masked_upper_bits = (iova >> 56) & mask;
    }
    // Instruction fetch addresses and load and store effective addresses,
    // which are 64 bits, must have bits 63:<VASIZE> all equal to bit
    // (VASIZE-1), or else a page-fault exception will occur - for SXL=0
    // Do the address is canonical check - for SXL=0
    // For SXL = 1 check bits 63:32 are all 0
    if ( (masked_upper_bits != 0 && masked_upper_bits != mask && SXL == 0) ||
         (masked_upper_bits != 0 && SXL == 1) ) goto page_fault;

    i = LEVELS - 1;
    a = iosatp.PPN * PAGESIZE;
step_2:
    // 2. Let pte be the value of the PTE at address a+va.vpn[i]×PTESIZE. (For
    //    Sv32 PTESIZE=4. and for all other modes PTESIZE=8). If accessing pte
    //    violates a PMA or PMP check, raise an access-fault exception
    //    corresponding to the original access type.
    pte->raw = 0;
    a = a + vpn[i] * PTESIZE;

    // Invoke G-stage page table to translate the PTE address if G-stage page
    // table is active.
    // If IOMMU HW A/D bit update are enabled the implicit accesses are treated
    // as writes. This avoids the IOMMU needing to go back in time to set D bit
    // in G-stage page tables if A or D bit needs to be set in VS stage.
    is_implicit = 1;
    if ( ( gst_fault = second_stage_address_translation(a, 1, DID, 1, 0, 0, is_implicit,
                            PV, PID, PSCV, PSCID, GV, GSCID, iohgatp, GADE, SADE, SXL,
                            &a, &gst_page_sz, &gpte, rcid, mcid) ) ) {
        if ( gst_fault == GST_PAGE_FAULT ) goto guest_page_fault;
        if ( gst_fault == GST_ACCESS_FAULT ) goto access_fault;
        goto data_corruption;
    }

    //    If the address is beyond the maximum physical address width of the machine
    //    then an access fault occurs
    if ( a & ~pa_mask ) goto access_fault;

    // Count S/VS stage page walks
    count_events(PV, PID, PSCV, PSCID, DID, GV, GSCID, S_VS_PT_WALKS);
    pte->raw = 0;
    status = read_memory(a, PTESIZE, (char *)&pte->raw, rcid, mcid);
    if ( status & ACCESS_FAULT ) goto access_fault;
    if ( status & DATA_CORRUPTION) goto data_corruption;

    // 3. If pte.v = 0, or if pte.r = 0 and pte.w = 1, or if any bits or
    //    encodings that are reserved for future standard use are set within pte,
    //    stop and raise a page-fault exception to the original access type.
    if ( (pte->V == 0) || (pte->R == 0 && pte->W == 1) ||
         ((pte->PBMT != 0) && (g_reg_file.capabilities.Svpbmt == 0)) ||
         (pte->PBMT == 3) ||
         (pte->reserved != 0) )
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
    if ( i != 0 && pte->N ) goto page_fault;

    // 4. Otherwise, the PTE is valid. If pte.r = 1 or pte.x = 1, go to step 5.
    //    Otherwise, this PTE is a pointer to the next level of the page table.
    //    Let i = i − 1. If i < 0, stop and raise a page-fault exception
    //    corresponding to the original access type. Otherwise, let
    //    a = pte.ppn × PAGESIZE and go to step 2.
    if ( pte->R == 1 || pte->X == 1 ) goto step_5;

    // The G bit designates a global mapping. Global mappings are those that exist
    // in all address spaces.  For non-leaf PTEs, the global setting implies that
    // all mappings in the subsequent levels of the page table are global.
    NL_G = NL_G | pte->G;

    // For non-leaf PTEs, bits 62–61 are reserved for future standard use. Until
    // their use is defined by a standard extension, they must be cleared by
    // software for forward compatibility, or else a page-fault exception is raised.
    if ( pte->PBMT != 0 ) goto page_fault;

    // For non-leaf PTEs, the D, A, and U bits are reserved for future standard use.
    if ( pte->D != 0 || pte->A != 0 || pte->U != 0) goto page_fault;

    i = i - 1;
    if ( i < 0 ) goto page_fault;
    a = pte->PPN * PAGESIZE;
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
    if ( check_access_perms == 1 ) {
        if ( is_exec  && (pte->X == 0) ) goto page_fault;
        if ( is_read  && (pte->R == 0) ) goto page_fault;
        if ( is_write && (pte->W == 0) ) goto page_fault;
    }
    if ( (priv == U_MODE) && (pte->U == 0) ) goto page_fault;

    // When ENS is 1, supervisor privilege transactions that read with
    // execute intent to pages mapped with U bit in PTE set to 1 will fault,
    // regardless of the state of SUM.
    if ( is_exec && (priv == S_MODE) && (pte->U == 1) ) goto page_fault;

    // When ENS is 1, the SUM (permit Supervisor User Memory access) bit modifies
    // the privilege with which supervisor privilege transactions access virtual
    // memory. When SUM is 0, supervisor privilege transactions to pages mapped
    // with U-bit in PTE set to 1 will fault.
    if ( (priv == S_MODE) && !is_exec && SUM == 0 && pte->U == 1 ) goto page_fault;

    ppn[4] = ppn[3] = ppn[2] = ppn[1] = ppn[0] = 0;
    if ( iosatp.MODE == IOSATP_Sv32 && SXL == 1) {
        ppn[0] = get_bits(19, 10, pte->raw);
        ppn[1] = get_bits(31, 20, pte->raw);
    }
    if ( iosatp.MODE == IOSATP_Sv39 && SXL == 0 ) {
        ppn[0] = get_bits(18, 10, pte->raw);
        ppn[1] = get_bits(27, 19, pte->raw);
        ppn[2] = get_bits(53, 28, pte->raw);
    }
    if ( iosatp.MODE == IOSATP_Sv48 && SXL == 0 ) {
        ppn[0] = get_bits(18, 10, pte->raw);
        ppn[1] = get_bits(27, 19, pte->raw);
        ppn[2] = get_bits(36, 28, pte->raw);
        ppn[3] = get_bits(53, 37, pte->raw);
    }
    if ( iosatp.MODE == IOSATP_Sv57 && SXL == 0 ) {
        ppn[0] = get_bits(18, 10, pte->raw);
        ppn[1] = get_bits(27, 19, pte->raw);
        ppn[2] = get_bits(36, 28, pte->raw);
        ppn[3] = get_bits(45, 37, pte->raw);
        ppn[4] = get_bits(53, 46, pte->raw);
    }
    // 6. If i > 0 and pte.ppn[i − 1 : 0] = 0, this is a misaligned superpage;
    // stop and raise a page-fault exception corresponding to the original
    // access type.
    *page_sz = PAGESIZE;
    if ( i > 0 ) {
        switch ( i ) {
            case 4: if ( ppn[3] ) goto page_fault;
                    *page_sz *= 512UL; // 256TiB
            case 3: if ( ppn[2] ) goto page_fault;
                    *page_sz *= 512UL; // 512GiB
            case 2: if ( ppn[1] ) goto page_fault;
                    *page_sz *= 512UL; // 1GiB
            case 1: if ( ppn[0] ) goto page_fault;
                    *page_sz *= 512UL; // 2MiB
                    if ( iosatp.MODE == IOSATP_Sv32 && SXL == 1 ) {
                        *page_sz *= 2UL; // 4MiB
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
    if ( i == 0 && pte->N && ((pte->PPN & 0xF) != 0x8) ) goto page_fault;

    // 7. If pte.a = 0, or if the original memory access is a store and pte.d = 0,
    //    If SADE is 1, the IOMMU updates A and D bits in first-stage PTEs atomically. If
    //    SADE is 0, the IOMMU causes a page-fault corresponding to the original access type.
    //    To set A or D bit in first-stage PTE, the G-stage PTE that provides
    //    its translation must have write permission else a guest page fault occurs.
    //    if the A bit is 0 or if the memory access is a store and the D bit is 0.
    //    - If a store to pte would violate a PMA or PMP check, raise an access-fault exception
    //      corresponding to the original access type.
    //    Perform the following steps atomically:
    //    – Compare pte to the value of the PTE at address a + va.vpn[i] × PTESIZE.
    //    – If the values match, set pte.a to 1 and, if the original memory access is a store,
    //      also set pte.d to 1.
    //    – If the comparison fails, return to step 2
    if ( (pte->A == 1) && ( (pte->D == 1) || (is_write == 0) || (pte->W == 0) ) ) goto step_8;

    // A and/or D bit update needed
    if ( SADE == 0 ) goto page_fault;

    // If G-stage does not provide write permission then cause guest page fault
    if ( gpte.W == 0 ) goto guest_page_fault;

    // Count S/VS stage page walks
    count_events(PV, PID, PSCV, PSCID, DID, GV, GSCID, S_VS_PT_WALKS);
    amo_pte.raw = 0;
    status = read_memory_for_AMO(a, PTESIZE, (char *)&amo_pte.raw, rcid, mcid);

    if ( status & ACCESS_FAULT ) goto access_fault;
    if ( status & DATA_CORRUPTION) goto data_corruption;

    pte_changed = (amo_pte.raw == pte->raw) ? 0 : 1;

    if ( pte_changed == 0 ) {
        amo_pte.A = 1;
        // The case for is_write == 1 && pte.W == 0 is to address ATS translation
        // requests that may request write permission when write permission does not
        // exist. If write permission exists then the D bit is set else D bit is not
        // set and the write permission is returned in responses as 0.
        if ( (is_write == 1) && (amo_pte.W == 1) ) amo_pte.D = 1;
    }

    status = write_memory((char *)&amo_pte.raw, a, PTESIZE, rcid, mcid);

    if ( status & ACCESS_FAULT ) goto access_fault;
    if ( status & DATA_CORRUPTION) goto data_corruption;

    if ( pte_changed == 1) goto step_2;

step_8:
    // 8. The translation is successful.

    // b. Implicit reads of NAPOT page table entries may create address-translation
    //    cache entries mapping a + va.vpn[j] × PTESIZE to a copy of pte in which
    //    pte.ppn[pte.napot bits − 1 : 0] is replaced by vpn[0][pte.napot bits − 1 : 0],
    //    for any or all j such that j[8 : napot bits] = i[8 : napot bits], all for
    //    the address space identified in satp as loaded by step 0.
    if ( pte->N )
        pte->PPN = (pte->PPN & ~0xF) | ((iova / PAGESIZE) & 0xF);

    // The G bit designates a global mapping. Global mappings are those that exist
    // in all address spaces.  For non-leaf PTEs, the global setting implies that
    // all mappings in the subsequent levels of the page table are global.
    pte->G = NL_G | pte->G;

    // The translated physical address is given as follows:
    // pa.pgoff = va.pgoff.
    // If i > 0, then this is a superpage translation and pa.ppn[i − 1 : 0] = va.vpn[i − 1 : 0].
    // pa.ppn[LEVELS − 1 : i] = pte.ppn[LEVELS − 1 : i]
    *pa = ((pte->PPN * PAGESIZE) & ~(*page_sz - 1)) | (iova & (*page_sz - 1));
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

data_corruption:
    *cause = 274;                    // First/Second-stage PT data corruption
    return 1;

guest_page_fault:
    // Stop and raise a page-fault exception corresponding
    // to the original access type.
    if ( is_exec ) *cause = 20;      // Instruction guest page fault
    else if ( is_read ) *cause = 21; // Read guest page fault
    else *cause = 23;                // Write/AMO guest page fault
    // If the CAUSE is a guest-page fault then bits 63:2 of the zero-extended
    // guest-physical-address are reported in iotval2[63:2]. If bit 0 of iotval2
    // is 1, then guest-page-fault was caused by an implicit memory access for
    // VS-stage address translation. If bit 0 of iotval2 is 1, and the implicit
    // access was a write then bit 1 of iotval2 is set to 1 else it is set to 0.
    *iotval2 = (a & ~0x3);
    *iotval2 |= 1;
    *iotval2 |= (SADE << 1);
    return 1;
}
