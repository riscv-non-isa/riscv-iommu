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
    uint64_t iova, uint32_t msi_write_data, addr_type_t at, device_context_t *DC,
    uint32_t *cause, uint64_t *resp_pa, uint8_t *R, uint8_t *W, uint8_t *U, 
    uint8_t *is_msi, uint8_t *is_unsup, uint8_t *is_mrif_wr, uint32_t *mrif_nid,
    uint8_t pid_valid, uint32_t process_id, uint8_t PSCV, uint32_t PSCID, uint32_t device_id,
    uint8_t GV, uint32_t GSCID) {

    uint64_t A, m, I, mrif_dw_addr, page_sz;
    uint32_t mrif_dw;
    uint8_t status, ioatc_status;
    msipte_t msipte;
    uint32_t D;
    uint8_t X, G, PBMT;

    *is_msi = *is_mrif_wr = *is_unsup = *R = *W = *U = 0;

    // 1. Let `A` be the 32-bit aligned `IOVA`
    A = iova;

    // 2. Let `DC` be the device-context located using the `device_id` of the device
    //    using the process outlined in <<GET_DC>>.
    // 3. Determine if the address `A` is an MSI address as specified in <<MSI_ID>>.
    *is_msi = (((A >> 12) & ~DC->msi_addr_mask.mask) == 
               ((DC->msi_addr_pattern.pattern & ~DC->msi_addr_mask.mask)));

    // 4. If the address is not determined to be an MSI then stop this process and
    //    instead use the regular translation data structures to do the address
    //    translation.
    if ( *is_msi == 0 ) 
        return 0;

    // 5. Extract an interrupt file number `I` from `A` as
    //    `I = extract(A >> 12, DC.msi_addr_mask)`. The extract function here is similar
    //    to generic bit extract performed by RISC-V instruction `BEXT`, defined by the
    //    Bitmanip extension (B extension). The bit extract function `extract(x, y)`
    //    discards all bits from `x` whose matching bits in the same positions in the
    //    mask `y` are zeros, and packs the remaining bits from `x` contiguously at the
    //    least-significant end of the result, keeping the same bit order as `x` and
    //    filling any other bits at the most-significant end of the result with zeros.
    //    For example, if the bits of `x` and `y` are
    //    ** `x = a b c d e f g h`
    //    ** `y = 1 0 1 0 0 1 1 0`
    //    ** then the value of `extract(x, y)` has bits `0 0 0 0 a c f g`.
    I = extract((A >> 12), DC->msi_addr_mask.mask);

    // 6. If bit 2 of `A` is 1, i.e. the MSI is in big-endian byte order. The IOMMU
    //    capable of big-endian access to memory if the `END` bit in the `capabilities`
    //    register (<<CAP>>) is 1. When the IOMMU is capable of big-endian operation,
    //    the feature control register, `fctrl` (<<FCTRL>>), holds the configuration
    //    bit that may be set to 1 to enable big-endian access to memory. If the IOMMU
    //    is not capable or has not been configured for big-endian access to memory,
    //    then stop this process and treat the transaction as an unsupported request.
    if ( (A & (1UL << 2) ) && (g_reg_file.fctrl.end == 0) ) {
        *is_unsup = 1;
        return 0;
    }

    // Lookup IOATC to determine if there is a cached translation
    // Since only G stage permissions may be active the S/VS stage permissions must 
    // never fault. G-stage faults cause invalidate and return miss. So we cannot 
    // get a ATC fault on this lookup.
    if ( (ioatc_status = lookup_ioatc_iotlb(iova, U_MODE, 1, 1, 0, 0, 0, 0, GV, GSCID, cause,
                                            resp_pa, &page_sz, R, W, &X, &G, &PBMT)) == IOATC_HIT )
        return 0;

    // Miss in IOATC
    // Count misses in TLB
    count_events(pid_valid, process_id, PSCV, PSCID, device_id, GV, GSCID, IOATC_TLB_MISS);

    // 7. Let `m` be `(DC.msiptp.PPN x 2^12^)`.
    m = DC->msiptp.PPN * PAGESIZE;

    // 8. Let `msipte` be the value of sixteen bytes at address `(m | (I x 16))`. If
    //    accessing `msipte` violates a PMA or PMP check, then stop and report
    //    "MSI PTE load access fault" (cause = 261).
    status = read_memory((m + (I * 16)), 16, (char *)&msipte.raw);
    if ( status & ACCESS_FAULT ) {
        *cause = 261;     // MSI PTE load access fault
        return 1;
    }

    // 9. If `msipte` access detects a data corruption (a.k.a. poisoned data), then
    //    stop and report "MSI PT data corruption" (cause = 270). This fault is reported
    //    if the IOMMU supports RAS (i.e., capabilities.RAS == 1)
    if ( (status & DATA_CORRUPTION) ) {
        *cause = 270;     // MSI PTE load access fault
        return 1;
    }

    //10. If `msipte.V == 0`, then stop and report "MSI PTE not valid" (cause = 262).
    if ( msipte.V == 0 ) {
        *cause = 262;     // MSI PTE not valid
        return 1;
    }

    //11. If `msipte.C == 1`, then further process is to interpret the PTE is
    //    implementation defined. No custom implementation in reference model
    //    so this case causes "MSI PTE misconfigured" (cause = 263).
    if ( msipte.C == 1 ) {
        *cause = 263;
        return 1;
    }

    //12. If `msipte.C == 0` then the process is outlined in subsequent steps.
    //13. If `msipte.W == 1` the PTE is write-through mode PTE and the translation
    //    process is as follows:
    //    a. If any bits or encoding that are reserved for future standard use are 
    //       set within msipte, stop and report "MSI PTE misconfigured" (cause = 263).
    //    b. Compute the translated address as `msipte.PPN << 12 | A[11:0]`.
    if ( msipte.W == 1 ) {
        if ( msipte.write_through.reserved0 != 0 || 
             msipte.write_through.reserved1 != 0 ) {
            *cause = 263;
            return 1;
        }
        *resp_pa = ((msipte.write_through.PPN * PAGESIZE) | (A & 0xFFF));
        *R = 1;
        *W = 1;
        *U = 0;
        // Cache the translation in the IOATC
        // In the IOTLB the IOVA & PPN is stored in the NAPOT format
        // Here the translations are always PAGESIZE translations
        cache_ioatc_iotlb((iova/PAGESIZE),       // IOVA
                    GV,                          // GV
                    0,                           // PSCV
                    GSCID, 0,                    // GSCID, PSCID tags
                    1, 1, 1, 1, 1, 1, PMA,       // VS stage attributes - R=W=X=U=G=D1, PBMT=PMA
                    1, 1, 0, 1,                  // G stage attributes - G$=GW=1, GX=0, GD=1
                    msipte.write_through.PPN,    // PPN
                    0                            // S - size
                   );
                   return 0;
    }

    //14. If `msipte.W == 0` the PTE is in MRIF mode and the translation process
    //    is as follows:
    //    a. If `capabilities.MSI_MRIF == 0`, stop and report "MSI PTE misconfigured"
    //       (cause = 263).
    if ( g_reg_file.capabilities.msi_mrif == 0 ) {
        *cause = 263;
        return 1;
    }

    //    c. If any bits or encoding that are reserved for future standard use are
    //       set within `msipte`, stop and report "MSI PTE misconfigured" (cause = 262).
    if ( msipte.mrif.reserved0 != 0 || msipte.mrif.reserved1 != 0 ||
         msipte.mrif.reserved2 != 0 || msipte.mrif.reserved3 != 0 ||
         msipte.mrif.reserved4 != 0 ) {
        *cause = 263;
        return 1;
    }

    //    b. If the transaction is a PCIe ATS translation request then return a Success
    //       response with R, W, and U bit set to 1. See <<ATS_FAULTS>> for further
    //       details on this processing.
    // When a MSI PTE is configured in MRIF mode, a MSI write with data value `D`
    // requires the IOMMU to set the interrupt-pending bit for interrupt identity `D`
    // in the MRIF. A translation request from a device to a GPA that is mapped
    // through a MRIF mode MSI PTE is not eligible to receive a translated address.
    // This is accomplished by setting "Untranslated Access Only" (U) field of the
    // returned response to 1.
    if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
        *R = 1;
        *W = 1;
        *U = 1;
        return 0;
    }

    //    d. Let `D` be the 32-bit data associated with the write. The byte order of
    //       `D` is determined by bit 2 of `A`.
    D = msi_write_data;
    //    e. If `A[11:3]` or `D[31:11]` is not zero, then stop this process and request
    //       the IO bridge to discard the write as an unsupported request.
    if ( ((A & 0xFFC) != 0) || ((D & ~0xFFF) != 0) ) {
        *is_unsup = 1;
        return 0;
    }
    //    f. If the IOMMU supports atomic memory operations
    //       (`capabilities.AMO` is 1, <<CAP>>), then, in the destination MRIF
    //       (at address `msipte.MRIF_ADDR * 512`), set the interrupt-pending bit
    //       for interrupt identity `D` to 1 using an `AMOOR` operation for atomic update.
    if ( g_reg_file.capabilities.amo == 1 ) {
        mrif_dw_addr = (msipte.mrif.MRIF_ADDR * 512) + (D >> 5);
        status = read_memory_for_AMO((msipte.mrif.MRIF_ADDR * 512), 4, (char *)&mrif_dw);
        if ( status == 0 ) {
            mrif_dw |= (1UL << (D & 0x1F));
            status = write_memory((char *)&mrif_dw, mrif_dw_addr, 4);
        }
    }
    //    g. If the IOMMU does not support atomic memory operations then, in the
    //       destination MRIF (at address `msipte.MRIF_ADDR * 512`), set the
    //       interrupt-pending bit for interrupt identity `D` to 1 using a non-atomic
    //       read-modify-write sequence.
    if ( g_reg_file.capabilities.amo == 1 ) {
        mrif_dw_addr = (msipte.mrif.MRIF_ADDR * 512) + (D >> 5);
        status = read_memory((msipte.mrif.MRIF_ADDR * 512), 4, (char *)&mrif_dw);
        if ( status == 0 ) {
            mrif_dw |= (1UL << (D & 0x1F));
            status = write_memory((char *)&mrif_dw, mrif_dw_addr, 4);
        }
    }
    //    h. If accessing MRIF violates a PMA or PMP check, then stop and report
    //       "MRIF access fault" (cause = 264).
    if ( status & ACCESS_FAULT ) {
        *cause = 264;     // MRIF access fault
        return 1;
    }
    //    i. If the MRIF access detects a data corruption (a.k.a poisoned data), then
    //       stop and report "MSI MRIF data corruption" (cause = 271).
    if ( (status & DATA_CORRUPTION) ) {
        *cause = 271;     // MSI MRIF data corruption
        return 1;
    }
    //    j. Zero-extend the 11-bit `(msipte.N10 << 10) | msipte.N90` value to 32 bits,
    //       and do a 32-bit write of this value in little-endian byte order to the
    //       address `msipte.NPPN << 12` (i.e., physical page number `NPPN`, page
    //       offset zero).
    *mrif_nid   = (msipte.mrif.N10 << 10) | msipte.mrif.N90;
    *resp_pa    = (msipte.mrif.NPPN * PAGESIZE);
    *is_mrif_wr = 1;
    //    k. The following rules must be followed to order the write to the destination
    //       MRIF and the write to the notice physical page number (`NPPN`):
    //       i. All writes older than the incoming MSI that was transformed by this
    //          process must be globally visible before the write to the destination
    //          MRIF or to the `NPPN` becomes globally visible; unless protocol specific
    //          relaxation is allowed (e.g. PCIe relaxed ordering) or is not required.
    //       ii.The write to destination MRIF must be globally visible before the write to
    //          `NPPN` becomes globally visible.
    //15. MSI address translation process is complete.
    *R = 1;
    *W = 1;
    *U = 0;
    return 0;
}
