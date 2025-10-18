// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include <stdio.h>
#include <inttypes.h>
#include "iommu.h"
#include "tables_api.h"
#include "test_app.h"
int8_t
reset_system(
    uint8_t mem_gb, uint16_t num_vms) {
    uint32_t gscid;
    // Create memory
    if ( (memory = malloc((mem_gb * 1024UL * 1024UL * 1024UL))) == NULL )
        return -1;

    // Initialize free list of pages
    for ( gscid = 0; gscid < 65536; gscid++ ) next_free_gpage[gscid] = 0;
    next_free_page = 0;
    return 0;
}
uint64_t
get_free_gppn(uint64_t num_gppn, iohgatp_t iohgatp) {
    uint64_t free_gppn = next_free_gpage[iohgatp.GSCID];

    if ( free_gppn & (num_gppn -1) ) {
        free_gppn = free_gppn + (num_gppn -1);
        free_gppn = free_gppn & ~(num_gppn -1);
    }
    next_free_gpage[iohgatp.GSCID] = free_gppn + num_gppn;
    return free_gppn;
}
uint32_t
log2szm1(uint32_t n) {
#if defined(__GNUC__) || defined(__clang__)
    return 31 - __builtin_clz(n);
#else
    uint32_t log2sz = 0;
    while (n >>= 1) {
        log2sz++;
    }
    return log2sz > 0 ? log2sz-1 : 0;
#endif
}
int8_t
enable_cq(
    iommu_t *iommu,
    uint32_t nppn) {
    cqb_t cqb;
    cqcsr_t cqcsr;

    cqb.raw = 0;
    cqb.ppn = get_free_ppn(nppn);
    cqb.log2szm1 = log2szm1((nppn * PAGESIZE)/CQ_ENTRY_SZ);
    write_register(iommu, CQB_OFFSET, 8, cqb.raw);
    do {
        cqcsr.raw = read_register(iommu, CQCSR_OFFSET, 4);
    } while ( cqcsr.busy == 1 );
    cqcsr.raw = 0;
    cqcsr.cie = 1;
    cqcsr.cqen = 1;
    cqcsr.cqmf = 1;
    cqcsr.cmd_to = 1;
    cqcsr.cmd_ill = 1;
    cqcsr.fence_w_ip = 1;
    write_register(iommu, CQCSR_OFFSET, 4, cqcsr.raw);
    do {
        cqcsr.raw = read_register(iommu, CQCSR_OFFSET, 4);
    } while ( cqcsr.busy == 1 );
    if ( cqcsr.cqon != 1 ) {
        printf("CQ enable failed\n");
        return -1;
    }
    return 0;
}

int8_t
enable_fq(
    iommu_t *iommu,
    uint32_t nppn) {
    fqb_t fqb;
    fqcsr_t fqcsr;

    fqb.raw = 0;
    fqb.ppn = get_free_ppn(nppn);
    fqb.log2szm1 = log2szm1((nppn * PAGESIZE)/FQ_ENTRY_SZ);
    write_register(iommu, FQB_OFFSET, 8, fqb.raw);
    do {
        fqcsr.raw = read_register(iommu, FQCSR_OFFSET, 4);
    } while ( fqcsr.busy == 1 );
    fqcsr.raw = 0;
    fqcsr.fie = 1;
    fqcsr.fqen = 1;
    fqcsr.fqmf = 1;
    fqcsr.fqof = 1;
    write_register(iommu, FQCSR_OFFSET, 4, fqcsr.raw);
    do {
        fqcsr.raw = read_register(iommu, FQCSR_OFFSET, 4);
    } while ( fqcsr.busy == 1 );
    if ( fqcsr.fqon != 1 ) {
        printf("FQ enable failed\n");
        return -1;
    }
    return 0;
}

int8_t
enable_disable_pq(
    iommu_t *iommu,
    uint32_t nppn, uint8_t enable_disable) {
    pqb_t pqb;
    pqcsr_t pqcsr;

    if ( enable_disable == 1 ) {
        pqb.raw = 0;
        pqb.ppn = get_free_ppn(nppn);
        pqb.log2szm1 = log2szm1((nppn * PAGESIZE)/PQ_ENTRY_SZ);
        write_register(iommu, PQB_OFFSET, 8, pqb.raw);
    }
    do {
        pqcsr.raw = read_register(iommu, PQCSR_OFFSET, 4);
    } while ( pqcsr.busy == 1 );
    pqcsr.raw = 0;
    pqcsr.pie = 1;
    pqcsr.pqen = enable_disable;
    pqcsr.pqmf = 1;
    pqcsr.pqof = 1;
    write_register(iommu, PQCSR_OFFSET, 4, pqcsr.raw);
    do {
        pqcsr.raw = read_register(iommu, PQCSR_OFFSET, 4);
    } while ( pqcsr.busy == 1 );
    if ( pqcsr.pqon != 1 && enable_disable == 1) {
        printf("PQ enable failed\n");
        return -1;
    }
    if ( pqcsr.pqon == 1 && enable_disable == 0 ) {
        printf("PQ disable failed\n");
        return -1;
    }
    return 0;
}

int8_t
enable_iommu(
    iommu_t *iommu,
    uint8_t iommu_mode) {
    ddtp_t ddtp;
    uint32_t i;
    uint64_t zero = 0;

    // Allocate a page for DDT root page
    do {
        ddtp.raw = read_register(iommu, DDTP_OFFSET, 8);
    } while ( ddtp.busy == 1 );

    ddtp.raw = 0;
    ddtp.ppn = get_free_ppn(1);
    // Clear the page
    for ( i = 0; i < 512; i++ )
        write_memory_test((char *)&zero, (ddtp.ppn * PAGESIZE) | (i * 8), 8);

    ddtp.iommu_mode = iommu_mode;
    write_register(iommu, DDTP_OFFSET, 8, ddtp.raw);
    do {
        ddtp.raw = read_register(iommu, DDTP_OFFSET, 8);
    } while ( ddtp.busy == 1 );
    return (ddtp.iommu_mode == iommu_mode) ? 0 : -1;
}
void
send_translation_request(iommu_t *iommu, uint32_t did, uint8_t pid_valid, uint32_t pid, uint8_t no_write,
    uint8_t exec_req, uint8_t priv_req, uint8_t is_cxl_dev, addr_type_t at, uint64_t iova,
    uint32_t length, uint8_t read_writeAMO,
    hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp) {

    req->device_id        = did;
    req->pid_valid        = pid_valid;
    req->process_id       = pid;
    req->no_write         = no_write;
    req->exec_req         = exec_req;
    req->priv_req         = priv_req;
    req->is_cxl_dev       = is_cxl_dev;
    req->tr.at            = at;
    req->tr.iova          = iova;
    req->tr.length        = length;
    req->tr.read_writeAMO = read_writeAMO;
    iommu_translate_iova(iommu, req, rsp);
    return;
}
int8_t
check_exp_pq_rec(iommu_t *iommu, uint32_t DID, uint32_t PID, uint8_t PV, uint8_t PRIV, uint8_t EXEC,
                 uint16_t reserved0, uint8_t reserved1, uint64_t PLOAD)
{
    page_rec_t page_rec;
    pqb_t pqb;
    pqh_t pqh;
    if ( read_register(iommu, PQH_OFFSET, 4) == read_register(iommu, PQT_OFFSET, 4) ) return -1;
    pqh.raw = read_register(iommu, PQH_OFFSET, 4);
    pqb.raw = read_register(iommu, PQB_OFFSET, 8);
    read_memory_test(((pqb.ppn * PAGESIZE) | (pqh.index * PQ_ENTRY_SZ)), PQ_ENTRY_SZ, (char *)&page_rec);
    if ( page_rec.DID != DID ) return -1;
    if ( page_rec.PID != PID ) return -1;
    if ( page_rec.PV != PV ) return -1;
    if ( page_rec.PRIV != PRIV ) return -1;
    if ( page_rec.EXEC != EXEC ) return -1;
    if ( page_rec.reserved0 != reserved0 ) return -1;
    if ( page_rec.reserved1 != reserved1 ) return -1;
    if ( page_rec.PAYLOAD != PLOAD ) return -1;
    write_register(iommu, PQH_OFFSET, 4, pqh.raw + 1);
    return 0;
}
int8_t
check_faults(
    iommu_t *iommu,
    uint16_t cause, uint8_t  exp_PV, uint32_t exp_PID, uint8_t  exp_PRIV,
    uint32_t exp_DID, uint64_t exp_iotval, uint8_t ttyp, uint64_t exp_iotval2) {
    fault_rec_t fault_rec;
    fqb_t fqb;
    fqh_t fqh;

    fqh.raw = read_register(iommu, FQH_OFFSET, 4);
    if ( (fqh.raw >= read_register(iommu, FQT_OFFSET, 4)) && (cause != 0) ) {
        printf("No faults logged\n");
        return -1;
    }
    if ( (fqh.raw < read_register(iommu, FQT_OFFSET, 4)) && (cause == 0) ) {
        printf("Unexpected fault logged\n");
        return -1;
    }

    fqb.raw = read_register(iommu, FQB_OFFSET, 8);
    read_memory_test(((fqb.ppn * PAGESIZE) | (fqh.index * FQ_ENTRY_SZ)), FQ_ENTRY_SZ, (char *)&fault_rec);

    // pop the fault record
    fqh.index++;
    write_register(iommu, FQH_OFFSET, 4, fqh.raw);

    if ( fault_rec.CAUSE != cause || fault_rec.DID != exp_DID ||
         fault_rec.iotval != exp_iotval ||
         fault_rec.iotval2 != exp_iotval2 ||
         fault_rec.TTYP != ttyp ||
         fault_rec.reserved != 0 ) {
        printf("Bad fault record\n");
        return -1;
    }
    if ( (exp_PV != fault_rec.PV) ||
         (exp_PV && ((fault_rec.PID != exp_PID) ||
         (fault_rec.PRIV != exp_PRIV))) ) {
        printf("Bad fault record\n");
        return -1;
    }
    return 0;
}
int8_t
check_rsp_and_faults(
    iommu_t *iommu,
    hb_to_iommu_req_t *req,
    iommu_to_hb_rsp_t *rsp,
    status_t status,
    uint16_t cause,
    uint64_t exp_iotval2) {

    fqh_t fqh;
    uint8_t EXP_TTYP;

    EXP_TTYP = TTYPE_NONE;
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.read_writeAMO == READ ) {
        if ( req->exec_req )
            EXP_TTYP = UNTRANSLATED_READ_FOR_EXECUTE_TRANSACTION;
        else
            EXP_TTYP = UNTRANSLATED_READ_TRANSACTION;
    }
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.read_writeAMO == WRITE )
        EXP_TTYP = UNTRANSLATED_WRITE_AMO_TRANSACTION;
    if ( req->tr.at == ADDR_TYPE_TRANSLATED && req->tr.read_writeAMO == READ ) {
        if ( req->pid_valid && req->exec_req )
            EXP_TTYP = TRANSLATED_READ_FOR_EXECUTE_TRANSACTION;
        else
            EXP_TTYP = TRANSLATED_READ_TRANSACTION;
    }
    if ( req->tr.at == ADDR_TYPE_TRANSLATED && req->tr.read_writeAMO == WRITE )
        EXP_TTYP = TRANSLATED_WRITE_AMO_TRANSACTION;
    if ( req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST )
        EXP_TTYP = PCIE_ATS_TRANSLATION_REQUEST;
    if ( rsp->status != status ) return -1;

    fqh.raw = read_register(iommu, FQH_OFFSET, 4);
    if ( (fqh.raw >= read_register(iommu, FQT_OFFSET, 4)) && (cause != 0) ) {
        printf("No faults logged\n");
        return -1;
    }
    if ( (fqh.raw < read_register(iommu, FQT_OFFSET, 4)) && (cause == 0) ) {
        printf("Unexpected fault logged\n");
        return -1;
    }

    if ( cause == 0 ) return 0;
    return check_faults(iommu, cause, req->pid_valid, req->process_id, req->priv_req,
                 req->device_id, req->tr.iova, EXP_TTYP, exp_iotval2);
}
uint64_t
add_device(iommu_t *iommu, uint32_t device_id, uint32_t gscid, uint8_t en_ats, uint8_t en_pri, uint8_t t2gpa,
           uint8_t dtf, uint8_t prpr,
           uint8_t gade, uint8_t sade, uint8_t dpe, uint8_t sbe, uint8_t sxl,
           uint8_t iohgatp_mode, uint8_t iosatp_mode, uint8_t pdt_mode,
           uint8_t msiptp_mode, uint8_t msiptp_pages, uint64_t msi_addr_mask,
           uint64_t msi_addr_pattern) {
    device_context_t DC;
    char zero[16384];
    memset(zero, 0, 16384);
    memset(&DC, 0, sizeof(DC));

    DC.tc.V      = 1;
    DC.tc.EN_ATS = en_ats;
    DC.tc.EN_PRI = en_pri;
    DC.tc.T2GPA  = t2gpa;
    DC.tc.DTF    = dtf;
    DC.tc.PRPR   = prpr;
    DC.tc.GADE   = gade;
    DC.tc.SADE   = sade;
    DC.tc.DPE    = dpe;
    DC.tc.SBE    = sbe;
    DC.tc.SXL    = sxl;
    if ( iohgatp_mode != IOHGATP_Bare ) {
        DC.iohgatp.GSCID = gscid;
        DC.iohgatp.PPN = get_free_ppn(4);
        write_memory_test(zero, DC.iohgatp.PPN * PAGESIZE, 16384);
    }
    DC.iohgatp.MODE = iohgatp_mode;
    if ( iosatp_mode != IOSATP_Bare ) {
        DC.tc.PDTV = 0;
        DC.fsc.iosatp.MODE = iosatp_mode;
        if ( DC.iohgatp.MODE != IOHGATP_Bare ) {
            gpte_t gpte;
            DC.fsc.iosatp.PPN = get_free_gppn(1, DC.iohgatp);
            gpte.raw = 0;
            gpte.V = 1;
            gpte.R = 1;
            gpte.W = 1;
            gpte.X = 0;
            gpte.U = 1;
            gpte.G = 0;
            gpte.A = 0;
            gpte.D = 0;
            gpte.PBMT = PMA;
            gpte.PPN = get_free_ppn(1);
            write_memory_test(zero, gpte.PPN * PAGESIZE, 4096);
            add_g_stage_pte(iommu, DC.iohgatp, (PAGESIZE * DC.fsc.iosatp.PPN), gpte, 0);
        } else {
            DC.fsc.iosatp.PPN = get_free_ppn(1);
            write_memory_test(zero, DC.fsc.iosatp.PPN * PAGESIZE, 4096);
        }
    }
    if ( pdt_mode != PDTP_Bare ) {
        DC.tc.PDTV = 1;
        DC.fsc.pdtp.MODE = pdt_mode;
        if ( DC.iohgatp.MODE != IOHGATP_Bare ) {
            gpte_t gpte;
            DC.fsc.pdtp.PPN = get_free_gppn(1, DC.iohgatp);
            gpte.raw = 0;
            gpte.V = 1;
            gpte.R = 1;
            gpte.W = 1;
            gpte.X = 0;
            gpte.U = 1;
            gpte.G = 0;
            gpte.A = 0;
            gpte.D = 0;
            gpte.PBMT = PMA;
            gpte.PPN = get_free_ppn(1);
            write_memory_test(zero, gpte.PPN * PAGESIZE, 4096);
            add_g_stage_pte(iommu, DC.iohgatp, (PAGESIZE * DC.fsc.pdtp.PPN), gpte, 0);
        } else {
            DC.fsc.pdtp.PPN = get_free_ppn(1);
            write_memory_test(zero, DC.fsc.pdtp.PPN * PAGESIZE, 4096);
        }
    }
    DC.msiptp.MODE = msiptp_mode;
    if ( msiptp_mode != MSIPTP_Off ) {
       DC.msiptp.PPN = get_free_ppn(msiptp_pages);
       write_memory_test(zero, DC.msiptp.PPN * PAGESIZE, 4096);
       DC.msi_addr_mask.mask = msi_addr_mask;
       DC.msi_addr_pattern.pattern = msi_addr_pattern;
    }
    return add_dev_context(iommu, &DC, device_id);
}
void
iotinval(
    iommu_t *iommu,
    uint8_t f3, uint8_t GV, uint8_t AV, uint8_t PSCV, uint32_t GSCID, uint32_t PSCID, uint64_t address) {
    command_t cmd;
    cqb_t cqb;
    cqt_t cqt;
    uint64_t temp, temp1;
    temp = access_viol_addr;
    temp1 = data_corruption_addr;
    access_viol_addr = -1;
    data_corruption_addr = -1;
    cmd.low = cmd.high = 0;
    cmd.iotinval.opcode = IOTINVAL;
    cmd.iotinval.func3 = f3;
    cmd.iotinval.gv = GV;
    cmd.iotinval.av = AV;
    cmd.iotinval.pscv = PSCV;
    cmd.iotinval.gscid = GSCID;
    cmd.iotinval.pscid = PSCID;
    cmd.iotinval.addr_63_12 = address / PAGESIZE;
    cqb.raw = read_register(iommu, CQB_OFFSET, 8);
    cqt.raw = read_register(iommu, CQT_OFFSET, 4);
    write_memory_test((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * CQ_ENTRY_SZ)), CQ_ENTRY_SZ);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(iommu, CQT_OFFSET, 4, cqt.raw);
    process_commands(iommu);
    return;
}
void
ats_command(
    iommu_t *iommu,
    uint8_t f3, uint8_t DSV, uint8_t PV, uint32_t PID, uint8_t DSEG, uint16_t RID, uint64_t payload) {
    command_t cmd;
    cqb_t cqb;
    cqt_t cqt;
    uint64_t temp, temp1;
    temp = access_viol_addr;
    temp1 = data_corruption_addr;
    access_viol_addr = -1;
    data_corruption_addr = -1;
    cmd.low = cmd.high = 0;
    cmd.ats.opcode = ATS;
    cmd.ats.func3 = f3;
    cmd.ats.rid = RID;
    cmd.ats.pv = PV;
    cmd.ats.pid = PID;
    cmd.ats.dsv = DSV;
    cmd.ats.dseg = DSEG;
    cmd.ats.payload = payload;

    cqb.raw = read_register(iommu, CQB_OFFSET, 8);
    cqt.raw = read_register(iommu, CQT_OFFSET, 4);
    write_memory_test((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * CQ_ENTRY_SZ)), CQ_ENTRY_SZ);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(iommu, CQT_OFFSET, 4, cqt.raw);
    process_commands(iommu);
    return;
}
void
generic_any(
    iommu_t *iommu,
    command_t cmd) {
    cqb_t cqb;
    cqt_t cqt;
    uint64_t temp, temp1;
    temp = access_viol_addr;
    temp1 = data_corruption_addr;
    cqb.raw = read_register(iommu, CQB_OFFSET, 8);
    cqt.raw = read_register(iommu, CQT_OFFSET, 4);
    write_memory_test((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * CQ_ENTRY_SZ)), CQ_ENTRY_SZ);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(iommu, CQT_OFFSET, 4, cqt.raw);
    process_commands(iommu);
    return;
}

void
iodir(
    iommu_t *iommu,
    uint8_t f3, uint8_t DV, uint32_t DID, uint32_t PID) {
    command_t cmd;
    cqb_t cqb;
    cqt_t cqt;
    uint64_t temp, temp1;
    temp = access_viol_addr;
    temp1 = data_corruption_addr;
    access_viol_addr = -1;
    data_corruption_addr = -1;
    cmd.low = cmd.high = 0;
    cmd.iodir.opcode = IODIR;
    cmd.iodir.func3 = f3;
    cmd.iodir.dv = DV;
    cmd.iodir.did = DID;
    cmd.iodir.pid = PID;
    cqb.raw = read_register(iommu, CQB_OFFSET, 8);
    cqt.raw = read_register(iommu, CQT_OFFSET, 4);
    write_memory_test((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * CQ_ENTRY_SZ)), CQ_ENTRY_SZ);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(iommu, CQT_OFFSET, 4, cqt.raw);
    process_commands(iommu);
    return;
}
void
iofence(
    iommu_t *iommu,
    uint8_t f3, uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WSI_bit, uint64_t addr, uint32_t data) {
    command_t cmd;
    cqb_t cqb;
    cqt_t cqt;
    uint64_t temp, temp1;
    temp = access_viol_addr;
    temp1 = data_corruption_addr;
    access_viol_addr = -1;
    data_corruption_addr = -1;
    cmd.low = cmd.high = 0;
    cmd.iofence.opcode = IOFENCE;
    cmd.iofence.func3 = f3;
    cmd.iofence.pr = PR;
    cmd.iofence.pw = PW;
    cmd.iofence.av = AV;
    cmd.iofence.wsi = WSI_bit;
    cmd.iofence.addr_63_2 = addr >> 2;
    cmd.iofence.data = data;
    cqb.raw = read_register(iommu, CQB_OFFSET, 8);
    cqt.raw = read_register(iommu, CQT_OFFSET, 4);
    write_memory_test((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * CQ_ENTRY_SZ)), CQ_ENTRY_SZ);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(iommu, CQT_OFFSET, 4, cqt.raw);
    process_commands(iommu);
    return;
}
uint64_t
get_free_ppn(
    uint64_t num_ppn) {
    uint64_t free_ppn;
    if ( next_free_page & (num_ppn -1) ) {
        next_free_page = next_free_page + (num_ppn -1);
        next_free_page = next_free_page & ~(num_ppn -1);
    }
    free_ppn = next_free_page;
    next_free_page += num_ppn;
    memset(&memory[free_ppn * PAGESIZE], 0, num_ppn * PAGESIZE);
    return free_ppn;
}
