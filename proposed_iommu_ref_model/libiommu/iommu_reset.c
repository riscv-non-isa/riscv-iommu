#include "iommu_registers.h"
#include "iommu_data_structures.h"
#include "iommu_req_rsp.h"
#include "iommu_fault.h"

// This structure is used to determine if a register is a read only or is
// writeable. The bits set to 1 are not writeable
iommu_regs_t g_reg_types;
iommu_regs_t g_reg_file;
uint8_t offset_to_size[4096];

int reset_iommu(uint8_t num_hpm, uint8_t hpmctr_bits, uint16_t num_evts, 
                uint8_t num_vec, capabilities_t capabilities, fctrl_t fctrl) {
    int i, ppn_mask, pa_mask, evtID_mask, num_vec_mask;
    int random;
    srand(time(NULL));
    random = rand() & 0xFF;

    // Only PA upto 56 bits supported in RISC-V
    if ( capabilities.pas > 56 ) {
        return -1;
    }
    // Only one of MSI, WIS, or BOTH supported
    if ( capabilities.igs != MSI && 
         capabilities.igs != WIS && 
         capabilities.igs != IGS_BOTH ) {
        return -1;
    }
    // If IGS_BOTH is not supported then WIS must be 0
    // if MSI is only supported mode else it must be 1
    if ( capabilities.igs != IGS_BOTH && 
         ((capabilities.igs == MSI && fctrl.wis != 0) ||
          (capabilities.igs == WIS && fctrl.wis == 0)) ) {
        return -1;
    }
    // Only 15-bit event ID supported
    if ( num_evts > 32768 ) {
        return -1; 
    }
    // vectors is a number between 1 and 15
    if ( num_vec == 0 || num_vec > 16 ) {
        return -1;
    }
    // Number of HPM counters must be between 0 and 31
    // If perfmon is not supported then should be 0
    if ( num_hpm > 31 ||
         (num_hpm != 0 && capabilities.pmon == 0) ) {
        return -1;
    }
    // HPM counters must be between 1 and 62 bits
    if ( hpmctr_bits < 1 || hpmctr_bits > 62 ) {
        return -1;
    }
    pa_mask  = ((1UL << (capabilities.pas)) - 1);
    ppn_mask = pa_mask >> 12;

    evtID_mask = num_evts--;
    evtID_mask |= evtID_mask >> 1;
    evtID_mask |= evtID_mask >> 2;
    evtID_mask |= evtID_mask >> 4;
    evtID_mask |= evtID_mask >> 8;

    num_vec_mask = num_vec--;
    num_vec_mask |= num_vec_mask >> 1;
    num_vec_mask |= num_vec_mask >> 2;


    // Mark all fields as not writeable by default
    // then clear the fields that are writeable. 
    // The WARL behavior is enforced later
    memset(&g_reg_types, 0xFF, sizeof(g_reg_types));

    // Feature control endian selector is writeable
    // if capabilities indicate bi-endian iommu
    if (capabilities.end == 1)
        g_reg_types.fctrl.end = 0;

    // Mark writeable bits in ddtp
    g_reg_types.ddtp.ppn ^= ppn_mask;
    g_reg_types.ddtp.iommu_mode = 0;
    // Mark writeable bits in cqb
    g_reg_types.cqb.log2szm1 = 0;
    g_reg_types.cqb.ppn ^= ppn_mask;
    // Mark writeable bits in cqt
    g_reg_types.cqt.index = 0;
    // Mark writeable bits in fqb
    g_reg_types.fqb.log2szm1 = 0;
    g_reg_types.fqb.ppn ^= ppn_mask;
    // Mark writeable bits in fqh
    g_reg_types.fqh.index = 0;
    // Mark writeable bits in pqb
    g_reg_types.pqb.log2szm1 = 0;
    g_reg_types.pqb.ppn ^= ppn_mask;
    // Mark writeable bits in pqh
    g_reg_types.pqh.index = 0;
    // Mark writeable bits in cqcsr
    g_reg_types.cqcsr.cqen = 0;
    g_reg_types.cqcsr.cie = 0;
    g_reg_types.cqcsr.cqmf = 0;
    g_reg_types.cqcsr.cmd_to = 0;
    g_reg_types.cqcsr.cmd_ill = 0;
    g_reg_types.cqcsr.fence_w_ip = 0;
    // Mark writeable bits in fqcsr
    g_reg_types.fqcsr.fqen = 0;
    g_reg_types.fqcsr.fie = 0;
    g_reg_types.fqcsr.fqmf = 0;
    g_reg_types.fqcsr.fqof = 0;
    // Mark writeable bits in pqcsr
    g_reg_types.pqcsr.pqen = 0;
    g_reg_types.pqcsr.pie = 0;
    g_reg_types.pqcsr.pqmf = 0;
    g_reg_types.pqcsr.pqof = 0;
    // Mark writeable bits in ipsr
    g_reg_types.ipsr.cip = 0;
    g_reg_types.ipsr.fip = 0;
    g_reg_types.ipsr.pmip = 0;
    g_reg_types.ipsr.pip = 0;
    // Mark writeable bits in iocountinh
    g_reg_types.iocountinh.cy = (capabilities.pmon == 1) ? 0 : 1;
    g_reg_types.iocountinh.hpm = (1UL << num_hpm) - 1;
    // Mark writeable bits in iohpmcycles
    g_reg_types.iohpmcycles.counter = (1UL << hpmctr_bits) - 1;
    g_reg_types.iohpmcycles.of = (capabilities.pmon == 1) ? 0 : 1;
    for ( i = 0; i < num_hpm; i++ ) {
        g_reg_types.iohpmctr[i].counter = (1UL << hpmctr_bits) - 1;
        g_reg_types.iohpmevt[i].eventID ^= evtID_mask;
        g_reg_types.iohpmevt[i].dmask = 0;
        g_reg_types.iohpmevt[i].pid_pscid = 0;
        g_reg_types.iohpmevt[i].did_gscid = 0;
        g_reg_types.iohpmevt[i].pv_pscv = 0;
        g_reg_types.iohpmevt[i].dv_gscv = 0;
        g_reg_types.iohpmevt[i].idt = 0;
        g_reg_types.iohpmevt[i].of = 0;
    }
    g_reg_types.icvec.civ ^= num_vec_mask;
    g_reg_types.icvec.fiv ^= num_vec_mask;
    g_reg_types.icvec.pmiv ^= num_vec_mask;
    g_reg_types.icvec.piv ^= num_vec_mask;

    if ( capabilities.igs == IGS_BOTH || capabilities.igs == MSI ) {
        for ( i = 0; i < num_vec; i++ ) {
            // Bits MAX_PA-1:2 are writeable
            g_reg_types.msi_cfg_tbl[i].msi_addr ^= (pa_mask & ~0x3);
            g_reg_types.msi_cfg_tbl[i].msi_data = 0;
            g_reg_types.msi_cfg_tbl[i].msi_vec_ctrl ^= 1;
        }
    }

    // Initialize the IOMMU register file with random values
    memset(&g_reg_file, random, sizeof(g_reg_file));

    // Clear the reserved bits to 0
    for ( i = 0; i < 4096; i++ ) {
        g_reg_file.regs[i] &= ~g_reg_types.regs[i];
    }
    // Initialize the reset default capabilities and feature
    // control.
    g_reg_file.capabilities = capabilities;
    g_reg_file.fctrl = fctrl;

    // If IOMMU supports both wired and MSI then fctrl selector
    // is implementation defined else set to valud in capabilities
    if ( g_reg_file.capabilities.igs != IGS_BOTH ) {
        g_reg_file.fctrl.wis = (g_reg_file.capabilities.igs == WIS) ? 1 : 0;
    }
    // Make the ddtp.iommu_mode legal
    if ( g_reg_file.ddtp.iommu_mode > DDT_3LVL ) {
        g_reg_file.ddtp.iommu_mode = DDT_3LVL;
    }
    // Make the vectors legal
    g_reg_file.icvec.civ &= num_vec_mask;
    g_reg_file.icvec.fiv &= num_vec_mask;
    g_reg_file.icvec.pmiv &= num_vec_mask;
    g_reg_file.icvec.piv &= num_vec_mask;
    // Initialize registers that have resets to 0
    // The reset default value is 0 for the following registers. 
    // Section 4.2 - Reset value is implementation-defined for all
    // other registers and/or fields.
    // - fctrl
    // - cqcsr
    // - fqcsr
    // - pqcsr
    // - ddtp.busy
    g_reg_file.cqcsr.cqen = 0;
    g_reg_file.cqcsr.cie = 0;
    g_reg_file.cqcsr.cqmf = 0;
    g_reg_file.cqcsr.cmd_to = 0;
    g_reg_file.cqcsr.cmd_ill = 0;
    g_reg_file.cqcsr.fence_w_ip = 0;

    g_reg_file.fqcsr.fqen = 0;
    g_reg_file.fqcsr.fie = 0;
    g_reg_file.fqcsr.fqmf = 0;
    g_reg_file.fqcsr.fqof = 0;

    g_reg_file.pqcsr.pqen = 0;
    g_reg_file.pqcsr.pie = 0;
    g_reg_file.pqcsr.pqmf = 0;
    g_reg_file.pqcsr.pqof = 0;

    g_reg_file.ddtp.busy = 0;

    // Initialize the offset to register size mapping array
    for ( i = 0; i < 4096; i++ ) {
        // Initialize offsets as invalid by default
        offset_to_size[i] = 0xFF;
    }

    offset_to_size[CAPABILITIES_OFFSET] = 8;
    offset_to_size[FCTRL_OFFSET] = 4;
    offset_to_size[DDTP_OFFSET]  = 8;
    offset_to_size[DDTP_OFFSET + 4] = 4;
    offset_to_size[CQB_OFFSET] = 8;
    offset_to_size[CQB_OFFSET + 4] = 4;
    offset_to_size[CQH_OFFSET] = 4;
    offset_to_size[CQT_OFFSET] = 4;
    offset_to_size[FQB_OFFSET] = 8;
    offset_to_size[FQB_OFFSET + 4] = 4;
    offset_to_size[FQH_OFFSET] = 4;
    offset_to_size[FQT_OFFSET] = 4;
    offset_to_size[PQB_OFFSET] = 8;
    offset_to_size[PQB_OFFSET + 4] = 4;
    offset_to_size[PQH_OFFSET] = 4;
    offset_to_size[PQT_OFFSET] = 4;
    offset_to_size[CQCSR_OFFSET] = 4;
    offset_to_size[FQCSR_OFFSET] = 4;
    offset_to_size[PQCSR_OFFSET] = 4;
    offset_to_size[IPSR_OFFSET] = 4;
    offset_to_size[IOCNTOVF_OFFSET] = 4;
    offset_to_size[IOCNTINH_OFFSET] = 4;
    offset_to_size[IOHPMCYCLES_OFFSET] = 4;
    for ( i = IOHPMCTR1_OFFSET; i < IOHPMCTR1_OFFSET + (8 * 31); i += 8 ) {
        offset_to_size[i] = 8;
        offset_to_size[i + 4] = 4;
    }
    for ( i = IOHPMEVT1_OFFSET; i < IOHPMEVT1_OFFSET + (8 * 31); i += 8 ) {
        offset_to_size[i] = 8;
        offset_to_size[i + 4] = 4;
    }
    offset_to_size[ICVEC_OFFSET] = 4;
    for ( i = MSI_CFG_TBL_OFFSET; i < MSI_CFG_TBL_OFFSET + (16 * 16); i += 16 ) {
        offset_to_size[i]     = 8;
        offset_to_size[i + 4] = 4;
        offset_to_size[i+8]   = 4;
        offset_to_size[i+12]  = 4;
    }
    return 0;
}
