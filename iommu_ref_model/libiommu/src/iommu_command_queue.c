// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
uint8_t g_command_queue_stall_for_itag = 0;
uint8_t g_ats_inv_req_timeout = 0;
uint8_t g_iofence_wait_pending_inv = 0;
uint8_t g_iofence_pending_PR, g_iofence_pending_PW, g_iofence_pending_AV, g_iofence_pending_WSI_BIT;
uint64_t g_iofence_pending_ADDR;
uint32_t g_iofence_pending_DATA;

uint8_t g_pending_inval_req_DSV;
uint8_t g_pending_inval_req_DSEG;
uint16_t g_pending_inval_req_RID;
uint8_t g_pending_inval_req_PV;
uint32_t g_pending_inval_req_PID;
uint64_t g_pending_inval_req_PAYLOAD;

void
process_commands(
    void) {
    uint8_t status, itag;
    uint64_t a;
    uint64_t pa_mask = ((1UL << (g_reg_file.capabilities.pas)) - 1);
    command_t command;

    // Command queue is used by software to queue commands to be processed by
    // the IOMMU. Each command is 16 bytes.
    // The PPN of the base of this in-memory queue and the size of the queue
    // is configured into a memorymapped register called command-queue base (cqb).
    // The tail of the command-queue resides in a software controlled read/write
    // memory-mapped register called command-queue tail (cqt). The cqt is an index
    // into the next command queue entry that software will write. Subsequent to
    // writing the command(s), software advances the cqt by the count of the number
    // of commands written. The head of the command-queue resides in a read-only
    // memory-mapped IOMMU controlled register called command-queue head (cqh). The
    // cqh is an index into the command queue that IOMMU should process next.
    //

    // If command-queue access leads to a memory fault then the
    // command-queue-memory-fault cqmf bit is set to 1
    // If the execution of a command leads to a timeout (e.g. a command to invalidate
    // device ATC may timeout waiting for a completion), then the command-queue
    // sets the cmd_to bit.
    // If an illegal or unsupported command is fetched and decoded by the
    // command-queue then the command-queue sets the cmd_ill bit
    // If any of these bits are set then CQ stops processing from the
    // command-queue.
    // The command-queue is active if cqon is 1.
    // Sometimes the command queue may stall due to unavailability of internal
    // resources - e.g. ITAG trackers
    if ( (g_reg_file.cqcsr.cqon == 0) ||
         (g_reg_file.cqcsr.cqen == 0) ||
         (g_reg_file.cqcsr.cqmf != 0) ||
         (g_reg_file.cqcsr.cmd_ill != 0) ||
         (g_reg_file.cqcsr.cmd_to != 0) ||
         (g_command_queue_stall_for_itag != 0) ||
         (g_iofence_wait_pending_inv != 0) )
        return;

    // If cqh == cqt, the command-queue is empty.
    // If cqt == (cqh - 1) the command-queue is full.
    if ( g_reg_file.cqh.index == g_reg_file.cqt.index )
        return;

    a = g_reg_file.cqb.ppn * PAGESIZE | (g_reg_file.cqh.index * CQ_ENTRY_SZ);
    status = (a  & ~pa_mask) ?
             ACCESS_FAULT :
             read_memory(a, CQ_ENTRY_SZ, (char *)&command,
                         g_reg_file.iommu_qosid.rcid, g_reg_file.iommu_qosid.mcid,
                         PMA);
    if ( status != 0 ) {
        // If command-queue access leads to a memory fault then the
        // command-queue-memory-fault bit is set to 1 and the command
        // queue stalls until this bit is cleared. When cqmf is set to 1, an
        // interrupt is generated if an interrupt is not already pending (i.e.,
        // ipsr.cip == 1) and not masked (i.e. cqsr.cie == 0). To reenable
        // command processing, software should clear this bit by writing 1
        if ( g_reg_file.cqcsr.cqmf == 0 ) {
            g_reg_file.cqcsr.cqmf = 1;
            generate_interrupt(COMMAND_QUEUE);
        }

        return;
    }

    // IOMMU commands are grouped into a major command group determined by the
    // opcode and within each group the func3 field specifies the function invoked
    // by that command. The opcode defines the format of the operand fields. One
    // or more of those fields may be used by the specific function invoked.
    // A command is determined to be illegal if it uses a reserved encoding or if a
    // reserved bit is set to 1. A command is unsupported if it is defined but not
    // implemented as determined by the IOMMU capabilities register.
    switch ( command.any.opcode ) {
        case IOTINVAL:
            // The non-leaf PTE invalidation extension is implemented if the
            // capabilities.NL (bit 42) is 1. When the capabilities.NL bit is 1, a
            // non-leaf (NL) field is defined at bit 34 in the IOTINVAL.VMA and
            // IOTINVAL.GVMA commands by this extension. When the capabilities.NL
            // bit is 0, bit 34 remains reserved.
            // The address range invalidation extension is implemented if
            // `capabilities.S` (bit 43) is 1. When `capabilities.S` is 1, a
            // range-size (`S`) operand is defined at bit 73 in the `IOTINVAL.VMA` and
            // `IOTINVAL.GVMA` commands by this extension. When the `capabilities.S`
            // bit is 0, bit 73 remains reserved.
            if ( command.iotinval.rsvd != 0 || command.iotinval.rsvd1 != 0 ||
                 command.iotinval.rsvd2 != 0 || command.iotinval.rsvd3 != 0 ||
                 command.iotinval.rsvd4 != 0 ||
                 (g_reg_file.capabilities.nl == 0 && command.iotinval.nl != 0) ||
                 (g_reg_file.capabilities.s  == 0 && command.iotinval.s  != 0) )
                goto command_illegal;
            switch ( command.any.func3 ) {
                case VMA:
                    do_iotinval_vma(command.iotinval.gv, command.iotinval.av,
                                    command.iotinval.nl, command.iotinval.pscv,
                                    command.iotinval.gscid, command.iotinval.pscid,
                                    command.iotinval.addr_63_12, command.iotinval.s);
                    break;
                case GVMA:
                    // Setting PSCV to 1 with IOTINVAL.GVMA is illegal.
                    if ( command.iotinval.pscv != 0 ) goto command_illegal;
                    do_iotinval_gvma(command.iotinval.gv, command.iotinval.av,
                                     command.iotinval.nl, command.iotinval.gscid,
                                     command.iotinval.addr_63_12, command.iotinval.s);
                    break;
                default: goto command_illegal;
            }
            break;
        case IODIR:
            if ( command.iodir.rsvd != 0 || command.iodir.rsvd1 != 0 ||
                 command.iodir.rsvd2 != 0 || command.iodir.rsvd3 != 0 )
                goto command_illegal;
            switch ( command.any.func3 ) {
                case INVAL_DDT:
                    // The PID operand is reserved for the
                    // IODIR.INVAL_DDT command.
                    if ( command.iodir.pid != 0 ) goto command_illegal;
                    // When DV operand is 1, the value of the DID operand must not
                    // be wider than that supported by the ddtp.iommu_mode.
                    if ( command.iodir.dv &&
                         (command.iodir.did & ~g_max_devid_mask) ) {
                        goto command_illegal;
                    }
                    do_inval_ddt(command.iodir.dv, command.iodir.did);
                    break;
                case INVAL_PDT:
                    // The DV operand must be 1 for IODIR.INVAL_PDT else
                    // the command is illegal. When DV operand is 1, the value of
                    // the DID operand must not be wider than that supported by
                    // the ddtp.iommu_mode.
                    if ( command.iodir.dv != 1 ) goto command_illegal;
                    // When DV operand is 1, the value of the DID operand must not
                    // be wider than that supported by the ddtp.iommu_mode.
                    if ( command.iodir.did & ~g_max_devid_mask )
                        goto command_illegal;
                    // The PID operand of IODIR.INVAL_PDT must not be wider than
                    // the width supported by the IOMMU (see Section 5.3)
                    if ( g_reg_file.capabilities.pd20 == 0 &&
                         command.iodir.pid > ((1UL << 17) - 1) ) {
                        goto command_illegal;
                    }
                    if ( g_reg_file.capabilities.pd20 == 0 &&
                         g_reg_file.capabilities.pd17 == 0 &&
                         command.iodir.pid > ((1UL << 8) - 1) ) {
                        goto command_illegal;
                    }
                    do_inval_pdt(command.iodir.did, command.iodir.pid);
                    break;
                default: goto command_illegal;
            }
            break;
        case IOFENCE:
            if ( command.iofence.reserved != 0 || command.iofence.reserved1 != 0 )
                goto command_illegal;
            // The wired-signaled-interrupt (WSI) bit when set to 1
            // causes a wired-interrupt from the command
            // queue to be generated on completion of IOFENCE.C. This
            // bit is reserved if the IOMMU supports MSI.
            if ( g_reg_file.fctl.wsi == 0 && command.iofence.wsi == 1)
                goto command_illegal;
            switch ( command.any.func3 ) {
                case IOFENCE_C:
                    if ( do_iofence_c(command.iofence.pr, command.iofence.pw,
                             command.iofence.av, command.iofence.wsi,
                             (command.iofence.addr_63_2 << 2UL), command.iofence.data) ) {
                        // If IOFENCE encountered a memory fault or timeout
                        // then do not advance the CQH
                        // If IOFENCE is waiting for invalidation requests
                        // to complete then do not advance the CQ head
                        return;
                    }
                    break;
                default: goto command_illegal;
            }
            break;
        case ATS:
            if ( command.ats.rsvd != 0 || command.ats.rsvd1 != 0 ) goto command_illegal;
            switch ( command.any.func3 ) {
                case INVAL:
                    // Allocate a ITAG for the request
                    if ( allocate_itag(command.ats.dsv, command.ats.dseg,
                            command.ats.rid, &itag) ) {
                        // No ITAG available, This command stays pending
                        // but since the reference implementation only
                        // has one deep pending command buffer the CQ
                        // is now stall till a completion or a timeout
                        // frees up pending ITAGs.
                        g_pending_inval_req_DSV = command.ats.dsv;
                        g_pending_inval_req_DSEG = command.ats.dseg;
                        g_pending_inval_req_RID = command.ats.rid;
                        g_pending_inval_req_PV = command.ats.pv;
                        g_pending_inval_req_PID = command.ats.pid;
                        g_pending_inval_req_PAYLOAD = command.ats.payload;
                        g_command_queue_stall_for_itag = 1;
                    } else {
                        // ITAG allocated successfully, send invalidate request
                        do_ats_msg(INVAL_REQ_MSG_CODE, itag, command.ats.dsv, command.ats.dseg,
                            command.ats.rid, command.ats.pv, command.ats.pid, command.ats.payload);
                    }
                    break;
                case PRGR:
                    do_ats_msg(PRGR_MSG_CODE, 0, command.ats.dsv, command.ats.dseg,
                        command.ats.rid, command.ats.pv, command.ats.pid, command.ats.payload);
                    break;
                default: goto command_illegal;
            }
            break;
        default: goto command_illegal;
    }
    // The head of the command-queue resides in a read-only memory-mapped IOMMU
    // controlled register called command-queue head (`cqh`). The `cqh` is an index
    // into the command queue that IOMMU should process next. Subsequent to reading
    // each command the IOMMU may advance the `cqh` by 1.
    g_reg_file.cqh.index =
        (g_reg_file.cqh.index + 1) & ((1UL << (g_reg_file.cqb.log2szm1 + 1)) - 1);
    return;

command_illegal:
    // If an illegal or unsupported command is fetched and decoded by
    // the command-queue then the command-queue sets the cmd_ill
    // bit and stops processing from the command-queue. When cmd_ill
    // is set to 1, an interrupt is generated if not already pending (i.e.
    // ipsr.cip == 1) and not masked (i.e. cqsr.cie == 0). To reenable
    // command processing software should clear this bit by writing 1
    if ( g_reg_file.cqcsr.cmd_ill == 0 ) {
        g_reg_file.cqcsr.cmd_ill = 1;
        generate_interrupt(COMMAND_QUEUE);
    }
    return;
}
void
do_inval_ddt(
    uint8_t DV, uint32_t DID) {
    uint8_t i;
    // IOMMU operations cause implicit reads to DDT and/or PDT.
    // To reduce latency of such reads, the IOMMU may cache entries from
    // the DDT and/or PDT in IOMMU directory caches. These caches may not
    // observe modifications performed by software to these data structures
    // in memory.
    // The IOMMU DDT cache invalidation command, `IODIR.INVAL_DDT`
    // synchronize updates to DDT with the operation of the IOMMU and
    // flushes the matching cached entries.
    // The `DV` operand indicates if the device ID (`DID`) operand is valid.
    // `IODIR.INVAL_DDT` guarantees that any previous stores made by a RISC-V hart to
    // the DDT are observed before all subsequent implicit reads from IOMMU to DDT.
    // If `DV` is 0, then the command invalidates all  DDT and PDT entries cached for
    // all devices. If `DV` is 1, then the command invalidates cached leaf level DDT
    // entry for the device identified by `DID` operand and all associated PDT entries.
    // The `PID` operand is reserved for `IODIR.INVAL_DDT`.
    for ( i = 0; i < DDT_CACHE_SIZE; i++ ) {
        if ( ddt_cache[i].valid == 0 ) continue;
        if ( DV == 0 ) ddt_cache[i].valid = 0;
        if ( DV == 1 && (ddt_cache[i].DID == DID) )
            ddt_cache[i].valid = 0;
    }
    return;
}
void
do_inval_pdt(
    uint32_t DID, uint32_t PID) {
    int i;

    // IOMMU operations cause implicit reads to DDT and/or PDT.
    // To reduce latency of such reads, the IOMMU may cache entries from
    // the DDT and/or PDT in IOMMU directory caches. These caches may not
    // observe modifications performed by software to these data structures
    // in memory.
    // The IOMMU PDT cache invalidation command, `IODIR.INVAL_PDT` synchronize
    // updates to PDT with the operation of the IOMMU and flushes the matching
    // cached entries.
    // The `DV` operand must be 1 for `IODIR.INVAL_PDT`.
    // `IODIR.INVAL_PDT` guarantees that any previous stores made by a RISC-V hart to
    // the PDT are observed before all subsequent implicit reads from IOMMU to PDT.
    // The command invalidates cached leaf PDT entry for the specified `PID` and `DID`.

    for ( i = 0; i < PDT_CACHE_SIZE; i++ )
        if ( pdt_cache[i].DID == DID && pdt_cache[i].PID == PID && pdt_cache[i].valid == 1)
            pdt_cache[i].valid = 0;
    return;
}

void
do_iotinval_vma(
    uint8_t GV, uint8_t AV, uint8_t NL, uint8_t PSCV, uint32_t GSCID,
    uint32_t PSCID, uint64_t ADDR_63_12, uint8_t S) {

    // IOMMU operations cause implicit reads to PDT, first-stage and second-stage
    // page tables. To reduce latency of such reads, the IOMMU may cache entries
    // from the first and/or second-stage page tables in the
    // IOMMU-address-translation-cache (IOATC). These caches may not observe
    // modifications performed by software to these data structures in memory.
    // The IOMMU translation-table cache invalidation commands, IOTINVAL.VMA
    // and IOTINVAL.GVMA synchronize updates to in-memory S/VS-stage and G-stage
    // page table data structures with the operation of the IOMMU and invalidate
    // the matching IOATC entries.
    // The GV operand indicates if the Guest-Soft-Context ID (GSCID) operand is
    // valid. The PSCV operand indicates if the Process Soft-Context ID (PSCID)
    // operand is valid. Setting PSCV to 1 is allowed only for IOTINVAL.VMA. The
    // AV operand indicates if the address (ADDR) operand is valid. When GV is 0,
    // the translations associated with the host (i.e. those where the
    // second-stage translation is not active) are operated on.
    // IOTINVAL.VMA ensures that previous stores made to the first-stage page
    // tables by the harts are observed by the IOMMU before all subsequent
    // implicit reads from IOMMU to the corresponding firststage page tables.
    //
    // .`IOTINVAL.VMA` operands and operations
    // |`GV`|`AV`|`PSCV`| Operation
    // |0   |0   |0     | Invalidates all address-translation cache entries, including
    //                    those that contain global mappings, for all host address
    //                    spaces.
    // |0   |0   |1     | Invalidates all address-translation cache entries for the
    //                    host address space identified by `PSCID` operand, except for
    //                    entries containing global mappings.
    // |0   |1   |0     | Invalidates all address-translation cache entries that
    //                    contain leaf page table entries, including those that contain
    //                    global mappings, corresponding to the IOVA in `ADDR` operand,
    //                    for all host address spaces.
    // |0   |1   |1     | Invalidates all address-translation cache entries that
    //                    contain leaf page table entries corresponding to the IOVA in
    //                    `ADDR` operand and that match the host address space
    //                    identified by `PSCID` operand, except for entries containing
    //                    global mappings.
    // |1   |0   |0     | Invalidates all address-translation cache entries, including
    //                    those that contain global mappings, for all VM address spaces
    //                    associated with `GSCID` operand.
    // |1   |0   |1     | Invalidates all address-translation cache entries for the
    //                    for the VM address space identified by `PSCID` and `GSCID`
    //                    operands, except for entries containing global mappings.
    // |1   |1   |0     | Invalidates all address-translation cache entries that
    //                    contain leaf page table entries, including those that contain
    //                    global mappings, corresponding to the IOVA in `ADDR` operand,
    //                    for all VM address spaces associated with the `GSCID` operand.
    // |1   |1   |1     | Invalidates all address-translation cache entries that
    //                    contain leaf page table entries corresponding to the IOVA in
    //                    `ADDR` operand, for the VM address space identified by `PSCID`
    //                    and `GSCID` operands, except for entries containing global
    //                    mappings.

    uint8_t i, gscid_match, pscid_match, addr_match, global_match;

    // The address range invalidation extension adds the S bit.
    // When the AV operand is 0, the S operand is ignored in both the IOTINVAL.VMA and
    // IOTINVAL.GVMA commands. When the S operand is ignored or set to 0, the operations of
    // the IOTINVAL.VMA and IOTINVAL.GVMA commands are as specified in the RISC-V IOMMU
    // Version 1.0 specification.
    // When the S operand is not ignored and is 1, the ADDR operand represents a NAPOT
    // range encoded in the operand itself. Starting from bit position 0 of the ADDR operand,
    // if the first 0 bit is at position X, the range size is 2(X+1) * 4 KiB. When X is 0,
    // the size of the range is 8 KiB.  If the S operand is not ignored and is 1 and all bits
    // of the ADDR operand are 1, the behavior is UNSPECIFIED.
    //   * The model treats this unspecified behavior as matching the entire address space.
    // If the S operand is not ignored and is 1 and the most significant bit of the ADDR
    // operand is 0 while all other bits are 1, the specified address range covers the entire
    // address space

    for ( i = 0; i < TLB_SIZE; i++ ) {
        gscid_match = pscid_match = addr_match = global_match = 0;
        if ( (GV == 0 && tlb[i].GV == 0 ) ||
             (GV == 1 && tlb[i].GV == 1 && tlb[i].GSCID == GSCID) )
            gscid_match = 1;
        if ( (PSCV == 0) ||
             (PSCV == 1 && tlb[i].PSCV == 1 && tlb[i].PSCID == PSCID) )
            pscid_match = 1;
        if ( (PSCV == 0) ||
             (PSCV == 1 && tlb[i].G == 0) )
            global_match = 1;
        if ( (AV == 0) ||
             (AV == 1 && match_address_range(ADDR_63_12, S, tlb[i].vpn, tlb[i].S)) )
            addr_match = 1;
        if ( gscid_match && pscid_match && addr_match && global_match )
            tlb[i].valid = 0;
    }
    // This model implementation does not have non-leaf PTE caches. This
    // information is for documentation only.
    // * When the `AV` operand is 0, the `NL` operand is ignored and the `IOTINVAL.VMA`
    //   command operations are as specified in RISC-V IOMMU Version 1.0 specification.
    // * When the `AV` operand is 1 and the `NL` operand is 0, the `IOTINVAL.VMA`
    //   command operations are as specified in RISC-V IOMMU Version 1.0 specification.
    // * When both the `AV` and `NL` operands are 1, the `IOTINVAL.VMA` command
    //   performs the following operations:
    //   ** When `GV=0` and `PSCV=0`: Invalidates information cached from all levels of
    //      first-stage page table entries corresponding to the IOVA in the `ADDR`
    //      operand for all host address spaces, including entries containing global
    //      mappings.
    //   ** When `GV=0` and `PSCV=1`: Invalidates information cached from all levels of
    //      first-stage page table entries corresponding to the IOVA in the `ADDR`
    //      operand and the host address space identified by the `PSCID` operand, except
    //      for entries containing global mappings.
    //   ** When `GV=1` and `PSCV=0`: Invalidates information cached from all levels of
    //      first-stage page table entries corresponding to the IOVA in the `ADDR`
    //      operand for all VM address spaces associated with the `GSCID` operand,
    //      including entries that contain global mappings.
    //   ** When `GV=1` and `PSCV=1`: Invalidates information cached from all levels of
    //      first-stage page table entries corresponding to the IOVA in the `ADDR`
    //      operand and the VM address space identified by the `PSCID` and `GSCID`
    //      operands, except for entries containing global mappings.
    //
    // if (AV == 1 && NL == 1) {
    //     invalidate_vs_stage_nl_pte_caches(GV, AV, NL, PSCV, GSCID, PSCID, ADDR_63_12);
    // }
    return;
}
void
do_iotinval_gvma(
    uint8_t GV, uint8_t AV, uint8_t NL, uint32_t GSCID, uint64_t ADDR_63_12, uint8_t S) {

    uint8_t i, gscid_match, addr_match;
    // Conceptually, an implementation might contain two address-translation
    // caches: one that maps guest virtual addresses to guest physical addresses,
    // and another that maps guest physical addresses to supervisor physical
    // addresses. IOTINVAL.GVMA need not flush the former cache, but it must
    // flush entries from the latter cache that match the IOTINVAL.GVMA’s
    // address and GSCID arguments.
    // More commonly, implementations contain address-translation caches
    // that map guest virtual addresses directly to supervisor physical
    // addresses, removing a level of indirection. For such implementations,
    // any entry whose guest virtual address maps to a guest physical address that
    // matches the IOTINVAL.GVMA’s address and GSCID arguments must be flushed.
    // Selectively flushing entries in this fashion requires tagging them with
    // the guest physical address, which is costly, and so a common technique
    // is to flush all entries that match the IOTINVAL.GVMA’s GSCID argument,
    // regardless of the address argument.
    // IOTINVAL.GVMA ensures that previous stores made to the G-stage page
    // tables are observed before all subsequent implicit reads from IOMMU
    // to the corresponding G-stage page tables. Setting PSCV to 1 with
    // IOTINVAL.GVMA is illegal.
    // .`IOTINVAL.GVMA` operands and operations
    // | `GV` | `AV`   | Operation
    // | 0    | n/a    | Invalidates information cached from any level of the
    //                   G-stage page table, for all VM address spaces.
    // | 1    | 0      | Invalidates information cached from any level of the
    //                   G-stage page tables, but only for VM address spaces
    //                   identified by the `GSCID` operand.
    // | 1    | 1      | Invalidates information cached from leaf G-stage page
    //                   table entries corresponding to the guest-physical-address in
    //                   `ADDR` operand, for only for VM address spaces identified
    //                   `GSCID` operand.
    //
    // The address range invalidation extension adds the S bit.
    // When the GV operand is 0, both the AV and S operands are ignored by the
    // IOTINVAL.GVMA command.
    // When the AV operand is 0, the S operand is ignored in both the IOTINVAL.VMA and
    // IOTINVAL.GVMA commands. When the S operand is ignored or set to 0, the operations of
    // the IOTINVAL.VMA and IOTINVAL.GVMA commands are as specified in the RISC-V IOMMU
    // Version 1.0 specification.
    // When the S operand is not ignored and is 1, the ADDR operand represents a NAPOT
    // range encoded in the operand itself. Starting from bit position 0 of the ADDR operand,
    // if the first 0 bit is at position X, the range size is 2(X+1) * 4 KiB. When X is 0,
    // the size of the range is 8 KiB.  If the S operand is not ignored and is 1 and all bits
    // of the ADDR operand are 1, the behavior is UNSPECIFIED.
    //   * The model treats this unspecified behavior as matching the entire address space.
    // If the S operand is not ignored and is 1 and the most significant bit of the ADDR
    // operand is 0 while all other bits are 1, the specified address range covers the entire
    // address space
    for ( i = 0; i < TLB_SIZE; i++ ) {
        if ( tlb[i].valid == 0 ) continue;
        if ( (GV == 0 && tlb[i].GV == 1) ||
             (GV == 1 && tlb[i].GV == 1 && tlb[i].GSCID == GSCID) )
            gscid_match = 1;
        // If the cache holds a VA -> SPA translation i.e. PSCV == 1 then invalidate
        // it. If PSCV is 0 then it holds a GPA. If AV is 0 then all entries are
        // eligible else match the address
        if ( (tlb[i].PSCV == 1) || (AV == 0) || (GV == 0) ||
             (tlb[i].PSCV == 0 && AV == 1 && match_address_range(ADDR_63_12, S, tlb[i].vpn, tlb[i].S)) )
            addr_match = 1;
        if ( gscid_match && addr_match )
            tlb[i].valid = 0;
    }
    // This model implementation does not have non-leaf PTE caches. This
    // information is for documentation only.
    // * When the `GV` operand is 0, both the `AV` and `NL` operands are ignored and
    //   the `IOTINVAL.GVMA` command operations are as specified in RISC-V IOMMU
    //   Version 1.0 specification.
    // * When the `GV` operand is 1 and the `AV` operand is 0, the `NL` operand is
    //   ignored and the `IOTINVAL.GVMA` command operations are as specified in
    //   RISC-V IOMMU Version 1.0 specification.
    // * When the `GV` and `AV` operands are 1 and the `NL` operand is 0, the
    //   `IOTINVAL.GVMA` command operations are as specified in RISC-V IOMMU Version
    //   1.0 specification.
    // * When `GV`, `AV`, and `NL` are all 1, the `IOTINVAL.GVMA` command performs the
    //   following operations:
    //   ** Invalidates information cached from all levels of second-stage page table
    //      entries corresponding to the guest-physical address in the `ADDR` operand and
    //      the VM address spaces identified by the `GSCID` operand.
    //
    // if (GV == 1 && AV == 1 && NL == 1) {
    //     invalidate_g_stage_NL_pte_caches(GV, AV, NL, GSCID, ADDR_63_12);
    // }
    return;
}
void
do_ats_msg(
    uint8_t MSGCODE, uint8_t TAG, uint8_t DSV, uint8_t DSEG, uint16_t RID,
    uint8_t PV, uint32_t PID, uint64_t PAYLOAD) {
    ats_msg_t msg;
    // The ATS.INVAL command instructs the IOMMU to send a “Invalidation Request” message
    // to the PCIe device function identified by RID. An “Invalidation Request” message
    // is used to clear a specific subset of the address range from the address translation
    // cache in a device function. The ATS.INVAL command completes when an “Invalidation
    // Completion” response message is received from the device or a protocol defined
    // timeout occurs while waiting for a response. The IOMMU may advance the cqh and fetch
    // more commands from CQ while a response is awaited.
    // The ATS.PRGR command instructs the IOMMU to send a “Page Request Group Response”
    // message to the PCIe device function identified by the RID. The “Page Request Group
    // Response” message is used by system hardware and/or software to communicate with the
    // device functions page-request interface to signal completion of a “Page Request”, or
    // the catastrophic failure of the interface.If the PV operand is set to 1, the message
    // is generated with a PASID with the PASID field set to the PID operand. The PAYLOAD
    // operand of the command is used to form the message body.
    // If the DSV operand is 1, then a valid destination segment number is specified by
    // the DSEG operand.
    msg.MSGCODE = MSGCODE;
    msg.TAG     = TAG;
    msg.RID     = RID;
    msg.DSV     = DSV;
    msg.DSEG    = DSEG;
    msg.PV      = PV;
    msg.PID     = PID;
    msg.PAYLOAD = PAYLOAD;
    msg.PRIV    = 0;
    msg.EXEC_REQ= 0;
    send_msg_iommu_to_hb(&msg);
    return;
}
uint8_t
do_iofence_c(
    uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WSI_BIT, uint64_t ADDR, uint32_t DATA) {

    uint8_t status;
    uint64_t pa_mask = ((1UL << (g_reg_file.capabilities.pas)) - 1);
    // The IOMMU fetches commands from the CQ in order but the IOMMU may execute the fetched
    // commands out of order. The IOMMU advancing cqh is not a guarantee that the commands
    // fetched by the IOMMU have been executed or committed. A IOFENCE.C command guarantees
    // that all previous commands fetched from the CQ have been completed and committed.
    g_iofence_wait_pending_inv = 1;
    if ( any_ats_invalidation_requests_pending() ) {
        // if all previous ATS invalidation requests
        // have not completed then IOFENCE waits for
        // them to complete - or timeout
        g_iofence_pending_PR = PR;
        g_iofence_pending_PW = PW;
        g_iofence_pending_AV = AV;
        g_iofence_pending_WSI_BIT = WSI_BIT;
        g_iofence_pending_ADDR = ADDR;
        g_iofence_pending_DATA = DATA;
        return 1;
    }
    // All previous pending invalidation requests completed or timed out
    g_iofence_wait_pending_inv = 0;
    // If any ATC invalidation requests timed out then set command timeout
    if ( g_ats_inv_req_timeout == 1 ) {
        if ( g_reg_file.cqcsr.cmd_to == 0 ) {
            g_reg_file.cqcsr.cmd_to = 1;
            generate_interrupt(COMMAND_QUEUE);
        }
        g_ats_inv_req_timeout = 0;
        return 1;
    }
    // The commands may be used to order memory accesses from I/O devices connected to the IOMMU
    // as viewed by the IOMMU, other RISC-V harts, and external devices or co-processors. The
    // PR and PW bits can be used to request that the IOMMU ensure that all previous requests
    // from devices that have already been processed by the IOMMU be committed to a global
    // ordering point such that they can be observed by all RISC-V harts and IOMMUs in the machine.
    if ( PR == 1 || PW == 1 )
        iommu_to_hb_do_global_observability_sync(PR, PW);

    // The AV command operand indicates if ADDR[63:2] operand and DATA operands are valid.
    // If AV=1, the IOMMU writes DATA to memory at a 4-byte aligned address ADDR[63:2] * 4 as
    // a 4-byte store.
    if ( AV == 1 ) {
        status = (ADDR & ~pa_mask) ?
                 ACCESS_FAULT :
                 write_memory((char *)&DATA, ADDR, 4, g_reg_file.iommu_qosid.rcid,
                              g_reg_file.iommu_qosid.mcid, PMA);
        if ( status != 0 ) {
            if ( g_reg_file.cqcsr.cqmf == 0 ) {
                g_reg_file.cqcsr.cqmf = 1;
                generate_interrupt(COMMAND_QUEUE);
            }
            return 1;
        }
    }
    // The wired-signaled-interrupt (WSI) bit when set to 1 causes a wired-interrupt from the command
    // queue to be generated on completion of IOFENCE.C. This bit is reserved if the IOMMU supports MSI
    if ( g_reg_file.cqcsr.fence_w_ip == 0 && WSI_BIT == 1 ) {
        g_reg_file.cqcsr.fence_w_ip = 1;
        generate_interrupt(COMMAND_QUEUE);
    }
    return 0;
}
// Retry a pending IOFENCE if all invalidations received
void
do_pending_iofence() {
    if ( do_iofence_c(g_iofence_pending_PR, g_iofence_pending_PW, g_iofence_pending_AV,
                 g_iofence_pending_WSI_BIT, g_iofence_pending_ADDR, g_iofence_pending_DATA) == 0 ) {
        // If not still pending then advance the CQH
        g_reg_file.cqh.index =
            (g_reg_file.cqh.index + 1) & ((1UL << (g_reg_file.cqb.log2szm1 + 1)) - 1);
    }
    // If IOFENCE is not pending and CQ was requested to be
    // turned off then turn it off now
    if ( g_iofence_wait_pending_inv == 0 ) {
        g_reg_file.cqcsr.cqon = g_reg_file.cqcsr.cqen;
        g_reg_file.cqcsr.busy = 0;
    }
    return;
}
void
queue_any_blocked_ats_inval_req() {
    uint8_t itag;
    if ( g_command_queue_stall_for_itag == 1 ) {
        // Allocate a ITAG for the request
        if ( allocate_itag(g_pending_inval_req_DSV, g_pending_inval_req_DSEG,
                           g_pending_inval_req_RID, &itag) )
            return;
        // ITAG allocated successfully, send invalidate request
        do_ats_msg(INVAL_REQ_MSG_CODE, itag, g_pending_inval_req_DSV,
                   g_pending_inval_req_DSEG, g_pending_inval_req_RID,
                   g_pending_inval_req_PV, g_pending_inval_req_PID,
                   g_pending_inval_req_PAYLOAD);
        // Remove the command queue stall
        g_command_queue_stall_for_itag = 0;
    }
    return;
}
