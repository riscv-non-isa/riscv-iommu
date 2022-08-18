// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include <stdio.h>
#include <inttypes.h>
#include "iommu.h"
#include "tables_api.h"
char *memory;
uint64_t next_free_page;
uint64_t next_free_gpage[65536];
int8_t reset_system(uint8_t mem_gb, uint16_t num_vms);
int8_t enable_cq(uint32_t nppn);
int8_t enable_fq(uint32_t nppn);
int8_t enable_pq(uint32_t nppn);
int8_t enable_iommu(uint8_t iommu_mode);
void iodir(uint8_t f3, uint8_t DV, uint32_t DID, uint32_t PID);
void iotinval( uint8_t f3, uint8_t GV, uint8_t AV, uint8_t PSCV, uint32_t GSCID, uint32_t PSCID, uint64_t address);
void iofence(uint8_t f3, uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WIS_bit, uint64_t addr, uint32_t data);
void send_translation_request(uint32_t did, uint8_t pid_valid, uint32_t pid, uint8_t no_write,
             uint8_t exec_req, uint8_t priv_req, uint8_t is_cxl_dev, addr_type_t at, uint64_t iova,
             uint32_t length, uint8_t read_writeAMO, uint32_t msi_wr_data,
             hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp);
int8_t check_rsp_and_faults(hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp, status_t status,
          uint16_t cause, uint64_t exp_iotval2);
uint64_t get_free_gppn(uint64_t num_gppn, iohgatp_t iohgatp);
uint64_t access_viol_addr = -1;
uint64_t data_corruption_addr = -1;
uint8_t pr_go_requested = 0;
uint8_t pw_go_requested = 0;
#define FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, code)\
    for ( at = 0; at < 3; at++ ) {\
        for ( pid_valid = 0; pid_valid < 2; pid_valid++ ) {\
            for ( exec_req = 0; exec_req < 2; exec_req++ ) {\
                for ( priv_req = 0; priv_req < 2; priv_req++ ) {\
                    for ( no_write = 0; no_write < 2; no_write++ )  {\
                        code\
                    }\
                }\
            }\
        }\
    }
uint64_t add_device(uint32_t device_id, uint32_t gscid, uint8_t en_ats, uint8_t en_pri, uint8_t t2gpa, 
           uint8_t dtf, uint8_t prpr, uint8_t iohgatp_mode, uint8_t iosatp_mode, uint8_t pdt_mode,
           uint8_t msiptp_mode, uint8_t msiptp_pages, uint64_t msi_addr_mask, 
           uint64_t msi_addr_pattern);
int
main(void) {
    capabilities_t cap = {0};
    fctrl_t fctrl = {0};
    uint8_t at, pid_valid, exec_req, priv_req, no_write, PR, PW, AV;
    uint32_t i, j;
    uint64_t DC_addr, exp_iotval2, iofence_PPN, iofence_data, gpa, temp, gpte_addr;
    device_context_t DC;
    ddte_t ddte;
    ddtp_t ddtp;
    gpte_t gpte;
    fqcsr_t fqcsr;
    cqcsr_t cqcsr;
    cqb_t cqb;
    cqt_t cqt;
    cqh_t cqh;
    command_t cmd;
    hb_to_iommu_req_t req; 
    iommu_to_hb_rsp_t rsp;
    tr_req_iova_t tr_req_iova;
    tr_req_ctrl_t tr_req_ctrl;
    tr_response_t tr_response;

    // reset system
    if ( reset_system(1, 2) < 0 ) return -1;

    // Reset the IOMMU
    cap.version = 0x10;
    cap.Sv39 = cap.Sv48 = cap.Sv57 = cap.Sv39x4 = cap.Sv48x4 = cap.Sv57x4 = 1;
    cap.amo = cap.ats = cap.t2gpa = cap.hpm = cap.msi_flat = cap.msi_mrif = 1;
    cap.dbg = 1;
    cap.pas = 50;
    if ( reset_iommu(8, 40, 0xff, 4, Off, cap, fctrl) < 0 ) return -1;

    // When Fault queue is not enabled, no logging should occur
    pid_valid = exec_req = priv_req = no_write = 1;
    at = 0;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( ((read_register(FQH_OFFSET, 4)) != read_register(FQT_OFFSET, 4)) ) return -1;
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    if ( fqcsr.fqof != 0 ) return -1;

    // Enable command queue
    if ( enable_cq(4) < 0 ) return -1;
    // Enable fault queue
    if ( enable_fq(4) < 0 ) return -1;
    // Enable page queue
    if ( enable_pq(4) < 0 ) return -1;

    printf("Test 1: All inbound transactions disallowed: ");
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) return -1;
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 256, 0) < 0 ) return -1;
        }
    });
    printf("PASS\n");

    // Enable IOMMU
    if ( enable_iommu(DDT_3LVL) < 0 ) return -1;

    printf("Test 2: Non-leaf DDTE invalid: ");
    // make DDTE invalid
    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddte.raw  = 0;
    write_memory((char *)&ddte, (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8), 8);
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) return -1;
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) return -1;
        }
    });
    printf("PASS\n");

    printf("Test 8: IOMMU NL-DDT access viol and data corruption:");
    iodir(INVAL_DDT, 1, 0x012345, 0);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    access_viol_addr = (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 257, 0) < 0 ) return -1;
    access_viol_addr = -1;
    data_corruption_addr = (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 268, 0) < 0 ) return -1;
    data_corruption_addr = -1;
    printf("PASS\n");

    printf("Test 3: Non-leaf DDTE reserved bits: ");
    // Set reserved bits in ddte
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        ddtp.raw = read_register(DDTP_OFFSET, 8);
        ddte.raw  = 0;
        ddte.reserved0 |= no_write;
        ddte.reserved1 |= ~no_write;
        ddte.V = 1;
        write_memory((char *)&ddte, (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8), 8);
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) return -1;
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
        }
    });
    printf("PASS\n");

    ddte.raw  = 0;
    write_memory((char *)&ddte, (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8), 8);

    printf("Test 4: Fault queue overflow : ");
    // Trigger a fault queue overflow
    // The queue should be empty now
    if ( (read_register(FQH_OFFSET, 4) != read_register(FQT_OFFSET, 4)) ) return -1;
    pid_valid = exec_req = priv_req = no_write = 1;
    at = 0;
    for ( i = 0; i < 1023; i++ ) {
        send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                 priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    }
    // The queue should be be full
    if ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) return -1;
    // No overflow should be set
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    if ( fqcsr.fqof == 1 ) return -1;
    // Next fault should cause overflow
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) return -1;
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    if ( fqcsr.fqof == 0 ) return -1;
    // Overflow should remain
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) return -1;
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    if ( fqcsr.fqof == 0 ) return -1;
    // Drain the fault queue, clear fqof
    write_register(FQH_OFFSET, 4, read_register(FQT_OFFSET, 4));
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    if ( fqcsr.fqof != 0 ) return -1;
    printf("PASS\n");

    // Add a device 0x012345 to guest with GSCID=1
    DC_addr = add_device(0x012345, 1, 0, 0, 0, 0, 0, IOHGATP_Sv48x4, IOSATP_Bare, PDTP_Bare,
                         MSIPTP_Flat, 1, 0xFFFFFFFFFF, 0x1000000000);
    (void)(DC_addr);

    printf("Test 5: Device context invalid: ");
    // make DC invalid
    read_memory(DC_addr, 64, (char *)&DC);
    DC.tc.V = 0;
    write_memory((char *)&DC, DC_addr, 64);
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) return -1;
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) return -1;
        }
    });
    DC.tc.V = 1;
    write_memory((char *)&DC, DC_addr, 64);
    printf("PASS\n");

    printf("Test 6: Device context misconfigured: ");
    read_memory(DC_addr, 64, (char *)&DC);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    DC.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.reserved = 0;
    DC.tc.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.tc.reserved = 0;
    DC.ta.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.ta.reserved = 0;
    DC.fsc.iosatp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.fsc.iosatp.reserved = 0;
    DC.msiptp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.msiptp.reserved = 0;
    DC.msi_addr_mask.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.msi_addr_mask.reserved = 0;
    DC.msi_addr_pattern.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.msi_addr_pattern.reserved = 0;
    DC.iohgatp.MODE = IOHGATP_Sv32x4;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.iohgatp.MODE = IOHGATP_Sv57x4 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.fsc.iosatp.MODE = IOSATP_Sv32;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.fsc.iosatp.MODE = IOSATP_Bare;
    DC.fsc.iosatp.MODE = IOSATP_Sv57 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.fsc.iosatp.MODE = IOSATP_Bare;
    DC.msiptp.MODE = MSIPTP_Flat + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.msiptp.MODE = MSIPTP_Bare;
    DC.tc.T2GPA = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.tc.T2GPA = 0;
    DC.tc.EN_PRI = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.tc.EN_PRI = 0;
    DC.tc.PRPR = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.tc.PRPR = 0;
    DC.tc.PDTV = 1;
    DC.fsc.pdtp.MODE = PD8 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.fsc.pdtp.MODE = PDTP_Bare;
    DC.fsc.pdtp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) return -1;
    DC.fsc.pdtp.reserved = 0;
    DC.tc.PDTV = 0;
    write_memory((char *)&DC, DC_addr, 64);
    printf("PASS\n");

    printf("Test 6: Unsupported transaction type :");
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) return -1;
        } else if ( at == ADDR_TYPE_TRANSLATED ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 260, 0) < 0 ) return -1;
        } else {
            uint16_t exp_cause;
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), 0, &req, &rsp);
            if ( pid_valid == 1 ) exp_cause = 260;
            else if ( (no_write ^ 1) == WRITE ) exp_cause = 23;
            else exp_cause = 21;
            exp_iotval2 = ( exp_cause != 260) ? 0xdeadbeec : 0;
            // iotval2 reports the gpa i.e. the iova
            if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, exp_cause, exp_iotval2) < 0 )
                return -1;
        }
    });
    printf("PASS\n");


    printf("Test 7: IOMMU device context access viol and data corruption:");
    iodir(INVAL_DDT, 1, 0x012345, 0);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    access_viol_addr = DC_addr;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 257, 0) < 0 ) return -1;
    access_viol_addr = -1;
    data_corruption_addr = DC_addr;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 268, 0) < 0 ) return -1;
    data_corruption_addr = -1;
    printf("PASS\n");


    printf("Test 7: IOMMU device context invalidation:");
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    // Get the device context cached
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 0xdeadbeec) < 0 ) return -1;
    // Update memory to mark invalid
    DC.tc.V = 0;
    write_memory((char *)&DC, DC_addr, 64);
    // Cached copy should apply
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 0xdeadbeec) < 0 ) return -1;
    iodir(INVAL_DDT, 1, 0x012345, 0);
    if ( read_register(CQH_OFFSET, 4) != read_register(CQT_OFFSET, 4) ) return -1;
    // Memory copy should apply
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), 0, &req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) return -1;
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    write_memory((char *)&DC, DC_addr, 64);
    printf("PASS\n");

    printf("Test 8: Test IOFENCE:");
    iofence_PPN = get_free_ppn(1);
    for ( PR = 0; PR < 2; PR++ ) {
        for ( PW = 0; PW < 2; PW++ ) {
            for ( AV = 0; AV < 2; AV++ ) {
                iofence_data = 0x1234567812345678;
                write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);
                pr_go_requested = 0;
                pw_go_requested = 0;
                iofence(IOFENCE_C, PR, PW, AV, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
                read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
                if ( AV == 1 && iofence_data != 0x12345678DEADBEEF )  return -1;
                if ( AV == 0 && iofence_data != 0x1234567812345678 )  return -1;
                if ( PR != pr_go_requested ) return -1;
                if ( PW != pw_go_requested ) return -1;
            }
        }
    }
    // Illegal Func3
    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    iofence(IOFENCE_C+1, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    if ( cqcsr.cmd_ill != 1 ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( 0 != pr_go_requested ) return -1;

    // Queue another - since illegal is set, head should not move
    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    if ( (read_register(CQH_OFFSET, 4) + 2) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( 0 != pr_go_requested ) return -1;

    // fix the illegal commend 
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqh.raw = read_register(CQH_OFFSET, 4);
    read_memory(((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16, (char *)&cmd);
    cmd.iofence.func3 = IOFENCE_C;
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16);

    // Clear the illegal
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    if ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEEF )  return -1;
    if ( 1 != pr_go_requested ) return -1;

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);
    process_commands();
    if ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEEF )  return -1;
    if ( 1 != pr_go_requested ) return -1;

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);



    // Set WIS - not supported in this config
    iofence(IOFENCE_C, 1, 0, 1, 1, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    if ( cqcsr.cmd_ill != 1 ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( 0 != pr_go_requested ) return -1;
    if ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) return -1;
    // Clear the illegal
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    // fix the illegal commend 
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqh.raw = read_register(CQH_OFFSET, 4);
    read_memory(((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16, (char *)&cmd);
    cmd.iofence.wis = 0;
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16);
    process_commands();
    if ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEEF )  return -1;
    if ( 1 != pr_go_requested ) return -1;

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    // Cause command queue memory fault
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqt.raw = read_register(CQT_OFFSET, 4);
    access_viol_addr = ((cqb.ppn * PAGESIZE) | (cqt.index * 16));

    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    if ( cqcsr.cqmf != 1 ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( 0 != pr_go_requested ) return -1;
    if ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) return -1;

    // Queue another - since cqmf is set, head should not move
    iofence(IOFENCE_C, 0, 1, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE2);
    if ( (read_register(CQH_OFFSET, 4) + 2) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( 0 != pr_go_requested ) return -1;
    // Clear the cqmf
    access_viol_addr = -1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    if ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEE1 )  return -1;
    if ( 1 != pr_go_requested ) return -1;
    if ( 0 != pw_go_requested ) return -1;
    process_commands();
    if ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEE2 )  return -1;
    if ( 0 != pr_go_requested ) return -1;
    if ( 1 != pw_go_requested ) return -1;

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    // Cause memory fault on completion buffer
    access_viol_addr = iofence_PPN * PAGESIZE;

    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    if ( cqcsr.cqmf != 1 ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x1234567812345678 )  return -1;
    if ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) return -1;

    // Clear the cqmf
    access_viol_addr = -1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    if ( (read_register(CQH_OFFSET, 4) ) != read_register(CQT_OFFSET, 4) ) return -1;
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    if ( iofence_data != 0x12345678DEADBEE1 )  return -1;

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    printf("PASS\n");

    printf("Test 8: Test G-stage translation sizes:");
    req.device_id = 0x012345;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST;
    req.tr.length = 64;
    req.tr.read_writeAMO = WRITE;
    gpte.raw = 0;
    gpte.V = 1;
    gpte.R = 1;
    gpte.W = 1;
    gpte.X = 1;
    gpte.U = 1;
    gpte.G = 0;
    gpte.A = 0;
    gpte.D = 0;
    gpte.PBMT = PMA;
    for ( j = 0; j < 3; j++ ) {
        if ( j == 2 ) {
            DC.iohgatp.MODE = IOHGATP_Sv57x4;
            gpa = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
            gpa = gpa * 8;
        } else if ( j == 1 ) {
            DC.iohgatp.MODE = IOHGATP_Sv48x4;
            gpa = 512UL * 512UL * 512UL * PAGESIZE;
            gpa = gpa * 4;
        } else {
            DC.iohgatp.MODE = IOHGATP_Sv39x4;
            gpa = 512UL * 512UL * PAGESIZE;
        }
        write_memory((char *)&DC, DC_addr, 64);
        iodir(INVAL_DDT, 1, 0x012345, 0);
        for ( i = 0; i < 5; i++ ) {
            if ( (i == 4) && DC.iohgatp.MODE != IOHGATP_Sv57x4 ) continue;
            if ( (i == 3) && DC.iohgatp.MODE != IOHGATP_Sv48x4 && 
                             DC.iohgatp.MODE != IOHGATP_Sv57x4 ) continue;
            gpa = gpa | ((1 << (i * 9)) * PAGESIZE) | 2048;
            req.tr.iova = gpa;
            gpte.PPN = 512UL * 512UL * 512UL * 512UL;
            gpte.PPN |= (1UL << (i * 9UL));
            add_g_stage_pte(DC.iohgatp, gpa, gpte, i);
            iommu_translate_iova(&req, &rsp);
            if ( rsp.status != SUCCESS ) return -1; 
            if ( rsp.trsp.S == 1 && i == 0 ) return -1; 
            if ( rsp.trsp.S == 0 && i != 0 ) return -1; 
            if ( rsp.trsp.U != 0 ) return -1;
            if ( rsp.trsp.R != 1 ) return -1;
            if ( rsp.trsp.W != 1 ) return -1;
            if ( rsp.trsp.Exe != 1 ) return -1;
            if ( rsp.trsp.PBMT != PMA ) return -1;
            if ( rsp.trsp.is_msi != 0 ) return -1;
            if ( rsp.trsp.S == 1 )  {
                temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
                temp = temp  * PAGESIZE | 0xFFF;
            } else {
                temp = 0xFFF;
            }
            if ( ((rsp.trsp.PPN * PAGESIZE) & ~temp) != (gpte.PPN * PAGESIZE) ) return -1;
            if ( ((temp + 1) != PAGESIZE) && i == 0 ) return -1; 
            if ( ((temp + 1) != 512UL * PAGESIZE) && i == 1 ) return -1; 
            if ( ((temp + 1) != 512UL * 512UL * PAGESIZE) && i == 2 ) return -1; 
            if ( ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) && i == 3 ) return -1; 
        }
    }
    g_reg_file.capabilities.Sv57x4 = 0;
    g_reg_file.capabilities.Sv48x4 = 0;
    g_reg_file.capabilities.Sv39x4 = 0;
    g_reg_file.capabilities.Sv32x4 = 0;
    for ( i = 0; i < 4; i++ ) {
        if ( i == 0 ) g_reg_file.capabilities.Sv32x4 = 1;
        if ( i == 1 ) g_reg_file.capabilities.Sv39x4 = 1;
        if ( i == 2 ) g_reg_file.capabilities.Sv48x4 = 1;
        if ( i == 3 ) g_reg_file.capabilities.Sv57x4 = 1;
        DC.iohgatp.MODE = IOHGATP_Bare;
        write_memory((char *)&DC, DC_addr, 64);
        iodir(INVAL_DDT, 1, 0x012345, 0);
        gpa = 512UL * 512UL * PAGESIZE;
        req.tr.iova = gpa;
        iommu_translate_iova(&req, &rsp);
        if ( rsp.status != SUCCESS ) return -1; 
        if ( rsp.trsp.U != 0 ) return -1;
        if ( rsp.trsp.R != 1 ) return -1;
        if ( rsp.trsp.W != 1 ) return -1;
        if ( rsp.trsp.Exe != 1 ) return -1;
        if ( rsp.trsp.PBMT != PMA ) return -1;
        if ( rsp.trsp.is_msi != 0 ) return -1;
        if ( rsp.trsp.S != 1 ) return -1;
        temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
        temp = temp  * PAGESIZE | 0xFFF;
        if ( i == 0 && ((temp + 1) != 2 * 512UL * PAGESIZE) ) return -1; 
        if ( i == 1 && ((temp + 1) != 512UL * 512UL * PAGESIZE) ) return -1; 
        if ( i == 2 && ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) ) return -1; 
        if ( i == 3 && ((temp + 1) != 512UL * 512UL * 512UL * 512UL * PAGESIZE) ) return -1; 
    }
    printf("PASS\n");

    printf("Test 9: Test G-stage permission faults:");
    req.device_id = 0x012345;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.length = 64;
    req.tr.read_writeAMO = WRITE;
    gpte.raw = 0;
    gpte.V = 1;
    gpte.R = 1;
    gpte.W = 1;
    gpte.X = 1;
    gpte.U = 1;
    gpte.G = 0;
    gpte.A = 0;
    gpte.D = 0;
    gpte.PBMT = PMA;
    DC.iohgatp.MODE = IOHGATP_Sv57x4;
    gpa = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
    gpa = gpa * 16;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012345, 0);
    gpa = gpa | ((1 << (i * 9)) * PAGESIZE) | 2048;
    req.tr.iova = gpa;
    gpte.PPN = 512UL * 512UL * 512UL * 512UL;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, i);
    read_memory(gpte_addr, 8, (char *)&gpte);

    gpte.U = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) return -1;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) return -1;

    gpte.U = 1;
    gpte.W = 1;
    gpte.R = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) return -1;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) return -1;

    gpte.X = 1;
    gpte.W = 0;
    gpte.R = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) return -1;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) return -1;

    gpte.PPN = 512UL * 512UL * 512UL ;
    gpte.X = 1;
    gpte.W = 1;
    gpte.R = 1;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) return -1;
    gpte.PPN = 512UL * 512UL * 512UL * 512UL;
    write_memory((char *)&gpte, gpte_addr, 8);

    access_viol_addr = gpte_addr;
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 7, 0) < 0 ) return -1;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) return -1;

    tr_req_ctrl.DID = 0x012345;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    tr_req_iova.raw = req.tr.iova;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    if ( tr_response.fault == 0 ) return -1;
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) return -1;

    access_viol_addr = -1;

    printf("PASS\n");

    printf("Test 9: Test IOTINVAL.GVMA:");

    req.device_id = 0x012345;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.length = 64;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    if ( rsp.status != SUCCESS ) return -1; 
    gpte.PPN = 512UL * 512UL * 512UL ;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    if ( rsp.status != SUCCESS ) return -1; 
    iotinval(GVMA, 1, 0, 0, 1, 0, 0);
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) return -1;
    gpte.PPN = 512UL * 512UL * 512UL * 512UL;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    if ( rsp.status != SUCCESS ) return -1; 
    iotinval(GVMA, 1, 1, 0, 1, 0, req.tr.iova);

    gpte.W = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    if ( rsp.status != SUCCESS ) return -1; 
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    if ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) return -1;
    gpte.W = 1;
    write_memory((char *)&gpte, gpte_addr, 8);
    iotinval(GVMA, 1, 1, 0, 1, 0, req.tr.iova);
    iommu_translate_iova(&req, &rsp);
    if ( rsp.status != SUCCESS ) return -1; 



    printf("PASS\n");





#if 0
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
    pte.X = 1;
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
    req.priv_req = 1;
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
    pte.U = 0;
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
    req.priv_req = 1;
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
    tr_req_ctrl.Priv = 1;
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

#endif
    return 0;
}
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
int8_t enable_cq(
    uint32_t nppn) {
    cqb_t cqb;
    cqcsr_t cqcsr;

    cqb.raw = 0;
    cqb.ppn = get_free_ppn(nppn);
    cqb.log2szm1 = 9;
    write_register(CQB_OFFSET, 8, cqb.raw);
    do {
        cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    } while ( cqcsr.busy == 1 );
    cqcsr.raw = 0;
    cqcsr.cie = 1;
    cqcsr.cqen = 1;
    cqcsr.cqmf = 1;
    cqcsr.cmd_to = 1;
    cqcsr.cmd_ill = 1;
    cqcsr.fence_w_ip = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    do {
        cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    } while ( cqcsr.busy == 1 );
    if ( cqcsr.cqon != 1 ) {
        printf("CQ enable failed\n");
        return -1;
    }
    return 0;
}

int8_t enable_fq(
    uint32_t nppn) {
    fqb_t fqb;
    fqcsr_t fqcsr;

    fqb.raw = 0;
    fqb.ppn = get_free_ppn(nppn);
    fqb.log2szm1 = 9;
    write_register(FQB_OFFSET, 8, fqb.raw);
    do {
        fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    } while ( fqcsr.busy == 1 );
    fqcsr.raw = 0;
    fqcsr.fie = 1;
    fqcsr.fqen = 1;
    fqcsr.fqmf = 1;
    fqcsr.fqof = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    do {
        fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    } while ( fqcsr.busy == 1 );
    if ( fqcsr.fqon != 1 ) {
        printf("FQ enable failed\n");
        return -1;
    }
    return 0;
}

int8_t enable_pq(
    uint32_t nppn) {
    pqb_t pqb;
    pqcsr_t pqcsr;

    pqb.raw = 0;
    pqb.ppn = get_free_ppn(4);
    pqb.log2szm1 = 9;
    write_register(PQB_OFFSET, 8, pqb.raw);
    do {
        pqcsr.raw = read_register(PQCSR_OFFSET, 4);
    } while ( pqcsr.busy == 1 );
    pqcsr.raw = 0;
    pqcsr.pie = 1;
    pqcsr.pqen = 1;
    pqcsr.pqmf = 1;
    pqcsr.pqof = 1;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    do {
        pqcsr.raw = read_register(PQCSR_OFFSET, 4);
    } while ( pqcsr.busy == 1 );
    if ( pqcsr.pqon != 1 ) {
        printf("PQ enable failed\n");
        return -1;
    }
    return 0;
}
int8_t
enable_iommu(
    uint8_t iommu_mode) {
    ddtp_t ddtp;
    uint32_t i;
    uint64_t zero = 0;

    // Allocate a page for DDT root page
    do {
        ddtp.raw = read_register(DDTP_OFFSET, 8);
    } while ( ddtp.busy == 1 );

    ddtp.raw = 0;
    ddtp.ppn = get_free_ppn(1);
    // Clear the page
    for ( i = 0; i < 512; i++ ) 
        write_memory((char *)&zero, (ddtp.ppn * PAGESIZE) | (i * 8), 8);

    ddtp.iommu_mode = iommu_mode;
    write_register(DDTP_OFFSET, 8, ddtp.raw);
    do {
        ddtp.raw = read_register(DDTP_OFFSET, 8);
    } while ( ddtp.busy == 1 );
    return 0;
}
void 
send_translation_request(uint32_t did, uint8_t pid_valid, uint32_t pid, uint8_t no_write,
    uint8_t exec_req, uint8_t priv_req, uint8_t is_cxl_dev, addr_type_t at, uint64_t iova,
    uint32_t length, uint8_t read_writeAMO, uint32_t msi_wr_data,
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
    req->tr.msi_wr_data   = msi_wr_data;
    iommu_translate_iova(req, rsp);
    return;
}
int8_t
check_rsp_and_faults(
    hb_to_iommu_req_t *req,
    iommu_to_hb_rsp_t *rsp,
    status_t status,
    uint16_t cause, 
    uint64_t exp_iotval2) {

    fault_rec_t fault_rec;
    fqb_t fqb;
    fqh_t fqh;
    uint8_t EXP_TTYP;

    EXP_TTYP = TTYPE_NONE;
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.read_writeAMO == READ ) {
        if ( req->pid_valid && req->exec_req )
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

    fqh.raw = read_register(FQH_OFFSET, 4);
    if ( (fqh.raw >= read_register(FQT_OFFSET, 4)) && (cause != 0) ) {
        printf("No faults logged\n");
        return -1;
    }
    if ( (fqh.raw < read_register(FQT_OFFSET, 4)) && (cause == 0) ) {
        printf("Unexpected fault logged\n");
        return -1;
    }

    if ( cause == 0 ) return 0;

    fqb.raw = read_register(FQB_OFFSET, 8);
    read_memory(((fqb.ppn * PAGESIZE) | (fqh.index * 32)), 32, (char *)&fault_rec);

    // pop the fault record
    fqh.index++;
    write_register(FQH_OFFSET, 4, fqh.raw);

    if ( fault_rec.CAUSE != cause || fault_rec.DID != req->device_id ||
         fault_rec.iotval != req->tr.iova ||
         fault_rec.iotval2 != exp_iotval2 ||
         fault_rec.TTYP != EXP_TTYP ||
         fault_rec.reserved != 0 ) {
        printf("Bad fault record\n");
        return -1;
    }
    if ( (req->pid_valid != fault_rec.PV) ||
         (req->pid_valid && ((fault_rec.PID != req->process_id) ||
                              (fault_rec.PRIV != req->priv_req))) ) {
        printf("Bad fault record\n");
        return -1;
    }
    return 0;
}
uint64_t
add_device(uint32_t device_id, uint32_t gscid, uint8_t en_ats, uint8_t en_pri, uint8_t t2gpa, 
           uint8_t dtf, uint8_t prpr, uint8_t iohgatp_mode, uint8_t iosatp_mode, uint8_t pdt_mode,
           uint8_t msiptp_mode, uint8_t msiptp_pages, uint64_t msi_addr_mask, 
           uint64_t msi_addr_pattern) {
    device_context_t DC;

    memset(&DC, 0, sizeof(DC));

    DC.tc.V      = 1;
    DC.tc.EN_ATS = en_ats;
    DC.tc.EN_PRI = en_pri;
    DC.tc.T2GPA  = t2gpa;
    DC.tc.DTF    = dtf;
    DC.tc.PRPR   = prpr;
    if ( iohgatp_mode != IOHGATP_Bare ) {
        DC.iohgatp.GSCID = gscid;
        DC.iohgatp.PPN = get_free_ppn(4);
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
            add_g_stage_pte(DC.iohgatp, (PAGESIZE * DC.fsc.iosatp.PPN), gpte, 0);
        } else {
            DC.fsc.iosatp.PPN = get_free_ppn(1);
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
            add_g_stage_pte(DC.iohgatp, (PAGESIZE * DC.fsc.pdtp.PPN), gpte, 0);
        } else {
            DC.fsc.iosatp.PPN = get_free_ppn(1);
        }
    }
    if ( msiptp_mode != MSIPTP_Bare ) {
       DC.fsc.iosatp.PPN = get_free_ppn(msiptp_pages);
    }
    return add_dev_context(&DC, device_id);
}
void 
iotinval(
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
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqt.raw = read_register(CQT_OFFSET, 4);
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * 16)), 16);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(CQT_OFFSET, 4, cqt.raw);
    process_commands();
    return;
}
void 
iodir(
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
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqt.raw = read_register(CQT_OFFSET, 4);
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * 16)), 16);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(CQT_OFFSET, 4, cqt.raw);
    process_commands();
    return;
}
void
iofence(
    uint8_t f3, uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WIS_bit, uint64_t addr, uint32_t data) {
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
    cmd.iofence.wis = WIS_bit;
    cmd.iofence.addr_63_2 = addr >> 2;
    cmd.iofence.data = data;
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqt.raw = read_register(CQT_OFFSET, 4);
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqt.index * 16)), 16);
    access_viol_addr = temp;
    data_corruption_addr = temp1;
    cqt.index++;
    write_register(CQT_OFFSET, 4, cqt.raw);
    process_commands();
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
    return free_ppn; 
}
uint8_t read_memory(
    uint64_t addr, uint8_t size, char *data){
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    memcpy(data, &memory[addr], size);
    return 0;
}
uint8_t read_memory_for_AMO(
    uint64_t addr, uint8_t size, char *data) {
    // Same for now
    return read_memory(addr, size, data);
}
uint8_t write_memory(
    char *data, uint64_t addr, uint8_t size) {
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    memcpy(&memory[addr], data, size);
    return 0;
}    
void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW){
    pr_go_requested = PR;
    pw_go_requested = PW;
}
void send_msg_iommu_to_hb(ats_msg_t *prgr){}
