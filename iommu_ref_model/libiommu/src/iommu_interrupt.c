// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"
static void
do_msi(
    iommu_t *iommu,
    uint32_t msi_data, uint64_t msi_addr) {
    uint8_t  status;
    uint64_t pa_mask = ((1UL << (iommu->reg_file.capabilities.pas)) - 1);
    int endian = iommu->reg_file.fctl.be ? BIG_ENDIAN : LITTLE_ENDIAN;
    status = (msi_addr & ~pa_mask) ?
             ACCESS_FAULT :
             write_memory((char *)&msi_data, msi_addr, 4,
                          iommu->reg_file.iommu_qosid.rcid,
                          iommu->reg_file.iommu_qosid.mcid, PMA, endian);
    if ( status & ACCESS_FAULT ) {
        // If an access fault is detected on a MSI write using msi_addr_x,
        // then the IOMMU reports a "IOMMU MSI write access fault" (cause 273) fault,
        // with TTYP set to 0 and iotval set to the value of msi_addr_x.
        report_fault(iommu, 273, msi_addr, 0, TTYPE_NONE, 0, 0, 0, 0, 0);
    }
    return;
}
void
generate_interrupt(
    iommu_t *iommu, uint8_t unit) {

    msi_addr_t msi_addr;
    uint32_t msi_data;
    msi_vec_ctrl_t msi_vec_ctrl;
    uint8_t  vec;

    // Interrupt pending status register (ipsr)
    // This 32-bits register (RW1C) reports the pending interrupts
    // which require software service. Each interrupt-pending bit
    // in the register corresponds to a interrupt source in the IOMMU. When an
    // interrupt-pending bit in the register is set to 1 the IOMMU will not
    // signal another interrupt from that source till software clears that
    // interrupt-pending bit by writing 1 to clear it.
    // Interrupt-cause-to-vector register (icvec)
    // Interrupt-cause-to-vector register maps a cause to a vector. All causes
    // can be mapped to same vector or a cause can be given a unique vector.
    switch ( unit ) {
        case FAULT_QUEUE:
            // The fault-queue-interrupt-pending
            if ( iommu->reg_file.ipsr.fip == 1)
                return;
            if ( iommu->reg_file.fqcsr.fie == 0)
                return;
            vec = iommu->reg_file.icvec.fiv;
            iommu->reg_file.ipsr.fip = 1;
            break;
        case PAGE_QUEUE:
            if ( iommu->reg_file.ipsr.pip == 1)
                return;
            if ( iommu->reg_file.pqcsr.pie == 0)
                return;
            vec = iommu->reg_file.icvec.piv;
            iommu->reg_file.ipsr.pip = 1;
            break;
        case COMMAND_QUEUE:
            if ( iommu->reg_file.ipsr.cip == 1)
                return;
            if ( iommu->reg_file.cqcsr.cie == 0)
                return;
            vec = iommu->reg_file.icvec.civ;
            iommu->reg_file.ipsr.cip = 1;
            break;
        default: // HPM
            if ( iommu->reg_file.ipsr.pmip == 1)
                return;
            vec = iommu->reg_file.icvec.pmiv;
            iommu->reg_file.ipsr.pmip = 1;
            break;
    }
    // The vector is used:
    // 1. By an IOMMU that generates interrupts as MSI, to index into MSI
    //    configuration table (msi_cfg_tbl) to determine the MSI to generate. An
    //    IOMMU is capable of generating interrupts as a MSI if capabilities.IGS==MSI
    //    or if capabilities.IGS==BOTH. When capabilities.IGS==BOTH the IOMMU may be
    //    configured to generate interrupts as MSI by setting fctl.WSI to 0.
    // 2. By an IOMMU that generates wire based interrupts, to determine the wire
    //    to signal the interrupt. An IOMMU is capable of generating wire based
    //    interrupts if capabilities.IGS==WSI or if capabilities.IGS==BOTH. When
    //    capabilities.IGS==BOTH the IOMMU may be configured to generate wire based
    //    interrupts by setting fctl.WSI to 1.
    if ( iommu->reg_file.fctl.wsi == 0 ) {
        msi_addr.raw = iommu->reg_file.msi_cfg_tbl[vec].msi_addr.raw;
        msi_data = iommu->reg_file.msi_cfg_tbl[vec].msi_data;
        msi_vec_ctrl.raw = iommu->reg_file.msi_cfg_tbl[vec].msi_vec_ctrl.raw;
        // When the mask bit M is 1, the corresponding interrupt vector is
        // masked and the IOMMU is prohibited from sending the associated
        // message.
        if ( msi_vec_ctrl.m == 1 ) {
            // Pending messages for that vector are later generated if the
            // corresponding mask bit is cleared to 0.
            iommu->msi_pending[vec] = 1;
            return;
        }
        do_msi(iommu, msi_data, msi_addr.raw);
    }
    return;
}
void
release_pending_interrupt(
    iommu_t *iommu, uint8_t vec) {
    msi_addr_t msi_addr;
    uint32_t msi_data;

    if ( iommu->msi_pending[vec] == 1 ) {
        msi_addr.raw = iommu->reg_file.msi_cfg_tbl[vec].msi_addr.raw;
        msi_data = iommu->reg_file.msi_cfg_tbl[vec].msi_data;
        do_msi(iommu, msi_data, msi_addr.raw);
        iommu->msi_pending[vec] = 0;
    }
    return;
}
