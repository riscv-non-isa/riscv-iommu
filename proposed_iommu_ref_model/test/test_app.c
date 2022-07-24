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
uint64_t guest_physical_free_list;
int
main(void) {
    capabilities_t cap = {0};
    fctrl_t fctrl = {0};
    ddtp_t ddtp;
    device_context_t DC;
    gpte_t gpte;
    pte_t pte;
    hb_to_iommu_req_t req; 
    iommu_to_hb_rsp_t rsp_msg;
    uint64_t SPA;

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
    guest_physical_free_list = 0;
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
    print_dev_context(&DC, 0x12345);

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

    printf("Adding a GPA translation: GPA = 0x%lx\n", (PAGESIZE * 512));
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

//////////////////////////
    memset(&DC, 0, sizeof(DC));
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    DC.fsc.iosatp.MODE = IOSATP_Sv48;
    DC.fsc.iosatp.PPN = get_free_ppn(1);
    DC.iohgatp.MODE = IOHGATP_Bare;
    add_dev_context(&DC, 0x012346);
    print_dev_context(&DC, 0x12346);

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 1;
    pte.D = 1;
    pte.PPN = get_free_ppn(512);
    pte.PBMT = PMA;

    printf("Adding a VA translation: VA = 0x%lx\n", (PAGESIZE * 512));
    printf("  SPA : %"PRIx64"\n", (uint64_t)pte.PPN);
    printf("  R : %x\n", pte.R);
    printf("  W : %x\n", pte.W);
    printf("  C : %x\n", pte.X);
    add_s_stage_pte(DC.fsc.iosatp, (PAGESIZE * 512), pte, 1);


    printf("Sending translation request for VA = 0x100000\n");
    req.device_id = 0x012346;
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

    tr_req_iova.raw = (512 * PAGESIZE);
    tr_req_ctrl.DID = 0x012346;
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

//----------------------------------------
    memset(&DC, 0, sizeof(DC));
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.iohgatp.PPN = get_free_ppn(4);
    DC.fsc.iosatp.MODE = IOSATP_Sv48;
    DC.fsc.iosatp.PPN = get_free_gppn(1, 1, DC.iohgatp);

    add_dev_context(&DC, 0x012347);
    printf("Adding DC for device 0x012347\n");
    print_dev_context(&DC, 0x12347);

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 0;
    pte.X = 0;
    pte.U = 1;
    pte.G = 0;
    pte.A = 1;
    pte.D = 1;
    pte.PPN = get_free_gppn(512, 1, DC.iohgatp);
    pte.PBMT = PMA;

    translate_gpa(DC.iohgatp, (pte.PPN * PAGESIZE), &SPA);
    printf("Adding a GVA translation: VA = 0x%lx\n", (PAGESIZE * 512));
    printf("  GPA : %"PRIx64"\n", (uint64_t)(pte.PPN * PAGESIZE));
    printf("  SPA : %"PRIx64"\n", (uint64_t)SPA);
    printf("  R : %x\n", pte.R);
    printf("  W : %x\n", pte.W);
    printf("  C : %x\n", pte.X);
    add_vs_stage_pte(DC.fsc.iosatp, (PAGESIZE * 512), pte, 1, DC.iohgatp);


    printf("Sending translation request for VA = 0x100000\n");
    req.device_id = 0x012347;
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

    tr_req_iova.raw = (512 * PAGESIZE);
    tr_req_ctrl.DID = 0x012347;
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

//++++++++++++++++++++++++++++++++++++++++++++
    memset(&DC, 0, sizeof(DC));
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    DC.tc.PDTV = 1;
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.iohgatp.PPN = get_free_ppn(4);
    DC.fsc.pdtp.MODE = PD20;
    DC.fsc.pdtp.PPN = get_free_gppn(1, 1, DC.iohgatp);

    add_dev_context(&DC, 0x012348);
    print_dev_context(&DC, 0x12348);

    process_context_t PC; 
    PC.fsc.iosatp.MODE = IOSATP_Sv48;
    PC.fsc.iosatp.PPN = get_free_gppn(1, 1, DC.iohgatp);
    PC.fsc.iosatp.reserved = 0;
    PC.ta.V = 1;
    PC.ta.ENS = 1;
    PC.ta.SUM = 0;
    PC.ta.reserved = 0;
    PC.ta.PSCID = 99;
    add_process_context(&DC, &PC, 0x0099);
    print_process_context(&PC, 0x012348, 0x0099);

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 0;
    pte.X = 0;
    pte.U = 1;
    pte.G = 0;
    pte.A = 1;
    pte.D = 1;
    pte.PPN = get_free_gppn(512, 1, DC.iohgatp);
    pte.PBMT = PMA;

    translate_gpa(DC.iohgatp, (pte.PPN * PAGESIZE), &SPA);
    printf("Adding a GVA translation: VA = 0x%lx\n", (PAGESIZE * 512));
    printf("  GPA : %"PRIx64"\n", (uint64_t)(pte.PPN * PAGESIZE));
    printf("  SPA : %"PRIx64"\n", (uint64_t)SPA);
    printf("  R : %x\n", pte.R);
    printf("  W : %x\n", pte.W);
    printf("  C : %x\n", pte.X);
    add_vs_stage_pte(PC.fsc.iosatp, (PAGESIZE * 512), pte, 1, DC.iohgatp);


    printf("Sending translation request for VA = 0x100000\n");
    req.device_id = 0x012348;
    req.pid_valid = 1;
    req.process_id = 0x99;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST;
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

    tr_req_iova.raw = (512 * PAGESIZE);
    tr_req_ctrl.DID = 0x012348;
    tr_req_ctrl.PV = 1;
    tr_req_ctrl.PID = 0x99;
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
get_free_gppn(uint16_t num_gppn, uint8_t map, iohgatp_t iohgatp) {
    uint64_t free_gppn;
    gpte_t gpte;
    uint8_t add_level;

    if ( guest_physical_free_list & (num_gppn -1) ) {
        guest_physical_free_list = guest_physical_free_list + (num_gppn -1);
        guest_physical_free_list = guest_physical_free_list & ~(num_gppn -1);
    }
    free_gppn = guest_physical_free_list;
    guest_physical_free_list += num_gppn;
    if ( map == 1 ) {
        if ( num_gppn == 1 ) {
            add_level = 0;
        }
        if ( num_gppn == 512 ) {
            add_level = 1;
        }
        if ( num_gppn == 512 * 512 ) {
            add_level = 2;
        }
        if ( num_gppn == 512 * 512 * 512 ) {
            add_level = 3;
        }
        gpte.raw = 0;
        gpte.V = 1;
        gpte.R = 1;
        gpte.W = 1;
        gpte.X = 1;
        gpte.U = 1;
        gpte.G = 0;
        gpte.A = 1;
        gpte.D = 1;
        gpte.PPN = get_free_ppn(num_gppn);
        gpte.PBMT = PMA;
        add_g_stage_pte(iohgatp, (PAGESIZE * free_gppn), gpte, add_level);
    }
    return free_gppn; 
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
