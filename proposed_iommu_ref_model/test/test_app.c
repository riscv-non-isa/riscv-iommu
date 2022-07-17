// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include <stdio.h>
#include <inttypes.h>
#include "iommu.h"
#include "tables_api.h"
char *memory;
uint64_t physical_free_list;
int
main(void) {
    capabilities_t cap = {0};
    fctrl_t fctrl = {0};
    ddtp_t ddtp;
    device_context_t DC;
    gpte_t gpte;
    hb_to_iommu_req_t req; 
    iommu_to_hb_rsp_t rsp_msg;

    cap.version = 0x10;
    cap.Sv39 = cap.Sv48 = cap.Sv39x4 = cap.Sv48x4 = 1;
    cap.amo = cap.ats = cap.t2gpa = cap.hpm = cap.msi_flat = cap.msi_mrif = 1;
    cap.dbg = 1;
    cap.pas = 46;

    if ( reset_iommu(8, 40, 0xff, 4, Off, cap, fctrl) < 0 ) {
        printf("IOMMU reset failed\n");
    }
    cap.raw = read_register(CAPABILITIES_OFFSET, 8);
    printf("CAPABILITIES = %"PRIx64"\n", cap.raw);

    // Create memory
    memory = malloc((1 * 1024 * 1024 * 1024));
    memset(memory, 0, (1 * 1024 * 1024 * 1024));
    physical_free_list = 0;
    // Allocate a page for DDT root page
    ddtp.ppn = get_free_ppn(1);
    ddtp.iommu_mode = DDT_3LVL;
    write_register(DDTP_OFFSET, 8, ddtp.raw);
    printf("DDTP.PPN = %"PRIx64" DDTP MODE = %x\n", (uint64_t)ddtp.ppn, ddtp.iommu_mode);

    memset(&DC, 0, sizeof(DC));
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    DC.fsc.iosatp.MODE = IOSATP_Bare;
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.iohgatp.PPN = get_free_ppn(4);
    add_dev_context(&DC, 0x012345);
    printf("Adding DC for device 0x012345\n");
    printf("    DC.tc.EN_ATS       = %x\n", DC.tc.EN_ATS);
    printf("    DC.iohgatp.PPN     = %"PRIx64"\n", (uint64_t)DC.iohgatp.PPN);
    printf("    DC.iohgatp.MODE    = %x\n", DC.iohgatp.MODE);
    printf("    DC.fsc.iosatp.MODE = %x\n", DC.fsc.iosatp.MODE);

    gpte.raw = 0;
    gpte.V = 1;
    gpte.R = 1;
    gpte.W = 1;
    gpte.X = 1;
    gpte.U = 1;
    gpte.G = 0;
    gpte.A = 1;
    gpte.D = 1;
    gpte.PPN = get_free_ppn(512);
    gpte.PBMT = PMA;

    printf("Adding a GPA translation: GPA = 0x%x\n", (PAGESIZE * 512));
    printf("  SPA : %"PRIx64"\n", (uint64_t)gpte.PPN);
    printf("  R : %x\n", gpte.R);
    printf("  W : %x\n", gpte.W);
    printf("  C : %x\n", gpte.X);
    add_g_stage_pte(DC.iohgatp, (PAGESIZE * 512), gpte, 1);


    printf("Sending translation request for GP = 0x100000\n");
    req.device_id = 0x012345;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST;
    //req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.iova = (512 * PAGESIZE);
    req.tr.length = 64;
    req.tr.read_writeAMO = READ;

    iommu_translate_iova(req, &rsp_msg);
    printf("Translation received \n");
    printf("  Status     = %x \n", rsp_msg.status);
    printf("  PPN        = %"PRIx64"\n", rsp_msg.trsp.PPN);
    printf("  S          = %x\n", rsp_msg.trsp.S);
    printf("  N          = %x\n", rsp_msg.trsp.N);
    printf("  CXL_IO     = %x\n", rsp_msg.trsp.CXL_IO);
    printf("  Global     = %x\n", rsp_msg.trsp.Global);
    printf("  Priv       = %x\n", rsp_msg.trsp.Priv);
    printf("  U          = %x\n", rsp_msg.trsp.U);
    printf("  R          = %x\n", rsp_msg.trsp.R);
    printf("  W          = %x\n", rsp_msg.trsp.W);
    printf("  Exe        = %x\n", rsp_msg.trsp.Exe);
    printf("  AMA        = %x\n", rsp_msg.trsp.AMA);
    printf("  PBMT       = %x\n", rsp_msg.trsp.PBMT);
    printf("  is_msi     = %x\n", rsp_msg.trsp.is_msi);
    printf("  is_mrif_wr = %x\n", rsp_msg.trsp.is_mrif_wr);
    printf("  mrif_nid   = %x\n", rsp_msg.trsp.mrif_nid);

    tr_req_iova_t tr_req_iova;
    tr_req_ctrl_t tr_req_ctrl;
    tr_response_t tr_response;
    tr_req_iova.raw = (512 * PAGESIZE);
    tr_req_ctrl.DID = 0x012345;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    printf("Translation received \n");
    printf("  Status     = %x \n", tr_response.fault);
    printf("  PPN        = %"PRIx64"\n", (uint64_t)tr_response.PPN);
    printf("  S          = %x\n", tr_response.S);
    printf("  PBMT       = %x\n", tr_response.PBMT);
    tr_req_ctrl.raw = read_register(TR_REQ_CTRL_OFFSET, 8);
    printf("  busy       = %x\n", tr_req_ctrl.go_busy);



    return 0;
}
uint64_t
get_free_ppn(uint16_t num_ppn) {
    uint64_t free_ppn;
    if ( physical_free_list & (num_ppn -1) ) {
        physical_free_list = physical_free_list + (num_ppn -1);
        physical_free_list = physical_free_list & ~(num_ppn -1);
    }
    free_ppn = physical_free_list;
    physical_free_list += num_ppn;
    return free_ppn; 
}
uint8_t read_memory(uint64_t addr, uint8_t size, char *data){
    memcpy(data, &memory[addr], size);
    return 0;
}
uint8_t read_memory_for_AMO(uint64_t address, uint8_t size, char *data) {
    // Same for now
    return read_memory(address, size, data);
}
uint8_t write_memory(char *data, uint64_t address, uint8_t size) {
    memcpy(&memory[address], data, size);
    return 0;
}    
void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW){}
void send_msg_iommu_to_hb(ats_msg_t *prgr){}
