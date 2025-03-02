// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"
uint64_t
extract(uint64_t data, uint64_t mask) {
    uint32_t i, j = 0;
    uint64_t I = 0;
    for ( i = 0; i < 64; i++ ) {
        if ( mask & (1UL << i) ) {
            I |= (((data >> i) & 0x01) << j);
            j++;
        }
    }
    return I;
}
uint8_t
msi_address_translation(
    uint64_t gpa, uint8_t is_exec, device_context_t *DC,
    uint8_t *is_msi, uint8_t *is_mrif, uint32_t *mrif_nid, uint64_t *dest_mrif_addr,
    uint32_t *cause, uint64_t *iotval2, uint64_t *pa,
    uint64_t *page_sz, gpte_t *g_pte, uint8_t check_access_perms,
    uint32_t rcid, uint32_t mcid) {

    uint64_t A, m, I;
    uint8_t status;
    msipte_t msipte;

    *iotval2 = 0;
    *is_msi = 0;

    if ( DC->msiptp.MODE == MSIPTP_Off )
        return 0;

    // 1. Let `A` be the GPA
    A = gpa;

    // 2. Let `DC` be the device-context located using the `device_id` of the device
    //    using the process outlined in <<GET_DC>>.
    // 3. Determine if the address `A` is an access to a virtual interrupt file as
    //    specified in <<MSI_ID>>
    *is_msi = (((A >> 12) & ~DC->msi_addr_mask.mask) ==
               ((DC->msi_addr_pattern.pattern & ~DC->msi_addr_mask.mask)));

    // 4. If the address is not determined to be that of a virtual interrupt file then
    //    stop this process and instead use the regular translation data structures to
    //    do the address translation.
    if ( *is_msi == 0 )
        return 0;

    // 5. Extract an interrupt file number `I` from `A` as
    //    `I = extract(A >> 12, DC.msi_addr_mask)`. The bit extract function `extract(x, y)`
    //    discards all bits from `x` whose matching bits in the same positions in the
    //    mask `y` are zeros, and packs the remaining bits from `x` contiguously at the
    //    least-significant end of the result, keeping the same bit order as `x` and
    //    filling any other bits at the most-significant end of the result with zeros.
    //    For example, if the bits of `x` and `y` are
    //    ** `x = a b c d e f g h`
    //    ** `y = 1 0 1 0 0 1 1 0`
    //    ** then the value of `extract(x, y)` has bits `0 0 0 0 a c f g`.
    I = extract((A >> 12), DC->msi_addr_mask.mask);

    // 6. Let `m` be `(DC.msiptp.PPN x 2^12^)`.
    m = DC->msiptp.PPN * PAGESIZE;

    // 7. Let `msipte` be the value of sixteen bytes at address `(m | (I x 16))`. If
    //    accessing `msipte` violates a PMA or PMP check, then stop and report
    //    "MSI PTE load access fault" (cause = 261).
    status = read_memory((m + (I * 16)), 16, (char *)&msipte.raw, rcid, mcid);
    if ( status & ACCESS_FAULT ) {
        *cause = 261;     // MSI PTE load access fault
        return 1;
    }

    // 8. If `msipte` access detects a data corruption (a.k.a. poisoned data), then
    //    stop and report "MSI PT data corruption" (cause = 270). This fault is reported
    //    if the IOMMU supports RAS (i.e., capabilities.RAS == 1)
    if ( (status & DATA_CORRUPTION) ) {
        *cause = 270;     // MSI PTE load access fault
        return 1;
    }

    // 9. If `msipte.V == 0`, then stop and report "MSI PTE not valid" (cause = 262).
    if ( msipte.V == 0 ) {
        *cause = 262;     // MSI PTE not valid
        return 1;
    }

    //10. If `msipte.C == 1`, then further process is to interpret the PTE is
    //    implementation defined. No custom implementation in reference model
    //    so this case causes "MSI PTE misconfigured" (cause = 263).
    if ( msipte.C == 1 ) {
        *cause = 263;
        return 1;
    }

    //11. If `msipte.C == 0` then the process is outlined in subsequent steps.
    //12. If `msipte.M == 0` or `msipte.M == 2`,then stop and report "MSI PTE
    //    misconfigured" (cause = 263).
    if ( msipte.M == 0 || msipte.M == 2 ) {
        *cause = 263;
        return 1;
    }

    //13. If `msipte.M == 3` the PTE is translate R/W mode PTE and the translation
    //    process is as follows:
    //    a. If any bits or encoding that are reserved for future standard use are
    //       set within msipte, stop and report "MSI PTE misconfigured" (cause = 263).
    //    b. Compute the translated address as `msipte.PPN << 12 | A[11:0]`.
    if ( msipte.M == 3 ) {
        if ( msipte.translate_rw.reserved != 0 || msipte.translate_rw.reserved0 != 0 ) {
            *cause = 263;
            return 1;
        }
        *pa = ((msipte.translate_rw.PPN * PAGESIZE) | (A & 0xFFF));
        g_pte->raw = 0;
        g_pte->PPN = gpa / PAGESIZE;
        g_pte->D = g_pte->A = g_pte->U = 1;
        g_pte->X = 0;
        g_pte->W = g_pte->R = g_pte->V = 1;
        g_pte->N = g_pte->G = 0;
        g_pte->PBMT = PMA;
        *page_sz = PAGESIZE;
        *is_mrif = 0;
        goto step_15;
    }

    //14. If `msipte.M == 1` the PTE is in MRIF mode and the translation process
    //    is as follows:
    //    a. If `capabilities.MSI_MRIF == 0`, stop and report "MSI PTE misconfigured"
    //       (cause = 263).
    if ( g_reg_file.capabilities.msi_mrif == 0 ) {
        *cause = 263;
        return 1;
    }

    //    b. If any bits or encoding that are reserved for future standard use are
    //       set within `msipte`, stop and report "MSI PTE misconfigured" (cause = 263).
    if ( msipte.mrif.reserved1 != 0 || msipte.mrif.reserved2 != 0 ||
         msipte.mrif.reserved3 != 0 || msipte.mrif.reserved4 != 0 ) {
        *cause = 263;
        return 1;
    }

    //    d. The address of the destination MRIF is `msipte.MRIF_Address[55:9] * 512`.
    *dest_mrif_addr = msipte.mrif.MRIF_ADDR_55_9 << 9;

    //    e. The destination address of the notice MSI is `msipte.NPPN << 12`.
    *pa    = (msipte.mrif.NPPN * PAGESIZE);

    //    f. Let `NID` be `(msipte.N10 << 10) | msipte.N[9:0]`. The data value for
    //       notice MSI is the 11-bit `NID` value zero-extended to 32-bits.
    *mrif_nid   = (msipte.mrif.N10 << 10) | msipte.mrif.N90;

    *is_mrif = 1;
    g_pte->raw = 0;
    g_pte->PPN = gpa / PAGESIZE;
    g_pte->D = g_pte->A = g_pte->U = 1;
    g_pte->X = 0;
    g_pte->W = g_pte->R = g_pte->V = 1;
    g_pte->N = g_pte->G = 0;
    g_pte->PBMT = PMA;
    *page_sz = PAGESIZE;

step_15:
    //15. The access permissions associated with the translation determined through
    //    this process are equivalent to that of a regular RISC-V second-stage PTE with
    //    R=W=U=1 and X=0. Similar to a second-stage PTE, when checking the U bit, the
    //    transaction is treated as not requesting supervisor privilege.
    //    a. If the transaction is a Untranslated or Translated read-for-execute then stop
    //       and report "Instruction acccess fault" (cause = 1).
    if ( is_exec == 1 && check_access_perms == 1 ) {
        *cause = 1;
        return 1;
    }
    return 0;
}
