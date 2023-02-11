// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include <stdio.h>
#include <inttypes.h>
#include "iommu.h"
#include "tables_api.h"
#include "test_app.h"
ats_msg_t exp_msg;
ats_msg_t rcvd_msg;
uint8_t exp_msg_received;
uint8_t message_received;
int8_t *memory;
uint64_t access_viol_addr = -1;
uint64_t data_corruption_addr = -1;
uint8_t pr_go_requested = 0;
uint8_t pw_go_requested = 0;
uint64_t next_free_page;
uint64_t next_free_gpage[65536];
int
main(void) {
    capabilities_t cap = {0};
    uint32_t offset;
    fctl_t fctl = {0};
    uint8_t at, pid_valid, exec_req, priv_req, no_write, PR, PW, AV;
    uint32_t i, j, test_num = 0;
    uint64_t DC_addr, exp_iotval2, iofence_PPN, iofence_data, spa, gpa;
    uint64_t gva, gpte_addr, pte_addr, PC_addr, temp;
    volatile uint64_t temp1;
    device_context_t DC;
    process_context_t PC;
    ddte_t ddte;
    ddtp_t ddtp;
    gpte_t gpte;
    pte_t  pte;
    fqcsr_t fqcsr;
    cqcsr_t cqcsr;
    pqcsr_t pqcsr;
    cqb_t cqb;
    cqt_t cqt;
    cqh_t cqh;
    command_t cmd;
    hb_to_iommu_req_t req; 
    iommu_to_hb_rsp_t rsp;
    tr_req_iova_t tr_req_iova;
    tr_req_ctrl_t tr_req_ctrl;
    tr_response_t tr_response;
    iohpmevt_t event;
    pdte_t pdte;
    ats_msg_t pr;
    ats_msg_t inv_cc;
    pqb_t pqb;
    ipsr_t ipsr;
    fqb_t fqb;
    fqh_t fqh;
    fault_rec_t fault_rec;

    // reset system
    fail_if( ( reset_system(1, 2) < 0 ) );

    // Reset the IOMMU
    cap.version = 0x10;
    cap.Sv39 = cap.Sv48 = cap.Sv57 = cap.Sv39x4 = cap.Sv48x4 = cap.Sv57x4 = 1;
    cap.amo = cap.ats = cap.t2gpa = cap.hpm = cap.msi_flat = cap.msi_mrif = 1;
    cap.dbg = 1;
    cap.pas = 50;
    cap.pd20 = cap.pd17 = cap.pd8 = 1;
    fail_if( ( reset_iommu(8, 40, 0xff, 3, Off, 0, 0, cap, fctl) < 0 ) );
    for ( i = MSI_ADDR_0_OFFSET; i <= MSI_ADDR_7_OFFSET; i += 16 ) {
        write_register(i, 8, 0xFF);
        fail_if(( read_register(i, 8) != 0xFc ));
        write_register(i, 8, 0x00);
    }
    for ( i = MSI_DATA_0_OFFSET; i <= MSI_DATA_7_OFFSET; i += 16 ) {
        write_register(i, 4, 0);
        fail_if( ( read_register(i, 4) != 0 ) );
    }
    for ( i = MSI_VEC_CTRL_0_OFFSET; i <= MSI_VEC_CTRL_7_OFFSET; i += 16 ) {
        write_register(i, 4, 1);
        fail_if( ( read_register(i, 4) != 1 ) );
    }
    // When Fault queue is not enabled, no logging should occur
    pid_valid = exec_req = priv_req = no_write = 1;
    at = 0;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( ((read_register(FQH_OFFSET, 4)) != read_register(FQT_OFFSET, 4)) ) );
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqof != 0 ) );

    // Enable command queue
    fail_if( ( enable_cq(4) < 0 ) );
    // Enable fault queue
    fail_if( ( enable_fq(4) < 0 ) );
    // Enable page queue
    fail_if( ( enable_disable_pq(4, 1) < 0 ) );

    START_TEST("All inbound transactions disallowed");
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 256, 0) < 0 ) );
        }
    });
    END_TEST();

    START_TEST("Bare mode tests");
    // Enable IOMMU in Bare mode
    fail_if( ( enable_iommu(DDT_Bare) < 0 ) );
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                 priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } 
        if ( at == ADDR_TYPE_TRANSLATED ) {
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 260, 0) < 0 ) );
        } 
        if ( at == ADDR_TYPE_UNTRANSLATED ) {
                fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
        }
    });
    // Turn it off
    fail_if( ( enable_iommu(Off) < 0 ) );
    END_TEST();

    START_TEST("Too wide device_id");
    fail_if( ( enable_iommu(DDT_1LVL) < 0 ) );
    send_translation_request(0x000145, 0, 0x99, 0, 0, 0, 0, 
                             UNTRANSLATED_REQUEST, 0, 1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    fail_if( ( enable_iommu(DDT_2LVL) < 0 ) );
    send_translation_request(0x012345, 0, 0x99, 0, 0, 0, 0, 
                             UNTRANSLATED_REQUEST, 0, 1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );

    END_TEST();

    // Enable IOMMU
    fail_if( ( enable_iommu(DDT_3LVL) < 0 ) );

    START_TEST("Non-leaf DDTE invalid");
    // make DDTE invalid
    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddte.raw  = 0;
    write_memory((char *)&ddte, (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8), 8);
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) );
        }
    });
    END_TEST();

    START_TEST("NL-DDT access viol & data corruption");
    iodir(INVAL_DDT, 1, 0x012345, 0);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    access_viol_addr = (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 257, 0) < 0 ) );
    access_viol_addr = -1;
    data_corruption_addr = (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 268, 0) < 0 ) );
    data_corruption_addr = -1;
    END_TEST();

    START_TEST("Non-leaf DDTE reserved bits");
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
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
        }
    });
    ddte.raw  = 0;
    write_memory((char *)&ddte, (ddtp.ppn * PAGESIZE) | (get_bits(23, 15, 0x012345) * 8), 8);
    END_TEST();


    START_TEST("Fault queue overflow and memory fault");
    // Clear IPSR
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip == 1 ) );

    // Initialize fault queue MSI vector
    // Map fault queue to vector 5
    write_register(ICVEC_OFFSET, 8, 0x0000000000000050);
    temp = get_free_ppn(1) * PAGESIZE;
    j = 0;
    write_memory((char *)&j, temp, 4);
    write_register(MSI_ADDR_5_OFFSET, 8, temp);
    write_register(MSI_DATA_5_OFFSET, 4, 0xDEADBEEF);
    write_register(MSI_VEC_CTRL_5_OFFSET, 4, 0);
    write_register(IPSR_OFFSET, 4, read_register(IPSR_OFFSET, 4));

    // Trigger a fault queue overflow
    // The queue should be empty now
    fail_if( ( (read_register(FQH_OFFSET, 4) != read_register(FQT_OFFSET, 4)) ) );
    pid_valid = exec_req = priv_req = no_write = 1;
    at = 0;
    for ( i = 0; i < 1023; i++ ) {
        send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                 priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    }
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip == 0 ) );
    read_memory(temp, 4, (char *)&j);
    fail_if( ( j != 0xDEADBEEF ) );
    j = 0;
    write_memory((char *)&j, temp, 4);
    // Clear IPSR
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 0 ) );

    // The queue should be be full
    fail_if( ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) );
    // No overflow should be set
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqof == 1 ) );
    // Next fault should cause overflow
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) );
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqof == 0 ) );

    // Overflow should have triggered a MSI
    read_memory(temp, 4, (char *)&j);
    fail_if( ( j != 0xDEADBEEF ) );
    j = 0;
    write_memory((char *)&j, temp, 4);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    // Clear IPSR
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    // Should retrigger since fqof is still set
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    read_memory(temp, 4, (char *)&j);
    fail_if( ( j != 0xDEADBEEF ) );
    j = 0;
    write_memory((char *)&j, temp, 4);

    // Overflow should remain
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( ((read_register(FQH_OFFSET, 4) - 1) != read_register(FQT_OFFSET, 4)) ) );
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqof == 0 ) );

    // disable and re-enable fault queue
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fqcsr.fqen = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( (fqcsr.fqen == 1) );
    fail_if( (fqcsr.fqon == 1) );
    fail_if( (fqcsr.busy == 1) );
    fail_if( (fqcsr.fqmf == 1) );
    fail_if( (fqcsr.fqof == 1) );
    fail_if( ( ((read_register(FQH_OFFSET, 4)) != read_register(FQT_OFFSET, 4)) ) );
    fail_if( ( (read_register(FQH_OFFSET, 4) != 0) ) );

    // Clear IPSR
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 0 ) );

    fqcsr.fqen = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( (fqcsr.fqen == 0) );
    fail_if( (fqcsr.fqon == 0) );
    fail_if( (fqcsr.busy == 1) );
    fail_if( (fqcsr.fqmf == 1) );
    fail_if( (fqcsr.fqof == 1) );

    // Create a memory fault
    fqb.raw = read_register(FQB_OFFSET, 8);
    access_viol_addr = fqb.ppn * PAGESIZE;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( read_register(FQH_OFFSET, 4) != read_register(FQT_OFFSET, 4) ) );
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqmf != 1 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( read_register(FQH_OFFSET, 4) != read_register(FQT_OFFSET, 4) ) );
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqmf == 1 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 0 ) );

    // Memory fault with interrupts inhibited
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fqcsr.fie = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqmf != 1 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip == 1 ) );
    fqcsr.fie = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);

    access_viol_addr = -1;
    fqcsr.fqen = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.fqen = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    // memory fault on MSI write
    access_viol_addr = temp;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fqcsr.raw = read_register(FQCSR_OFFSET, 4);
    fail_if( ( fqcsr.fqmf != 0 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.fip != 1 ) );
    fqh.raw = read_register(FQH_OFFSET, 4);
    fqb.raw = read_register(FQB_OFFSET, 8);
    read_memory(((fqb.ppn * PAGESIZE) | ((fqh.index + 1) * 32)), 32, (char *)&fault_rec);
    fail_if( ( fault_rec.TTYP != 0 ) );
    fail_if( ( fault_rec.iotval != temp ) );
    fail_if( ( fault_rec.CAUSE != 273 ) );
    fail_if( ( fault_rec.DID != 0 ) );
    fail_if( ( fault_rec.PID != 0 ) );
    fail_if( ( fault_rec.PV != 0 ) );
    fail_if( ( fault_rec.PRIV != 0 ) );
    access_viol_addr = -1;
    fqcsr.fqen = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    fqcsr.fqen = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);

    END_TEST();

    START_TEST("Device context invalid");

    // Add a device 0x012345 to guest with GSCID=1
    DC_addr = add_device(0x012345, 1, 0, 0, 0, 0, 0, 
                         1, 1, 0, 0, 0, 
                         IOHGATP_Sv48x4, IOSATP_Bare, PDTP_Bare,
                         MSIPTP_Flat, 1, 0xFFFFFFFFFF, 0x1000000000);
    (void)(DC_addr);

    // make DC invalid
    read_memory(DC_addr, 64, (char *)&DC);
    DC.tc.V = 0;
    write_memory((char *)&DC, DC_addr, 64);
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } else {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) );
        }
    });
    DC.tc.V = 1;
    write_memory((char *)&DC, DC_addr, 64);
    END_TEST();

    START_TEST("Device context misconfigured");
    read_memory(DC_addr, 64, (char *)&DC);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    DC.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.reserved = 0;
    DC.tc.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.reserved = 0;

    DC.ta.reserved0 = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.ta.reserved0 = 0;
    DC.ta.reserved1 = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.ta.reserved1 = 0;
    DC.fsc.iosatp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.fsc.iosatp.reserved = 0;
    DC.msiptp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.msiptp.reserved = 0;
    DC.msi_addr_mask.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.msi_addr_mask.reserved = 0;
    DC.msi_addr_pattern.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.msi_addr_pattern.reserved = 0;
    DC.iohgatp.MODE = IOHGATP_Sv32x4;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.iohgatp.MODE = IOHGATP_Sv57x4 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.iohgatp.MODE = IOHGATP_Sv48x4;
    DC.fsc.iosatp.MODE = IOSATP_Sv32;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.fsc.iosatp.MODE = IOSATP_Bare;
    DC.fsc.iosatp.MODE = IOSATP_Sv57 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.fsc.iosatp.MODE = IOSATP_Bare;
    DC.msiptp.MODE = MSIPTP_Flat + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.msiptp.MODE = MSIPTP_Off;
    DC.tc.T2GPA = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.T2GPA = 0;
    DC.tc.EN_PRI = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.EN_PRI = 0;
    DC.tc.PRPR = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.PRPR = 0;
    DC.tc.PDTV = 1;
    DC.fsc.pdtp.MODE = PD20 + 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.fsc.pdtp.MODE = PDTP_Bare;
    DC.fsc.pdtp.reserved = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.fsc.pdtp.reserved = 0;
    DC.tc.PDTV = 0;
    write_memory((char *)&DC, DC_addr, 64);

    DC.iohgatp.PPN |= 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.iohgatp.PPN |= 2;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.iohgatp.PPN &= ~0x3;
    write_memory((char *)&DC, DC_addr, 64);

    g_reg_file.capabilities.amo = 0;
    DC.tc.SADE = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.SADE = 0;
    DC.tc.GADE = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.SADE = 1;
    DC.tc.GADE = 1;
    write_memory((char *)&DC, DC_addr, 64);
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 259, 0) < 0 ) );
    DC.tc.SADE = 0;
    DC.tc.GADE = 0;
    write_memory((char *)&DC, DC_addr, 64);
    g_reg_file.capabilities.amo = 1;



    END_TEST();

    START_TEST("Unsupported transaction type");
    FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, {
        if ( at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, READ, &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
        } else if ( at == ADDR_TYPE_TRANSLATED ) {
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 260, 0) < 0 ) );
        } else {
            uint16_t exp_cause;
            send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                                     priv_req, 0, at, 0xdeadbeef, 16, (no_write ^ 1), &req, &rsp);
            if ( pid_valid == 1 ) exp_cause = 260;
            else if ( (no_write ^ 1) == WRITE ) exp_cause = 23;
            else exp_cause = 21;
            exp_iotval2 = ( exp_cause != 260) ? 0xdeadbeec : 0;
            // iotval2 reports the gpa i.e. the iova
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, exp_cause, exp_iotval2) < 0 )
                );
        }
    });
    END_TEST();


    START_TEST("Dev. ctx. access viol & data corruption");
    iodir(INVAL_DDT, 1, 0x012345, 0);
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    access_viol_addr = DC_addr;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 257, 0) < 0 ) );
    access_viol_addr = -1;
    data_corruption_addr = DC_addr;
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 268, 0) < 0 ) );
    data_corruption_addr = -1;
    END_TEST();


    START_TEST("Device context invalidation");
    at = ADDR_TYPE_UNTRANSLATED;
    pid_valid = no_write = exec_req = priv_req = 0;
    // Get the device context cached
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 0xdeadbeec) < 0 ) );
    // Update memory to mark invalid
    DC.tc.V = 0;
    write_memory((char *)&DC, DC_addr, 64);
    // Cached copy should apply
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 0xdeadbeec) < 0 ) );
    iodir(INVAL_DDT, 1, 0x012345, 0);
    fail_if( ( read_register(CQH_OFFSET, 4) != read_register(CQT_OFFSET, 4) ) );
    // Memory copy should apply
    send_translation_request(0x012345, pid_valid, 0x99, no_write, exec_req,
                             priv_req, 0, at, 0xdeadbeef, 16, (no_write ^1), &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 258, 0) < 0 ) );
    DC.tc.V = 1;
    DC.tc.EN_ATS = 1;
    write_memory((char *)&DC, DC_addr, 64);
    END_TEST();

    START_TEST("IOFENCE");
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
                fail_if( ( AV == 1 && iofence_data != 0x12345678DEADBEEF )  );
                fail_if( ( AV == 0 && iofence_data != 0x1234567812345678 )  );
                fail_if( ( PR != pr_go_requested ) );
                fail_if( ( PW != pw_go_requested ) );
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
    fail_if( ( cqcsr.cmd_ill != 1 ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( 0 != pr_go_requested ) );

    // Queue another - since illegal is set, head should not move
    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    fail_if( ( (read_register(CQH_OFFSET, 4) + 2) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( 0 != pr_go_requested ) );

    // fix the illegal commend 
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqh.raw = read_register(CQH_OFFSET, 4);
    read_memory(((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16, (char *)&cmd);
    cmd.iofence.func3 = IOFENCE_C;
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16);

    // Clear the illegal
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEEF )  );
    fail_if( ( 1 != pr_go_requested ) );

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEEF )  );
    fail_if( ( 1 != pr_go_requested ) );

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    // Set WIS - not supported in this config
    iofence(IOFENCE_C, 1, 0, 1, 1, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_ill != 1 ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( 0 != pr_go_requested ) );
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );
    // Clear the illegal
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    // fix the illegal commend 
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqh.raw = read_register(CQH_OFFSET, 4);
    read_memory(((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16, (char *)&cmd);
    cmd.iofence.wis = 0;
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16);
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEEF )  );
    fail_if( ( 1 != pr_go_requested ) );

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
    fail_if( ( cqcsr.cqmf != 1 ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( 0 != pr_go_requested ) );
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );

    // Queue another - since cqmf is set, head should not move
    iofence(IOFENCE_C, 0, 1, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE2);
    fail_if( ( (read_register(CQH_OFFSET, 4) + 2) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( 0 != pr_go_requested ) );
    // Clear the cqmf
    access_viol_addr = -1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEE1 )  );
    fail_if( ( 1 != pr_go_requested ) );
    fail_if( ( 0 != pw_go_requested ) );
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4)) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEE2 )  );
    fail_if( ( 0 != pr_go_requested ) );
    fail_if( ( 1 != pw_go_requested ) );

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    // Cause memory fault on completion buffer
    access_viol_addr = iofence_PPN * PAGESIZE;

    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqmf != 1 ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );

    // Clear the cqmf
    access_viol_addr = -1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    process_commands();
    fail_if( ( (read_register(CQH_OFFSET, 4) ) != read_register(CQT_OFFSET, 4) ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678DEADBEE1 )  );

    iofence_data = 0x1234567812345678;
    pr_go_requested = 0;
    pw_go_requested = 0;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);

    // Cause memory fault on completion buffer
    access_viol_addr = iofence_PPN * PAGESIZE;
    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqmf != 1 ) );
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( (read_register(CQH_OFFSET, 4) + 1) != read_register(CQT_OFFSET, 4) ) );
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqen == 1 ) );
    fail_if( ( cqcsr.cqon == 1 ) );
    fail_if( ( cqcsr.cqmf == 1 ) );
    fail_if( ( cqcsr.busy == 1 ) );
    fail_if( ( cqcsr.cmd_ill == 1 ) );
    fail_if( ( cqcsr.cmd_to == 1 ) );
    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqen == 0 ) );
    fail_if( ( cqcsr.cqon == 0 ) );
    fail_if( ( cqcsr.cqmf == 1 ) );
    fail_if( ( cqcsr.busy == 1 ) );
    fail_if( ( cqcsr.cmd_ill == 1 ) );
    fail_if( ( cqcsr.cmd_to == 1 ) );

    // DIsable CQ interrupts
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    cqcsr.cie = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    access_viol_addr = iofence_PPN * PAGESIZE;
    iofence(IOFENCE_C, 1, 0, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqmf != 1 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.cip != 0 ) );
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.cqen = 1;
    cqcsr.cie = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);

    access_viol_addr = -1;

    // Test fence_w_ip
    g_reg_file.capabilities.igs = IGS_BOTH;
    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddtp.iommu_mode = Off;
    write_register(DDTP_OFFSET, 8, ddtp.raw);
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    fqcsr.fqen = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    pqcsr.pqen = 0;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);

    fctl.raw = read_register(FCTRL_OFFSET, 4);
    fctl.wis = 1;
    write_register(FCTRL_OFFSET, 4, fctl.raw);
    fctl.raw = read_register(FCTRL_OFFSET, 4);
    fail_if( (fctl.wis == 0) );

    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    fqcsr.fqen = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    pqcsr.pqen = 1;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddtp.iommu_mode = DDT_3LVL;
    write_register(DDTP_OFFSET, 8, ddtp.raw);

    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( (ipsr.cip == 1) );

    iofence(IOFENCE_C, 1, 0, 1, 1, (iofence_PPN * PAGESIZE), 0xDEADBEE1);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( (cqcsr.fence_w_ip == 0) );
    fail_if( (ipsr.cip == 0) );
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( (ipsr.cip == 0) );
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    write_register(IPSR_OFFSET, 4, ipsr.raw);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( (cqcsr.fence_w_ip == 1) );
    fail_if( (ipsr.cip == 1) );

    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddtp.iommu_mode = Off;
    write_register(DDTP_OFFSET, 8, ddtp.raw);
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    fqcsr.fqen = 0;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    pqcsr.pqen = 0;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);

    fctl.wis = 0;
    write_register(FCTRL_OFFSET, 4, fctl.raw);
    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    fqcsr.fqen = 1;
    write_register(FQCSR_OFFSET, 4, fqcsr.raw);
    pqcsr.pqen = 1;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    ddtp.raw = read_register(DDTP_OFFSET, 8);
    ddtp.iommu_mode = DDT_3LVL;
    write_register(DDTP_OFFSET, 8, ddtp.raw);
    g_reg_file.capabilities.igs = MSI;

    END_TEST();

    START_TEST("G-stage translation sizes");
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
    DC.tc.SADE = 1;
    DC.tc.GADE = 1;
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
            fail_if( ( rsp.status != SUCCESS ) ); 
            fail_if( ( rsp.trsp.S == 1 && i == 0 ) ); 
            fail_if( ( rsp.trsp.S == 0 && i != 0 ) ); 
            fail_if( ( rsp.trsp.U != 0 ) );
            fail_if( ( rsp.trsp.R != 1 ) );
            fail_if( ( rsp.trsp.W != 1 ) );
            fail_if( ( rsp.trsp.Exe != 1 ) );
            fail_if( ( rsp.trsp.PBMT != PMA ) );
            fail_if( ( rsp.trsp.is_msi != 0 ) );
            if ( rsp.trsp.S == 1 )  {
                temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
                temp = temp  * PAGESIZE | 0xFFF;
            } else {
                temp = 0xFFF;
            }
            fail_if( ( ((rsp.trsp.PPN * PAGESIZE) & ~temp) != (gpte.PPN * PAGESIZE) ) );
            fail_if( ( ((temp + 1) != PAGESIZE) && i == 0 ) ); 
            fail_if( ( ((temp + 1) != 512UL * PAGESIZE) && i == 1 ) ); 
            fail_if( ( ((temp + 1) != 512UL * 512UL * PAGESIZE) && i == 2 ) ); 
            fail_if( ( ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) && i == 3 ) ); 
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
        fail_if( ( rsp.status != SUCCESS ) ); 
        fail_if( ( rsp.trsp.U != 0 ) );
        fail_if( ( rsp.trsp.R != 1 ) );
        fail_if( ( rsp.trsp.W != 1 ) );
        fail_if( ( rsp.trsp.Exe != 1 ) );
        fail_if( ( rsp.trsp.PBMT != PMA ) );
        fail_if( ( rsp.trsp.is_msi != 0 ) );
        fail_if( ( rsp.trsp.S != 1 ) );
        temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
        temp = temp  * PAGESIZE | 0xFFF;
        fail_if( ( i == 0 && ((temp + 1) != 2 * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 1 && ((temp + 1) != 512UL * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 2 && ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 3 && ((temp + 1) != 512UL * 512UL * 512UL * 512UL * PAGESIZE) ) ); 
    }
    END_TEST();

    START_TEST("G-stage permission faults");
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
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 4);
    read_memory(gpte_addr, 8, (char *)&gpte);

    gpte.U = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) );

    gpte.U = 1;
    gpte.W = 1;
    gpte.R = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) );

    gpte.X = 1;
    gpte.W = 0;
    gpte.R = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) );

    gpte.PPN = 512UL * 512UL * 512UL ;
    gpte.X = 1;
    gpte.W = 1;
    gpte.R = 1;
    write_memory((char *)&gpte, gpte_addr, 8);
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) );
    gpte.PPN = 512UL * 512UL * 512UL * 512UL;
    write_memory((char *)&gpte, gpte_addr, 8);

    access_viol_addr = gpte_addr;
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 7, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) );

    tr_req_ctrl.DID = 0x012345;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    tr_req_iova.raw = req.tr.iova;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    fail_if( ( tr_response.fault == 0 ) );
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) );

    access_viol_addr = -1;

    data_corruption_addr = gpte_addr;
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );

    tr_req_ctrl.DID = 0x012345;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    tr_req_iova.raw = req.tr.iova;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    fail_if( ( tr_response.fault == 0 ) );
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );

    data_corruption_addr = -1;



    END_TEST();

    START_TEST("IOTINVAL.GVMA");
    req.device_id = 0x012345;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.length = 64;
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    gpte.PPN = 512UL * 512UL * 512UL ;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    iotinval(GVMA, 1, 0, 0, 1, 0, 0);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa >> 2) << 2)) < 0 ) );
    gpte.PPN = 512UL * 512UL * 512UL * 512UL;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    iotinval(GVMA, 1, 1, 0, 1, 0, req.tr.iova);
    gpte.W = 0;
    write_memory((char *)&gpte, gpte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, ((gpa >> 2) << 2)) < 0 ) );
    gpte.W = 1;
    write_memory((char *)&gpte, gpte_addr, 8);
    iotinval(GVMA, 1, 1, 0, 1, 0, req.tr.iova);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    END_TEST();

    START_TEST("S-stage translation sizes");
    DC_addr = add_device(0x012349, 1, 1, 1, 0, 0, 1, 
                         1, 1, 0, 0, 0,
                         IOHGATP_Bare, IOSATP_Sv57, PDTP_Bare,
                         MSIPTP_Flat, 1, 0xF0F00FF0FF, 0x1903020124);
    read_memory(DC_addr, 64, (char *)&DC);
    DC.ta.PSCID = 10;
    write_memory((char *)&DC, DC_addr, 64);
    req.device_id = 0x012349;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST;
    req.tr.length = 64;
    req.tr.read_writeAMO = WRITE;
    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    for ( j = 0; j < 3; j++ ) {
        if ( j == 2 ) {
            DC.fsc.iosatp.MODE = IOSATP_Sv57;
            gva = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
            gva = gva * 8;
        } else if ( j == 1 ) {
            DC.fsc.iosatp.MODE = IOSATP_Sv48;
            gva = 512UL * 512UL * 512UL * PAGESIZE;
            gva = gva * 4;
        } else {
            DC.fsc.iosatp.MODE = IOSATP_Sv39;
            gva = 512UL * 512UL * PAGESIZE;
        }
        write_memory((char *)&DC, DC_addr, 64);
        iodir(INVAL_DDT, 1, 0x012349, 0);
        for ( i = 0; i < 5; i++ ) {
            if ( (i == 4) && DC.fsc.iosatp.MODE != IOSATP_Sv57 ) continue;
            if ( (i == 3) && DC.fsc.iosatp.MODE != IOSATP_Sv48 && 
                             DC.fsc.iosatp.MODE != IOSATP_Sv57 ) continue;
            gva = gva | ((1 << (i * 9)) * PAGESIZE) | 2048;
            req.tr.iova = gva;
            pte.PPN = 512UL * 512UL * 512UL * 512UL;
            pte.PPN |= (1UL << (i * 9UL));
            add_s_stage_pte(DC.fsc.iosatp, gva, pte, i);
            iommu_translate_iova(&req, &rsp);
            fail_if( ( rsp.status != SUCCESS ) ); 
            fail_if( ( rsp.trsp.S == 1 && i == 0 ) ); 
            fail_if( ( rsp.trsp.S == 0 && i != 0 ) ); 
            fail_if( ( rsp.trsp.U != 0 ) );
            fail_if( ( rsp.trsp.R != 1 ) );
            fail_if( ( rsp.trsp.W != 1 ) );
            fail_if( ( rsp.trsp.Exe != 1 ) );
            fail_if( ( rsp.trsp.PBMT != PMA ) );
            fail_if( ( rsp.trsp.is_msi != 0 ) );
            if ( rsp.trsp.S == 1 )  {
                temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
                temp = temp  * PAGESIZE | 0xFFF;
            } else {
                temp = 0xFFF;
            }
            fail_if( ( ((rsp.trsp.PPN * PAGESIZE) & ~temp) != (pte.PPN * PAGESIZE) ) );
            fail_if( ( ((temp + 1) != PAGESIZE) && i == 0 ) ); 
            fail_if( ( ((temp + 1) != 512UL * PAGESIZE) && i == 1 ) ); 
            fail_if( ( ((temp + 1) != 512UL * 512UL * PAGESIZE) && i == 2 ) ); 
            fail_if( ( ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) && i == 3 ) ); 
        }
    }
    g_reg_file.capabilities.Sv57 = 0;
    g_reg_file.capabilities.Sv48 = 0;
    g_reg_file.capabilities.Sv39 = 0;
    g_reg_file.capabilities.Sv32 = 0;
    for ( i = 0; i < 4; i++ ) {
        if ( i == 0 ) g_reg_file.capabilities.Sv32 = 1;
        if ( i == 1 ) g_reg_file.capabilities.Sv39 = 1;
        if ( i == 2 ) g_reg_file.capabilities.Sv48 = 1;
        if ( i == 3 ) g_reg_file.capabilities.Sv57 = 1;
        DC.fsc.iosatp.MODE = IOSATP_Bare;
        write_memory((char *)&DC, DC_addr, 64);
        iodir(INVAL_DDT, 1, 0x012349, 0);
        gva = 512UL * 512UL * PAGESIZE;
        req.tr.iova = gva;
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        fail_if( ( rsp.trsp.U != 0 ) );
        fail_if( ( rsp.trsp.R != 1 ) );
        fail_if( ( rsp.trsp.W != 1 ) );
        fail_if( ( rsp.trsp.Exe != 1 ) );
        fail_if( ( rsp.trsp.PBMT != PMA ) );
        fail_if( ( rsp.trsp.is_msi != 0 ) );
        fail_if( ( rsp.trsp.S != 1 ) );
        temp = rsp.trsp.PPN ^ (rsp.trsp.PPN  + 1);
        temp = temp  * PAGESIZE | 0xFFF;
        fail_if( ( i == 0 && ((temp + 1) != 2 * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 1 && ((temp + 1) != 512UL * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 2 && ((temp + 1) != 512UL * 512UL * 512UL * PAGESIZE) ) ); 
        fail_if( ( i == 3 && ((temp + 1) != 512UL * 512UL * 512UL * 512UL * PAGESIZE) ) ); 
    }

    // IOTINVAL not allowed to set PSCV
    iotinval(GVMA, 1, 1, 1, 1, 0, req.tr.iova);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_ill != 1 ) );
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);


    END_TEST();

    START_TEST("S-stage permission faults");
    req.device_id = 0x012349;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.length = 64;
    req.tr.read_writeAMO = WRITE;
    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    DC.fsc.iosatp.MODE = IOSATP_Sv57;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    gva = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
    gva = gva * 16;
    gva = gva | ((1 << (i * 9)) * PAGESIZE) | 2048;
    req.tr.iova = gva;
    pte.PPN = 512UL * 512UL * 512UL * 512UL;
    pte_addr = add_s_stage_pte(DC.fsc.iosatp, gva, pte, i);
    read_memory(pte_addr, 8, (char *)&pte);

    pte.U = 1;
    pte.W = 1;
    pte.R = 0;
    write_memory((char *)&pte, pte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 15, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );

    pte.X = 1;
    pte.W = 0;
    pte.R = 0;
    write_memory((char *)&pte, pte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 15, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );

    pte.PPN = 512UL * 512UL * 512UL ;
    pte.X = 1;
    pte.W = 1;
    pte.R = 1;
    write_memory((char *)&pte, pte_addr, 8);
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
    pte.PPN = 512UL * 512UL * 512UL * 512UL;
    write_memory((char *)&pte, pte_addr, 8);

    pte.PBMT = 3;
    write_memory((char *)&pte, pte_addr, 8);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
    pte.PBMT = 0;
    write_memory((char *)&pte, pte_addr, 8);

    access_viol_addr = pte_addr;

    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 7, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) );

    // Check DTF
    temp = read_register(FQT_OFFSET, 4);
    DC.tc.DTF = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    fail_if( ( temp != read_register(FQT_OFFSET, 4) ) );
    DC.tc.DTF = 0;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) );
    fail_if( ( temp == read_register(FQT_OFFSET, 4) ) );

    tr_req_ctrl.DID = 0x012349;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    tr_req_iova.raw = req.tr.iova;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    fail_if( ( tr_response.fault == 0 ) );
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 5, 0) < 0 ) );

    access_viol_addr = -1;

    data_corruption_addr = pte_addr;

    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );

    // Check DTF
    temp = read_register(FQT_OFFSET, 4);
    DC.tc.DTF = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    req.tr.read_writeAMO = READ;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    fail_if( ( temp != read_register(FQT_OFFSET, 4) ) );
    DC.tc.DTF = 0;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );
    fail_if( ( temp == read_register(FQT_OFFSET, 4) ) );

    tr_req_ctrl.DID = 0x012349;
    tr_req_ctrl.PV = 0;
    tr_req_ctrl.RWn = 1;
    tr_req_ctrl.go_busy = 1;
    tr_req_iova.raw = req.tr.iova;
    write_register(TR_REQ_IOVA_OFFSET, 8, tr_req_iova.raw);
    write_register(TR_REQ_CTRL_OFFSET, 8, tr_req_ctrl.raw);
    tr_response.raw = read_register(TR_RESPONSE_OFFSET, 8);
    fail_if( ( tr_response.fault == 0 ) );
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 274, 0) < 0 ) );

    data_corruption_addr = -1;


    END_TEST();

    START_TEST("IOTINVAL.VMA");
    req.device_id = 0x012349;
    req.pid_valid = 0;
    req.is_cxl_dev = 0;
    req.tr.at = ADDR_TYPE_UNTRANSLATED;
    req.tr.length = 64;
    req.tr.read_writeAMO = READ;

    for ( int g = 0; g < 1; g++ ) {
        pte.G = g;
        // AV=0, PSCV=0
        // Fill TLB
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Corrupt PTE
        pte.PPN = 512UL * 512UL * 512UL ;
        write_memory((char *)&pte, pte_addr, 8);
        // Hit in TLB
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Inv TLB
        iotinval(VMA, 0, 0, 0, 1, 10, 0);
        // Check fault observed - invalidate success
        iommu_translate_iova(&req, &rsp);
        fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
        // Correct fault
        pte.PPN = 512UL * 512UL * 512UL * 512UL;
        write_memory((char *)&pte, pte_addr, 8);

        // AV=0, PSCV=1
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Corrupt PTE
        pte.PPN = 512UL * 512UL * 512UL ;
        write_memory((char *)&pte, pte_addr, 8);
        // Hit in TLB
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Inv TLB - globals dont get flushed
        iotinval(VMA, 0, 0, 1, 1, 10, 0);
        // Check fault observed - invalidate success
        iommu_translate_iova(&req, &rsp);
        fail_if( ( g == 1 && rsp.status != SUCCESS ) );
        fail_if( ( g == 0 && check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
        if ( g == 1) {
            // Inv TLB - by address - flush globals
            // AV=1, PSCV=1
            iotinval(VMA, 0, 1, 1, 1, 10, req.tr.iova);
            // Check fault observed - invalidate success
            iommu_translate_iova(&req, &rsp);
            fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
        }
        // Correct fault
        pte.PPN = 512UL * 512UL * 512UL * 512UL;
        write_memory((char *)&pte, pte_addr, 8);

        // AV=1, PSCV=0
        // Fill TLB
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Corrupt PTE
        pte.PPN = 512UL * 512UL * 512UL ;
        write_memory((char *)&pte, pte_addr, 8);
        // Hit in TLB
        iommu_translate_iova(&req, &rsp);
        fail_if( ( rsp.status != SUCCESS ) ); 
        // Inv TLB
        iotinval(VMA, 0, 1, 0, 1, 10, req.tr.iova);
        // Check fault observed - invalidate success
        iommu_translate_iova(&req, &rsp);
        fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 13, 0) < 0 ) );
        // Correct fault
        pte.PPN = 512UL * 512UL * 512UL * 512UL;
        write_memory((char *)&pte, pte_addr, 8);
    }
    pte.W = 0;
    write_memory((char *)&pte, pte_addr, 8);
    // Fill TLB
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    // Fault from TLB
    pte.W = 1;
    write_memory((char *)&pte, pte_addr, 8);
    req.tr.read_writeAMO = WRITE;
    iommu_translate_iova(&req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 15, 0) < 0 ) );
    // Inv TLB
    iotinval(VMA, 0, 0, 0, 1, 10, 0);
    iommu_translate_iova(&req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    END_TEST();

    START_TEST("HPM filtering");
    
    for ( i = 0; i < 32; i++ ) {
        write_register(IOHPMEVT1_OFFSET + (i * 8), 8, 0);
        write_register(IOHPMCTR1_OFFSET + (i * 8), 8, 0);
    }
    write_register(IOCNTINH_OFFSET, 4, 0);

    event.eventID = UNTRANSLATED_REQUEST;
    event.dmask = 0;
    event.pid_pscid = 0;
    event.did_gscid = 0x012349;
    event.pv_pscv = 0;
    event.dv_gscv = 1;
    event.idt = 0;
    event.of = 0;
    write_register(IOHPMEVT1_OFFSET, 8, event.raw);
    write_register(IOHPMCTR1_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);

    event.eventID = TRANSLATED_REQUEST;
    event.dmask = 1;
    event.did_gscid = 0x01237f;
    write_register(IOHPMEVT2_OFFSET, 8, event.raw);
    write_register(IOHPMCTR2_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);

    event.eventID = TRANSLATION_REQUEST;
    event.dmask = 1;
    event.did_gscid = 0x01237f;
    event.pv_pscv = 1;
    event.pid_pscid = 10;
    write_register(IOHPMEVT3_OFFSET, 8, event.raw);
    write_register(IOHPMCTR3_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);


    event.eventID = TRANSLATION_REQUEST;
    event.dmask = 1;
    event.did_gscid = 0x01237f;
    event.dv_gscv = 0;
    event.pid_pscid = 10;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    write_register(IOHPMCTR4_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);

    event.eventID = TRANSLATION_REQUEST;
    event.dmask = 1;
    event.did_gscid = 0x01237f;
    event.dv_gscv = 0;
    event.pv_pscv = 0;
    event.pid_pscid = 10;
    write_register(IOHPMEVT5_OFFSET, 8, event.raw);
    write_register(IOHPMCTR5_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);

    for ( at = 0; at < 3; at++ ) {
        send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, at, 0xdeadbeef, 1, READ, &req, &rsp);
        send_translation_request(0x012349, 1, 10, 0,
             0, 0, 0, at, 0xdeadbeef, 1, READ, &req, &rsp);
        send_translation_request(0x072349, 1, 10, 0,
             0, 0, 0, at, 0xdeadbeef, 1, READ, &req, &rsp);
    }
    fail_if( ( read_register(IOHPMCTR1_OFFSET, 8) != 1 ) );
    fail_if( ( read_register(IOHPMCTR2_OFFSET, 8) != 1 ) );
    fail_if( ( read_register(IOHPMCTR3_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 1 ) );
    fail_if( ( read_register(IOHPMCTR5_OFFSET, 8) != 2 ) );
    fail_if( ( (read_register(IOCNTOVF_OFFSET, 4) & 0xFFFFFFFF) != 0x3E ) );
    event.raw = read_register(IOHPMEVT1_OFFSET, 8);
    event.of = 0;
    write_register(IOHPMEVT1_OFFSET, 8, event.raw);
    fail_if( ( (read_register(IOCNTOVF_OFFSET, 4) & 0xFFFFFFFF) != 0x3C ) );

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    DC.fsc.iosatp.MODE = IOSATP_Sv57;
    DC.ta.PSCID = 10;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x012349, 0);
    gva = 512UL * 512UL * 512UL * 512UL * PAGESIZE;
    gva = gva * 24;
    gva = gva | ((1 << (i * 9)) * PAGESIZE) | 2048;
    req.tr.iova = gva;
    pte.PPN = 512UL * 512UL * 512UL * 512UL;
    pte_addr = add_s_stage_pte(DC.fsc.iosatp, gva, pte, 0);

    event.eventID = IOATC_TLB_MISS;
    event.dmask = 0;
    event.pid_pscid = 10;
    event.did_gscid = 0x012349;
    event.pv_pscv = 1;
    event.dv_gscv = 0;
    event.idt = 1;
    event.of = 0;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    write_register(IOHPMCTR4_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 0 ) );
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 0 ) );

    // Inv TLB
    iotinval(VMA, 0, 0, 0, 1, 10, 0);
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 1 ) );

    // Inv TLB
    iotinval(VMA, 0, 0, 0, 1, 10, 0);
    event.pv_pscv = 0;
    event.dv_gscv = 0;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    write_register(IOHPMCTR4_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 0 ) );
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 

    // Inv TLB
    iotinval(VMA, 0, 0, 0, 1, 10, 0);
    event.pv_pscv = 0;
    event.dv_gscv = 1;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    write_register(IOHPMCTR4_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 0xffffffffff ));
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 0xffffffffff ));


    event.pv_pscv = 1;
    event.dv_gscv = 1;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    iotinval(VMA, 0, 0, 0, 1, 10, 0);
    send_translation_request(0x012349, 0, 10, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva, 1, READ, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8)  != 0xffffffffff ));

    for ( i = 0; i < 32; i++ ) {
        write_register(IOHPMEVT1_OFFSET + (i * 8), 8, 0);
        write_register(IOHPMCTR1_OFFSET + (i * 8), 8, 0);
    }

    END_TEST();

    START_TEST("Process Directory Table walk");
    // collapse fault queue
    write_register(FQH_OFFSET, 4, read_register(FQT_OFFSET, 4));
    DC_addr = add_device(0x112233, 0x1234, 0, 0, 0, 0, 0, 
                         1, 1, 0, 0, 0,
                         IOHGATP_Sv48x4, IOSATP_Bare, PD20,
                         MSIPTP_Flat, 1, 0xFFFFFFFFFF, 0x1000000000);
    read_memory(DC_addr, 64, (char *)&DC);
    DC.msiptp.MODE = MSIPTP_Off;
    write_memory((char *)&DC, DC_addr, 64);

    // Invalid non-leaf PDTE
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 266, 0) < 0 ) );

    // Access viol on non-leaf PDTE
    translate_gpa(DC.iohgatp, DC.fsc.pdtp.PPN * PAGESIZE, &temp);
    access_viol_addr = (temp) | (get_bits(19, 17, 0xBABEC) * 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 265, 0) < 0 ) );

    // Data corruption on non-leaf PDTE
    data_corruption_addr = access_viol_addr;
    access_viol_addr = -1;
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 269, 0) < 0 ) );
    data_corruption_addr = -1;


    // Add process context
    memset(&PC, 0, 16);
    PC.fsc.iosatp.MODE = IOSATP_Sv48;
    PC.fsc.iosatp.PPN = get_free_gppn(1, DC.iohgatp);
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
    gpte.PPN = get_free_ppn(1);
    add_g_stage_pte(DC.iohgatp, (PC.fsc.iosatp.PPN * PAGESIZE), gpte, 0);
    PC.ta.V = 1;
    PC.ta.PSCID = 10;
    PC.ta.ENS = 1;
    PC.ta.SUM = 1;
    PC_addr = add_process_context(&DC, &PC, 0xBABEC);
    read_memory(PC_addr, 16, (char *)&PC);

    // misconfigured NL PTE
    translate_gpa(DC.iohgatp, DC.fsc.pdtp.PPN * PAGESIZE, &temp);
    temp = (temp) | (get_bits(19, 17, 0xBABEC) * 8);
    read_memory(temp, 8, (char *)&pdte);
    pdte.reserved0 = 1;
    write_memory((char *)&pdte, temp, 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );
    pdte.reserved0 = 0;
    write_memory((char *)&pdte, temp, 8);

    // Misconfigured PC
    PC.ta.reserved0 = 1;
    write_memory((char *)&PC, PC_addr, 16);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );
    PC.ta.reserved0 = 0;
    write_memory((char *)&PC, PC_addr, 16);
    PC.ta.reserved1 = 1;
    write_memory((char *)&PC, PC_addr, 16);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );
    PC.ta.reserved1 = 0;
    write_memory((char *)&PC, PC_addr, 16);
    PC.ta.reserved0 = 1;
    PC.ta.reserved1 = 1;
    write_memory((char *)&PC, PC_addr, 16);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );
    PC.ta.reserved0 = 0;
    PC.ta.reserved1 = 0;
    write_memory((char *)&PC, PC_addr, 16);

    // Invalid PC
    PC.ta.V = 0;
    write_memory((char *)&PC, PC_addr, 16);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 266, 0) < 0 ) );
    PC.ta.V = 1;
    write_memory((char *)&PC, PC_addr, 16);

    // PC access violation
    access_viol_addr = PC_addr;
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 265, 0) < 0 ) );
    access_viol_addr = -1;
    // PC data corruption violation
    data_corruption_addr = PC_addr;
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 269, 0) < 0 ) );
    data_corruption_addr = -1;

    g_reg_file.capabilities.Sv57 = 0;
    g_reg_file.capabilities.Sv48 = 0;
    g_reg_file.capabilities.Sv39 = 0;
    g_reg_file.capabilities.Sv32 = 0;
    for ( j = 1; j < 16; j++ ) {
        PC.fsc.iosatp.MODE = j;
        write_memory((char *)&PC, PC_addr, 16);
        send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
        fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );
    }
    g_reg_file.capabilities.Sv57 = 1;
    g_reg_file.capabilities.Sv48 = 1;
    g_reg_file.capabilities.Sv39 = 1;
    g_reg_file.capabilities.Sv32 = 1;
    PC.fsc.iosatp.MODE = IOSATP_Sv48;
    write_memory((char *)&PC, PC_addr, 16);

    // guest page fault on PC walk
    gpa = (DC.fsc.pdtp.PPN * PAGESIZE) | (get_bits(19, 17, 0xBABEC) * 8);
    temp = translate_gpa(DC.iohgatp, gpa, &temp);
    read_memory(temp, 8, (char *)&gpte);
    gpte.V = 0;
    write_memory((char *)&gpte, temp, 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0xdeadbeef,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 21, ((gpa & ~0x3UL) | 1)) < 0 ) );
    gpte.V = 1;
    write_memory((char *)&gpte, temp, 8);

    // Two stage translation
    iodir(INVAL_DDT, 1, 0x112233, 0);
    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    pte.PPN = get_free_gppn(1, DC.iohgatp);

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
    gpte.PPN = get_free_ppn(1);
    gpa = pte.PPN * PAGESIZE;
    spa = gpte.PPN * PAGESIZE;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 0);
    gva = 0x100000;
    pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 0, DC.iohgatp);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );

    // fail_if if PC was cached
    PC.fsc.iosatp.reserved = 1;
    write_memory((char *)&PC, PC_addr, 16);
    iotinval(VMA, 1, 1, 1, 0x1234, 10, gva);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );

    // Invalidate PC - DV must be 1
    iodir(INVAL_PDT, 0, 0x112233, 0xBABEC);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_ill != 1 ) );

    // fix the illegal commend 
    cqb.raw = read_register(CQB_OFFSET, 8);
    cqh.raw = read_register(CQH_OFFSET, 4);
    read_memory(((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16, (char *)&cmd);
    cmd.iodir.dv = 1;
    write_memory((char *)&cmd, ((cqb.ppn * PAGESIZE) | (cqh.index * 16)), 16);
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);

    // Process the fixed up command
    process_commands();

    // should observe misconiguration
    iotinval(VMA, 1, 1, 1, 0x1234, 10, gva);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 267, 0) < 0 ) );

    // Fix PC Translation should succeed
    PC.fsc.iosatp.reserved = 0;
    write_memory((char *)&PC, PC_addr, 16);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );

    // Check A/D update from TLB
    read_memory(pte_addr, 8, (char *)&pte);
    pte.A = pte.D = 0;
    write_memory((char *)&pte, pte_addr, 8);
    iotinval(VMA, 1, 1, 1, 0x1234, 10, gva);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    read_memory(pte_addr, 8, (char *)&pte);
    fail_if( ( pte.A == 0 ) );
    fail_if( ( pte.D == 1 ) );
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    read_memory(pte_addr, 8, (char *)&pte);
    fail_if( ( pte.A == 0 ) );
    fail_if( ( pte.D == 0 ) );

    // transaction with no PASID
    send_translation_request(0x112233, 0, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gpa,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != (spa / PAGESIZE) ) );

    // Test T2GPA
    DC.tc.T2GPA = 1;
    DC.tc.EN_ATS = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != (gpa / PAGESIZE) ) );
    send_translation_request(0x112233, 0, 0xBABEC, 0,
             0, 1, 0, TRANSLATED_REQUEST, gpa,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != (spa / PAGESIZE) ) );

    // Disable T2GPA and test 2 stage translation
    DC.tc.T2GPA = 0;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != (spa / PAGESIZE) ) );
    send_translation_request(0x112233, 0, 0xBABEC, 0,
             0, 1, 0, TRANSLATED_REQUEST, spa,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != (spa / PAGESIZE) ) );

    // Disable supervisory requests
    PC.ta.ENS = 0;
    write_memory((char *)&PC, PC_addr, 64);
    iodir(INVAL_PDT, 1, 0x112233, 0xBABEC);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );

    // Disable supv to user access
    PC.ta.ENS = 1;
    PC.ta.SUM = 0;
    write_memory((char *)&PC, PC_addr, 64);
    iodir(INVAL_PDT, 1, 0x112233, 0xBABEC);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.R != 0 ) );
    fail_if( ( rsp.trsp.W != 0 ) );
    fail_if( ( rsp.trsp.Exe != 0 ) );

    // Cause a PDT access fault
    access_viol_addr = PC_addr;
    iodir(INVAL_PDT, 1, 0x112233, 0xBABEC);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, COMPLETER_ABORT, 0, 0) < 0 ) );
    access_viol_addr = -1;
    iodir(INVAL_PDT, 1, 0x112233, 0xBABEC);

    // Too wide PID and invalid PDTP mode
    DC.fsc.pdtp.MODE = PD8;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    DC.fsc.pdtp.MODE = PD17;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    DC.fsc.pdtp.MODE = 9;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 0, 0) < 0 ) );
    DC.fsc.pdtp.MODE = PD20;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);

    // Do a napot PTE
    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.N = 1;
    pte.PBMT = PMA;
    pte.PPN = get_free_gppn(16, DC.iohgatp);
    gpa = pte.PPN * PAGESIZE;
    pte.PPN |= 0x8;

    gpte.raw = 0;
    gpte.V = 1;
    gpte.R = 1;
    gpte.W = 1;
    gpte.X = 1;
    gpte.U = 1;
    gpte.G = 0;
    gpte.A = 0;
    gpte.D = 0;
    gpte.N = 1;
    gpte.PBMT = PMA;
    gpte.PPN = get_free_ppn(16);
    spa = gpte.PPN * PAGESIZE;
    gpte.PPN |= 0x8;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 0);
    gva = 0x900000;
    pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 0, DC.iohgatp);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.S != 0 ) );
    fail_if( ( rsp.trsp.PPN != (gpte.PPN & ~0x8)) );

    // Guest-Page fault on NL S-stage PTE
    gpte_addr = translate_gpa(DC.iohgatp, (PC.fsc.iosatp.PPN * PAGESIZE), &temp);
    gpte.raw = read_memory(gpte_addr, 8, (char *)&gpte);
    gpte.V = 0;
    write_memory((char *)&gpte.raw, gpte_addr, 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 
               ((PC.fsc.iosatp.PPN * PAGESIZE) | 3)) < 0 ) );

    gpte.V = 1;
    gpte.N = 1;
    write_memory((char *)&gpte.raw, gpte_addr, 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 
               ((PC.fsc.iosatp.PPN * PAGESIZE) | 3)) < 0 ) );
    gpte.N = 0;
    gpte.PBMT = 1;
    write_memory((char *)&gpte.raw, gpte_addr, 8);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 23, 
               ((PC.fsc.iosatp.PPN * PAGESIZE) | 3)) < 0 ) );
    gpte.N = 0;
    gpte.PBMT = 0;
    write_memory((char *)&gpte.raw, gpte_addr, 8);

    iodir(INVAL_DDT, 1, 0x112233, 0);
    iotinval(GVMA, 1, 0, 0, 0x1234, 0, 0);
    for ( i = 0; i < 10; i++ ) {
        // Add process context
        memset(&PC, 0, 16);
        PC.fsc.iosatp.MODE = IOSATP_Sv48;
        PC.fsc.iosatp.PPN = get_free_gppn(1, DC.iohgatp);
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
        gpte.PPN = get_free_ppn(1);
        add_g_stage_pte(DC.iohgatp, (PC.fsc.iosatp.PPN * PAGESIZE), gpte, 0);
        PC.ta.V = 1;
        PC.ta.PSCID = 100+i;
        PC.ta.ENS = 1;
        PC.ta.SUM = 1;
        PC_addr = add_process_context(&DC, &PC, 0x1000+i);
        read_memory(PC_addr, 16, (char *)&PC);
        pte.raw = 0;
        pte.V = 1;
        pte.R = 1;
        pte.W = 1;
        pte.X = 1;
        pte.U = 1;
        pte.G = 0;
        pte.A = 0;
        pte.D = 0;
        pte.PBMT = PMA;
        pte.PPN = get_free_gppn(1, DC.iohgatp);

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
        gpte.PPN = get_free_ppn(1);
        gpa = pte.PPN * PAGESIZE;
        spa = gpte.PPN * PAGESIZE;
        gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 0);
        gva = 0x100000;
        pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 0, DC.iohgatp);
    }
    for ( i = 0; i < 10; i++ ) {
        send_translation_request(0x112233, 1, 0x1000+i, 0,
                 0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
                 1, WRITE, &req, &rsp);
        fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    }
    iodir(INVAL_DDT, 1, 0x112233, 0);
    iotinval(GVMA, 1, 0, 0, 0x1234, 0, 0);

    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET,4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.pmip == 1 ) );

    gva = 0x100000;
    event.eventID = IOATC_TLB_MISS;
    event.dmask = 0;
    event.pid_pscid = 100;
    event.did_gscid = 0x1234;
    event.idt = 1;
    event.of = 1;

    event.pv_pscv = 0;
    event.dv_gscv = 0;
    write_register(IOHPMEVT1_OFFSET, 8, event.raw);
    write_register(IOHPMCTR1_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    event.pv_pscv = 1;
    event.dv_gscv = 0;
    write_register(IOHPMEVT2_OFFSET, 8, event.raw);
    write_register(IOHPMCTR2_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    event.pv_pscv = 0;
    event.dv_gscv = 1;
    write_register(IOHPMEVT3_OFFSET, 8, event.raw);
    write_register(IOHPMCTR3_OFFSET, 8, 0xFFFFFFFFFFFFFFFF);
    event.pv_pscv = 1;
    event.dv_gscv = 1;
    write_register(IOHPMEVT4_OFFSET, 8, event.raw);
    write_register(IOHPMCTR4_OFFSET, 8, 0x0);
    event.dmask = 1;
    event.did_gscid = 0x127f;
    write_register(IOHPMEVT5_OFFSET, 8, event.raw);
    write_register(IOHPMCTR5_OFFSET, 8, 0x9);

    send_translation_request(0x112233, 1, 0x1000, 0,
                 0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
                 1, WRITE, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR1_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR2_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR3_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 1 ) );
    fail_if( ( read_register(IOHPMCTR5_OFFSET, 8) != 0xa ) );

    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.pmip == 1 ) );
    g_reg_file.capabilities.hpm = 0;

    send_translation_request(0x112233, 1, 0x1000, 0,
                 0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
                 1, WRITE, &req, &rsp);
    fail_if( ( rsp.status != SUCCESS ) ); 
    fail_if( ( read_register(IOHPMCTR1_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR2_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR3_OFFSET, 8) != 0 ) );
    fail_if( ( read_register(IOHPMCTR4_OFFSET, 8) != 1 ) );

    for ( i = 0; i < 32; i++ ) {
        write_register(IOHPMEVT1_OFFSET + (i * 8), 8, 0);
        write_register(IOHPMCTR1_OFFSET + (i * 8), 8, 0);
    }
//HERE
    // Two stage translation with default process ID
    // Enable default process
    DC.tc.DPE = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);

    // transaction with no PASID - should fail as no default process context
    send_translation_request(0x112233, 0, 0, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, 0x10000,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 266, 0) < 0 ) );

#if 0

    // Add process context
    memset(&PC, 0, 16);
    PC.fsc.iosatp.MODE = IOSATP_Sv48;
    PC.fsc.iosatp.PPN = get_free_gppn(1, DC.iohgatp);
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
    gpte.PPN = get_free_ppn(1);
    add_g_stage_pte(DC.iohgatp, (PC.fsc.iosatp.PPN * PAGESIZE), gpte, 0);
    PC.ta.V = 1;
    PC.ta.PSCID = 10;
    PC.ta.ENS = 1;
    PC.ta.SUM = 1;
    PC_addr = add_process_context(&DC, &PC, 0xBABEC);
    read_memory(PC_addr, 16, (char *)&PC);

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    pte.PPN = get_free_gppn(1, DC.iohgatp);

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
    gpte.PPN = get_free_ppn(1);
    gpa = pte.PPN * PAGESIZE;
    spa = gpte.PPN * PAGESIZE;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 0);
    gva = 0x100000;
    pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 0, DC.iohgatp);
    send_translation_request(0x112233, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
#endif
    END_TEST();

    START_TEST("ATS page request group response");
    exp_msg.MSGCODE = PRGR_MSG_CODE;
    exp_msg.TAG = 0;
    exp_msg.RID = 0x1234;
    exp_msg.PV = 1;
    exp_msg.PID = 0xbabec;
    exp_msg.PRIV = 0;
    exp_msg.EXEC_REQ = 0;
    exp_msg.DSV = 1;
    exp_msg.DSEG = 0x43;
    exp_msg.PAYLOAD = 0xdeadbeeffeedbeef;
    ats_command(PRGR, 1, 1, 0xbabec, 0x43, 0x1234, 0xdeadbeeffeedbeef);
    fail_if( ( exp_msg_received == 0 ) );
    exp_msg.PV = 0;
    ats_command(PRGR, 1, 0, 0xbabec, 0x43, 0x1234, 0xdeadbeeffeedbeef);
    fail_if( ( exp_msg_received == 0 ) );
    END_TEST();
   
    START_TEST("ATS page request");
    // Invalid device_id
    pr.MSGCODE = PAGE_REQ_MSG_CODE;
    pr.TAG = 0;
    pr.RID = 0x1234;
    pr.PV = 1;
    pr.PID = 0xbabec;
    pr.PRIV = 1;
    pr.EXEC_REQ = 0;
    pr.DSV = 1;
    pr.DSEG = 0x43;
    pr.PAYLOAD = 0xdeadbeef00000007; // Set last, PRG index = 0
    exp_msg.MSGCODE = PRGR_MSG_CODE;
    exp_msg.TAG = 0;
    exp_msg.RID = 0x1234;
    exp_msg.PV = 1;
    exp_msg.PID = 0xbabec;
    exp_msg.PRIV = 0;
    exp_msg.EXEC_REQ = 0;
    exp_msg.DSV = 1;
    exp_msg.DSEG = 0x43;
    exp_msg.PAYLOAD = (0x1234UL << 48UL) | (RESPONSE_FAILURE << 44UL);
    handle_page_request(&pr);
    fail_if( ( exp_msg_received == 0 ) );
    fail_if( ( check_msg_faults(258, pr.PV, pr.PID, pr.PRIV, 0x431234, PAGE_REQ_MSG_CODE) < 0 ) );

    // ATS disabled
    pr.RID = 0x2233;
    pr.DSEG = 0x11;
    DC.tc.EN_ATS = 0;
    DC.tc.EN_PRI = 0;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    exp_msg.RID = 0x2233;
    exp_msg.DSEG = 0x11;
    exp_msg.PV = 0;
    exp_msg.PID = 0;
    exp_msg.PAYLOAD = (0x2233UL << 48UL) | (INVALID_REQUEST << 44UL);
    handle_page_request(&pr);
    fail_if( ( exp_msg_received == 0 ) );
    fail_if( ( check_msg_faults(260, pr.PV, pr.PID, pr.PRIV, 0x112233, PAGE_REQ_MSG_CODE) < 0 ) );

    // ATS enabled PRI disabled
    pr.RID = 0x2233;
    pr.DSEG = 0x11;
    DC.tc.EN_ATS = 1;
    DC.tc.EN_PRI = 0;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    exp_msg.RID = 0x2233;
    exp_msg.DSEG = 0x11;
    exp_msg.PV = 0;
    exp_msg.PID = 0;
    exp_msg.PAYLOAD = (0x2233UL << 48UL) | (INVALID_REQUEST << 44UL);
    handle_page_request(&pr);
    fail_if( ( exp_msg_received == 0 ) );
    fail_if( ( check_msg_faults(260, pr.PV, pr.PID, pr.PRIV, 0x112233, PAGE_REQ_MSG_CODE) < 0 ) );


    // ATS, PRI enabled - Page request queue disabled
    fail_if( ( enable_disable_pq(4, 0) < 0 ) );
    DC.tc.EN_ATS = 1;
    DC.tc.EN_PRI = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    exp_msg.RID = 0x2233;
    exp_msg.DSEG = 0x11;
    exp_msg.PV = 1;
    exp_msg.PID = 0xBABEC;
    exp_msg.PAYLOAD = (0x2233UL << 48UL) | (RESPONSE_FAILURE << 44UL);
    handle_page_request(&pr);
    fail_if( ( exp_msg_received == 0 ) );
    fail_if( ( ((read_register(PQH_OFFSET, 4)) != read_register(PQT_OFFSET, 4)) ) );
    fail_if( ( enable_disable_pq(4, 1) < 0 ) );

    // PRI enabled - should queue in PQ
    pr.RID = 0x2233;
    pr.DSEG = 0x11;
    DC.tc.EN_ATS = 1;
    DC.tc.EN_PRI = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    message_received = 0;
    handle_page_request(&pr);
    fail_if( ( message_received == 1 ) );
   
    // Create a overflow case
    write_register(PQH_OFFSET, 4, read_register(PQT_OFFSET, 4)+1);
    pr.RID = 0x2233;
    pr.DSEG = 0x11;
    DC.tc.EN_ATS = 1;
    DC.tc.EN_PRI = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    message_received = 0;
    exp_msg.RID = 0x2233;
    exp_msg.DSEG = 0x11;
    exp_msg.PV = 0;
    exp_msg.PID = 0;
    exp_msg.PAYLOAD = (0x2233UL << 48UL) | (SUCCESS << 44UL);
    handle_page_request(&pr);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    // Set PRPR
    DC.tc.PRPR = 1;
    write_memory((char *)&DC, DC_addr, 64);
    iodir(INVAL_DDT, 1, 0x112233, 0);
    exp_msg.PV = 1;
    exp_msg.PID = 0xBABEC;
    handle_page_request(&pr);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    // PR without LPIG
    pr.PAYLOAD = 0xdeadbeef00000003; // Set last = 0, PRG index = 0
    message_received = 0;
    handle_page_request(&pr);
    fail_if( ( message_received == 1 ) );

    // Clear overflow - and observe logging
    write_register(PQH_OFFSET, 4, read_register(PQT_OFFSET, 4));
    write_register(PQCSR_OFFSET, 4, read_register(PQCSR_OFFSET, 4));
    message_received = 0;
    handle_page_request(&pr);
    fail_if( ( message_received == 1 ) );
    fail_if( ( read_register(PQH_OFFSET, 4) == read_register(PQT_OFFSET, 4) ) );
    fail_if( ( check_exp_pq_rec(0x112233, 0xBABEC, 1, 1, 0, 0, 0, pr.PAYLOAD) < 0 ) );

    // cause memory fault
    pqb.raw = read_register(PQB_OFFSET, 8);
    access_viol_addr = (pqb.ppn * PAGESIZE);
    access_viol_addr += (read_register(PQT_OFFSET, 4) * 16);
    pr.PAYLOAD = 0xdeadbeef00000007; // Set last = 1, PRG index = 0
    exp_msg.PAYLOAD = (0x2233UL << 48UL) | (RESPONSE_FAILURE << 44UL);
    message_received = 0;
    handle_page_request(&pr);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );
    // Send another
    message_received = 0;
    handle_page_request(&pr);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET,4, ipsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.pip == 0 ) );
    access_viol_addr = -1;

    // disable page queue interrupt
    pqcsr.raw = read_register(PQCSR_OFFSET, 4);
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    write_register(IPSR_OFFSET,4, ipsr.raw);

    pqcsr.pie = 0;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    handle_page_request(&pr);
    ipsr.raw = read_register(IPSR_OFFSET, 4);
    fail_if( ( ipsr.pip == 1 ) );
    pqcsr.pqen = 0;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);
    pqcsr.pqen = 1;
    write_register(PQCSR_OFFSET, 4, pqcsr.raw);

    END_TEST();

    START_TEST("ATS inval request");
    // Send a spurious completion
    inv_cc.MSGCODE = INVAL_COMPL_MSG_CODE;
    inv_cc.TAG = 0;
    inv_cc.RID = 0x9988;
    inv_cc.PV = 0;
    inv_cc.PID = 0;
    inv_cc.PRIV = 0;
    inv_cc.EXEC_REQ = 0;
    inv_cc.DSV = 1;
    inv_cc.DSEG = 0x43;
    inv_cc.PAYLOAD = (0x9988UL << 48UL) | (3UL << 32UL) | 0x00000001UL;
    fail_if( ( handle_invalidation_completion(&inv_cc) == 0 ) );

    // send one - itag should be 0
    exp_msg.MSGCODE = INVAL_REQ_MSG_CODE;
    exp_msg.TAG = 0;
    exp_msg.RID = 0x1234;
    exp_msg.PV = 0;
    exp_msg.PID = 0;
    exp_msg.PRIV = 0;
    exp_msg.EXEC_REQ = 0;
    exp_msg.DSV = 1;
    exp_msg.DSEG = 0x43;
    exp_msg.PAYLOAD = 0x1234000000000000;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );
    // send another - itag should be 1
    exp_msg.TAG = 1;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    // Fence it - fence should block
    iofence_PPN = get_free_ppn(1);
    iofence_data = 0x1234567812345678;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);
    pr_go_requested = 0;
    pw_go_requested = 0;
    iofence(IOFENCE_C, 1, 1, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    // Fence should not complete
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( pr_go_requested == 1) );
    fail_if( ( pw_go_requested == 1) );

    // Send one more - this should get stuck behind the IOFENCE
    exp_msg.TAG = 2;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 1 ) );
    // Fence should still not complete
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( pr_go_requested == 1 ) );
    fail_if( ( pw_go_requested == 1 ) );
 
    // Send a invalid completion with itag of first inval
    inv_cc.MSGCODE = INVAL_COMPL_MSG_CODE;
    inv_cc.TAG = 0;
    inv_cc.RID = 0x9988;
    inv_cc.PV = 0;
    inv_cc.PID = 0;
    inv_cc.PRIV = 0;
    inv_cc.EXEC_REQ = 0;
    inv_cc.DSV = 1;
    inv_cc.DSEG = 0x43;
    inv_cc.PAYLOAD = (0x9988UL << 48UL) | (3UL << 32UL) | 0x00000001UL;
    fail_if( ( handle_invalidation_completion(&inv_cc) != 1 ) );
    inv_cc.RID = 0x1234;
    inv_cc.DSEG = 0x99;
    // Send a invalid completion with itag of second inval
    inv_cc.PAYLOAD = (0x9988UL << 48UL) | (3UL << 32UL) | 0x00000002UL;
    fail_if( ( handle_invalidation_completion(&inv_cc) != 1 ) );

    // Send completion for second itag with 3 completions
    inv_cc.RID = 0x1234;
    inv_cc.DSEG = 0x43;
    inv_cc.PAYLOAD = (0x9988UL << 48UL) | (3UL << 32UL) | 0x00000002UL;
    for ( i = 0; i < 3; i++ ) {
        fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
        // Fence should still not complete
        read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
        fail_if( ( iofence_data != 0x1234567812345678 )  );
        fail_if( ( pr_go_requested == 1 ) );
        fail_if( ( pw_go_requested == 1 ) );
    }

    // Send completion for second itag with 8 completions
    inv_cc.RID = 0x1234;
    inv_cc.DSEG = 0x43;
    inv_cc.PAYLOAD = (0x9988UL << 48UL) | (0UL << 32UL) | 0x00000001UL;
    for ( i = 0; i < 7; i++ ) {
        fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
        // Fence should still not complete
        read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
        fail_if( ( iofence_data != 0x1234567812345678 )  );
        fail_if( ( pr_go_requested == 1 ) );
        fail_if( ( pw_go_requested == 1 ) );
    }

    // Send the 8th one
    fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
    // Fence should complete
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x12345678deadbeef )  );
    fail_if( ( pr_go_requested != 1 ) );
    fail_if( ( pw_go_requested != 1 ) );
    fail_if( ( message_received != 0 ) );

    // The pending one should come out and with tag of 0 on next clock
    exp_msg.TAG = 0;
    message_received = 0;
    process_commands();
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    // Fence it - fence should block
    iofence_data = 0x1234567812345678;
    write_memory((char *)&iofence_data, (iofence_PPN * PAGESIZE), 8);
    pr_go_requested = 0;
    pw_go_requested = 0;
    iofence(IOFENCE_C, 1, 1, 1, 0, (iofence_PPN * PAGESIZE), 0xDEADBEEF);
    // Fence should not complete
    read_memory((iofence_PPN * PAGESIZE), 8, (char *)&iofence_data);
    fail_if( ( iofence_data != 0x1234567812345678 )  );
    fail_if( ( pr_go_requested == 1) );
    fail_if( ( pw_go_requested == 1) );

    i = read_register(CQH_OFFSET, 4);

    // Make it timeout
    do_ats_timer_expiry(0x00000001);
    // Command queue should timeout
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_to != 1 ) );
    // Queue a command - should not be picked up
    // head register must not move
    iodir(INVAL_DDT, 1, 0x112233, 0);
    fail_if( ( i != read_register(CQH_OFFSET, 4) ) );

    // clear the cmd_to bit
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    // clean up the command queue
    write_register(CQT_OFFSET, 4, i);

    // test running out of itags
    exp_msg.TAG = 0;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );
    exp_msg.TAG = 1;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );
    // Next two should block
    exp_msg.TAG = 1;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 1 ) );
    exp_msg.TAG = 0;
    message_received = 0;
    ats_command(INVAL, 1, 0, 0, 0x43, 0x1234, 0x1234000000000000);
    fail_if( ( message_received == 1 ) );

    // Complete first two
    inv_cc.PAYLOAD = (0x1234UL << 48UL) | (2UL << 32UL) | 0x00000003UL;
    fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
    fail_if( ( message_received == 1 ) );
    fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    message_received = 0;
    exp_msg.TAG = 1;
    process_commands();
    fail_if( ( message_received == 0 ) );
    fail_if( ( exp_msg_received == 0 ) );

    // Complete next two
    inv_cc.PAYLOAD = (0x1234UL << 48UL) | (1UL << 32UL) | 0x00000003UL;
    fail_if( ( handle_invalidation_completion(&inv_cc) != 0 ) );
    END_TEST();

    START_TEST("MSI write-through mode");

    DC_addr = add_device(0x042874, 0x1974, 0, 0, 0, 0, 0, 
                         1, 1, 0, 0, 0,
                         IOHGATP_Sv48x4, IOSATP_Bare, PDTP_Bare,
                         MSIPTP_Flat, 1, 0x0000000FF, 0x280000000);
    read_memory(DC_addr, 64, (char *)&DC);
    gpa = 0x280000003000;

    // Cause a access violation
    access_viol_addr = (DC.msiptp.PPN * PAGESIZE) + 3 * 16;
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 261, 0) < 0 ) );
    access_viol_addr = -1;

    data_corruption_addr = (DC.msiptp.PPN * PAGESIZE) + 3 * 16;
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 270, 0) < 0 ) );
    data_corruption_addr = -1;

    // Not present
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 262, 0) < 0 ) );

    // Misconfigured
    msipte_t msipte;
    msipte.V = 1;
    msipte.M = 0x0;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );

    msipte.C = 1;
    msipte.M = 0x0;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );

    msipte.C = 0;
    msipte.M = 2;
    msipte.translate_rw.reserved = 0x1;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );
    msipte.translate_rw.reserved = 0x0;
    msipte.translate_rw.reserved = 0x4;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );

    // Write through PTE
    msipte.translate_rw.reserved = 0x0;
    msipte.translate_rw.M = 0x3;
    msipte.translate_rw.PPN = 0xdeadbeef;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != 0xdeadbeef ) );
    fail_if( ( rsp.trsp.is_msi != 1 ) );
    fail_if( ( rsp.trsp.S != 0 ) );
    fail_if( ( rsp.trsp.PBMT != PMA ) );
    fail_if( ( rsp.trsp.is_msi != 1 ) );
    fail_if( ( rsp.trsp.is_mrif != 0 ) );

    // corrupt but hit from cache
    msipte.translate_rw.reserved = 0x1;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != 0xdeadbeef ) );
    fail_if( ( rsp.trsp.is_msi != 1 ) );
    fail_if( ( rsp.trsp.S != 0 ) );
    fail_if( ( rsp.trsp.PBMT != PMA ) );
    fail_if( ( rsp.trsp.is_msi != 1 ) );
    fail_if( ( rsp.trsp.is_mrif != 0 ) );

    // Invalidate TLB
    iotinval(GVMA, 1, 1, 0, 0x1974, 0, gpa);
    send_translation_request(0x042874, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );
    msipte.translate_rw.reserved = 0x1;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);
    msipte.translate_rw.reserved = 0x0;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 3 * 16), 16);

    END_TEST();

    START_TEST("MSI MFIF mode");
    DC_addr = add_device(0x121679, 0x1979, 1, 0, 0, 0, 0,
                         1, 1, 0, 0, 0,
                         IOHGATP_Sv48x4, IOSATP_Bare, PDTP_Bare,
                         MSIPTP_Flat, 1, 0x0000000FF, 0x280000000);
    read_memory(DC_addr, 64, (char *)&DC);
    msipte.mrif.V = 1;
    msipte.mrif.reserved1 = 0;
    msipte.mrif.M = 1;
    msipte.mrif.reserved1 = 0;
    msipte.mrif.MRIF_ADDR_55_9 = ((get_free_ppn(1) * PAGESIZE) / 512);
    msipte.mrif.reserved2 = 0;
    msipte.mrif.C = 0;
    msipte.mrif.N90 = 0x12;
    msipte.mrif.NPPN = 0xdeadbeef;
    msipte.mrif.reserved3 = 0;
    msipte.mrif.N10 = 0x1;
    msipte.mrif.reserved4 = 0;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 0x23 * 16), 16);

    gpa = 0x280000023000;

    // Disable MRIF
    g_reg_file.capabilities.msi_mrif = 0;
    send_translation_request(0x121679, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );
    g_reg_file.capabilities.msi_mrif = 1;

    // misconfigured
    msipte.mrif.reserved1 = 1;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 0x23 * 16), 16);
    send_translation_request(0x121679, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, UNSUPPORTED_REQUEST, 263, 0) < 0 ) );
    msipte.mrif.reserved1 = 0;
    write_memory((char *)&msipte, ((DC.msiptp.PPN * PAGESIZE) + 0x23 * 16), 16);

    // Unsupported
    send_translation_request(0x121679, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST, gpa, 4, READ, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.U != 1 ) ); 
    fail_if( ( rsp.trsp.R != 1 ) ); 
    fail_if( ( rsp.trsp.W != 1 ) ); 

    // Success
    send_translation_request(0x121679, 0, 0x0000, 0,
             0, 0, 0, ADDR_TYPE_UNTRANSLATED, gpa, 4, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    fail_if( ( rsp.trsp.PPN != 0xdeadbeef ) );
    fail_if( ( rsp.trsp.S != 0 ) );
    fail_if( ( rsp.trsp.PBMT != PMA ) );
    fail_if( ( rsp.trsp.is_msi != 1 ) );
    fail_if( ( rsp.trsp.is_mrif != 1 ) );
    fail_if( ( rsp.trsp.mrif_nid != 0x412 ) );
    fail_if( ( rsp.trsp.mrif_nid != 0x412 ) );
    fail_if( ( rsp.trsp.dest_mrif_addr != (msipte.mrif.MRIF_ADDR_55_9 * 512)) );

    END_TEST();

    START_TEST("Illegal commands and CQ mem faults");
    // illegal command
    cmd.any.opcode = 9;
    cmd.any.func3 = 0;
    generic_any(cmd);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_ill != 1 ) );
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqon != 1 ) );
    fail_if( ( cqcsr.cmd_ill != 0 ) );

    for ( i = 0; i < 64; i++ ) {
        cmd.high = 0;
        cmd.low = 0;
        temp = 0;
        for ( j = 0; j < 64; j++ ) {
            cmd.low = 1 << i;
            cmd.high = 1 << j;
            cmd.any.opcode = i;
            if ( cmd.any.opcode == IOTINVAL ) {
                if ( cmd.iotinval.rsvd == 0 &&
                     cmd.iotinval.rsvd1 == 0 &&
                     cmd.iotinval.rsvd2 == 0 &&
                     cmd.iotinval.rsvd3 == 0 &&
                     cmd.iotinval.rsvd4 == 0 ) {
                    if ( temp == 0 ) 
                        cmd.iotinval.func3 = 0x7;
                    if ( temp == 1 ) 
                        cmd.iotinval.rsvd = 1;
                    if ( temp == 2 ) 
                        cmd.iotinval.rsvd1 = 1;
                    if ( temp == 3 ) 
                        cmd.iotinval.rsvd2 = 1;
                    if ( temp == 4 ) 
                        cmd.iotinval.rsvd3 = 1;
                    if ( temp == 5 ) 
                        cmd.iotinval.rsvd4 = 1;
                    if ( temp > 5 )  {
                        cmd.iotinval.rsvd4 = 1;
                        cmd.iotinval.rsvd3 = 1;
                        cmd.iotinval.rsvd2 = 1;
                        cmd.iotinval.rsvd1 = 1;
                        cmd.iotinval.rsvd = 1;
                    }
                    temp++;
                }
            }
            if ( cmd.any.opcode == IOFENCE ) {
                if ( cmd.iofence.reserved == 0 &&
                     cmd.iofence.reserved1 == 0 ) {
                    if ( temp == 0 ) 
                        cmd.iofence.func3 = 0x7;
                    if ( temp == 1 ) 
                        cmd.iofence.reserved = 1;
                    if ( temp == 2 ) 
                        cmd.iofence.reserved1 = 1;
                    if ( temp > 2 ) { 
                        cmd.iofence.reserved = 1;
                        cmd.iofence.reserved1 = 1;
                    }
                    temp++;
                }
            }
            if ( cmd.any.opcode == IODIR ) {
                if ( cmd.iodir.rsvd == 0 &&
                     cmd.iodir.rsvd1 == 0 &&
                     cmd.iodir.rsvd2 == 0 ) {
                    if ( temp == 0 ) 
                        cmd.iodir.func3 = 0x7;
                    if ( temp == 1 ) 
                        cmd.iodir.rsvd = 1;
                    if ( temp == 2 ) 
                        cmd.iodir.rsvd1 = 1;
                    if ( temp == 3 ) 
                        cmd.iodir.rsvd2 = 1;
                    if ( temp > 3 )  {
                        cmd.iodir.rsvd2 = 1;
                        cmd.iodir.rsvd1 = 1;
                        cmd.iodir.rsvd = 1;
                    }
                    temp++;
                }
            }
            if ( cmd.any.opcode == ATS && cmd.ats.rsvd == 0 && 
                 cmd.ats.rsvd1 == 0) 
                cmd.ats.func3 = 0x7;
            generic_any(cmd);
            cqcsr.raw = read_register(CQCSR_OFFSET, 4);
            fail_if( ( cqcsr.cmd_ill != 1 ) );
            cqcsr.cqen = 0;
            write_register(CQCSR_OFFSET, 4, cqcsr.raw);
            cqcsr.cqen = 1;
            write_register(CQCSR_OFFSET, 4, cqcsr.raw);
            cqcsr.raw = read_register(CQCSR_OFFSET, 4);
            fail_if( ( cqcsr.cqon != 1 ) );
            fail_if( ( cqcsr.cmd_ill != 0 ) );
        }
    }
    cmd.high = 0;
    cmd.low = 0;
    cmd.any.opcode = IODIR;
    cmd.any.func3 = 7;
    generic_any(cmd);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cmd_ill != 1 ) );
    cqcsr.cqen = 0;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.cqen = 1;
    write_register(CQCSR_OFFSET, 4, cqcsr.raw);
    cqcsr.raw = read_register(CQCSR_OFFSET, 4);
    fail_if( ( cqcsr.cqon != 1 ) );
    fail_if( ( cqcsr.cmd_ill != 0 ) );

    // idle 
    process_commands();

    END_TEST();

    START_TEST("Sv32 mode");
    // Change IOMMU mode to base device context
    g_reg_file.capabilities.msi_flat = 0;

    // Allow selection of Sv32
    g_gxl_writeable = 1;
    g_reg_file.fctl.gxl = 1;

    DC_addr = add_device(0x000000, 1, 0, 0, 0, 0, 0, 
                         1, 1, 0, 0, 1,
                         IOHGATP_Sv32x4, IOSATP_Bare, PD20,
                         MSIPTP_Flat, 1, 0xFFFFFFFFFF, 0x1000000000);
    read_memory(DC_addr, 64, (char *)&DC);

    // Add process context
    memset(&PC, 0, 16);
    PC.fsc.iosatp.MODE = IOSATP_Sv32;
    PC.fsc.iosatp.PPN = get_free_gppn(1, DC.iohgatp);
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
    gpte.PPN = get_free_ppn(1);
    add_g_stage_pte(DC.iohgatp, (PC.fsc.iosatp.PPN * PAGESIZE), gpte, 0);
    PC.ta.V = 1;
    PC.ta.PSCID = 10;
    PC.ta.ENS = 1;
    PC.ta.SUM = 1;
    PC_addr = add_process_context(&DC, &PC, 0xBABEC);
    read_memory(PC_addr, 16, (char *)&PC);

    pte.raw = 0;
    pte.V = 1;
    pte.R = 1;
    pte.W = 1;
    pte.X = 1;
    pte.U = 1;
    pte.G = 0;
    pte.A = 0;
    pte.D = 0;
    pte.PBMT = PMA;
    pte.PPN = get_free_gppn(1, DC.iohgatp);

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
    gpte.PPN = get_free_ppn(1);
    gpa = pte.PPN * PAGESIZE;
    spa = gpte.PPN * PAGESIZE;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 0);
    gva = 0x100000;
    pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 0, DC.iohgatp);
    send_translation_request(0x000000, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );


    // Make a large page PTE
    pte.PPN = get_free_gppn(1024, DC.iohgatp);
    gpte.PPN = get_free_ppn(1024);
    gpa = pte.PPN * PAGESIZE;
    spa = gpte.PPN * PAGESIZE;
    gpte_addr = add_g_stage_pte(DC.iohgatp, gpa, gpte, 1);
    gva = 0x19000000;
    pte_addr = add_vs_stage_pte(PC.fsc.iosatp, gva, pte, 1, DC.iohgatp);
    send_translation_request(0x000000, 1, 0xBABEC, 0,
             0, 1, 0, ADDR_TYPE_UNTRANSLATED, gva,
             1, WRITE, &req, &rsp);
    fail_if( ( check_rsp_and_faults(&req, &rsp, SUCCESS, 0, 0) < 0 ) );
    END_TEST();
    
    START_TEST("Misc. Register Access tests");
   
    // Write read-only registers 
    for ( offset = 0; offset < 4096; ) {
        i = offset;
        switch (offset) {
            case CAPABILITIES_OFFSET:
                // This register is read only
                temp = read_register(i, 8);
                write_register(i, 8, 0xFF);
                fail_if( ( temp != read_register(i, 8) ) );
                offset += 8;
                break;
            case FCTRL_OFFSET:
                fctl.raw = read_register(i, 4);
                fctl.be = 1;
                fctl.wis = 1;
                write_register(i, 4, fctl.raw);
                fctl.raw = read_register(i, 4);
                fail_if( ( fctl.be != 0 ) );
                fail_if( ( fctl.wis != 0 ) );
                g_reg_file.capabilities.end = BOTH_END;
                fctl.raw = read_register(i, 4);
                temp = fctl.raw;
                fail_if( ( fctl.be != 0 ) );
                fail_if( ( fctl.wis != 0 ) );
                // IOMMU is on - this register should not be writeable 
                fctl.be = 1;
                fctl.wis = 1;
                write_register(i, 4, fctl.raw);
                fail_if( ( temp != read_register(i, 4) ) );
                fail_if( ( enable_iommu(Off) < 0 ) );
                // QUeues are On - register must not be writeable
                write_register(i, 4, fctl.raw);
                fail_if( ( temp != read_register(i, 4) ) );
                pqcsr.raw = read_register(PQCSR_OFFSET, 4);
                pqcsr.pqen = 0;
                write_register(PQCSR_OFFSET, 4, pqcsr.raw);
                write_register(i, 4, fctl.raw);
                fail_if( ( temp != read_register(i, 4) ) );
                fqcsr.raw = read_register(FQCSR_OFFSET, 4);
                fqcsr.fqen = 0;
                write_register(FQCSR_OFFSET, 4, fqcsr.raw);
                write_register(i, 4, fctl.raw);
                fail_if( ( temp != read_register(i, 4) ) );
                cqcsr.raw = read_register(CQCSR_OFFSET, 4);
                cqcsr.cqen = 0;
                write_register(CQCSR_OFFSET, 4, cqcsr.raw);
                write_register(i, 4, fctl.raw);
                fctl.raw = read_register(i, 4);
                fail_if( ( fctl.be != 1 ) ); 
                fail_if( ( fctl.wis != 0 ) ); 
                g_reg_file.capabilities.igs = IGS_BOTH;
                fctl.wis = 1;
                write_register(i, 4, fctl.raw);
                fctl.raw = read_register(i, 4);
                fail_if( ( fctl.wis != 1 ) ); 

                fctl.be = 0;
                fctl.wis = 0;
                write_register(i, 4, fctl.raw);
                pqcsr.pqen = 1;
                write_register(PQCSR_OFFSET, 4, pqcsr.raw);
                cqcsr.cqen = 1;
                write_register(CQCSR_OFFSET, 4, cqcsr.raw);
                fqcsr.fqen = 1;
                write_register(FQCSR_OFFSET, 4, fqcsr.raw);
                fail_if( ( enable_iommu(DDT_3LVL) < 0 ) );
                offset += 4;
                break;
            case DDTP_OFFSET:
                ddtp.raw = read_register(DDTP_OFFSET, 8);
                fail_if( (ddtp.iommu_mode != DDT_3LVL ) );
                offset += 8;
                break;
            case CQB_OFFSET:
            case FQB_OFFSET:
            case PQB_OFFSET:
                temp = read_register(i, 8);
                write_register(i, 8, 0xFF);
                fail_if( ( temp != read_register(i, 8) ) );
                offset += 8;
                break;
            case FQT_OFFSET:
            case CQH_OFFSET:
            case PQT_OFFSET:
            case IOCNTOVF_OFFSET:
                temp = read_register(i, 4);
                write_register(i, 4, 0xFF);
                fail_if( ( temp != read_register(i, 4) ) );
                offset += 4;
                break;
            case FQH_OFFSET:
            case PQH_OFFSET:
            case CQT_OFFSET:
                offset += 4;
                break;
            case CQCSR_OFFSET:
            case FQCSR_OFFSET:
            case PQCSR_OFFSET:
            case IPSR_OFFSET:
            case IOCNTINH_OFFSET:
                offset += 4;
                break;
            case IOHPMCYCLES_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.hpm = 0;
                write_register(i, 8, temp + 1);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.hpm = 1;
                write_register(i, 8, temp + 1);
                fail_if( ( (temp + 1) != read_register(i, 8) ) );
                offset += 8;
                break;
            case IOHPMCTR1_OFFSET:
            case IOHPMCTR2_OFFSET:
            case IOHPMCTR3_OFFSET:
            case IOHPMCTR4_OFFSET:
            case IOHPMCTR5_OFFSET:
            case IOHPMCTR6_OFFSET:
            case IOHPMCTR7_OFFSET:
            case IOHPMCTR8_OFFSET:
            case IOHPMCTR9_OFFSET:
            case IOHPMCTR10_OFFSET:
            case IOHPMCTR11_OFFSET:
            case IOHPMCTR12_OFFSET:
            case IOHPMCTR13_OFFSET:
            case IOHPMCTR14_OFFSET:
            case IOHPMCTR15_OFFSET:
            case IOHPMCTR16_OFFSET:
            case IOHPMCTR17_OFFSET:
            case IOHPMCTR18_OFFSET:
            case IOHPMCTR19_OFFSET:
            case IOHPMCTR20_OFFSET:
            case IOHPMCTR21_OFFSET:
            case IOHPMCTR22_OFFSET:
            case IOHPMCTR23_OFFSET:
            case IOHPMCTR24_OFFSET:
            case IOHPMCTR25_OFFSET:
            case IOHPMCTR26_OFFSET:
            case IOHPMCTR27_OFFSET:
            case IOHPMCTR28_OFFSET:
            case IOHPMCTR29_OFFSET:
            case IOHPMCTR30_OFFSET:
            case IOHPMCTR31_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.hpm = 0;
                write_register(i, 8, temp + 1);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.hpm = 1;
                write_register(i, 8, temp + 1);
                if ( offset < IOHPMCTR8_OFFSET ) {
                    fail_if( ( (temp + 1) != read_register(i, 8) ) );
                } else {
                    fail_if( ( (temp) != read_register(i, 8) ) );
                }
                offset += 8;
                break;
            case IOHPMEVT1_OFFSET:
            case IOHPMEVT2_OFFSET:
            case IOHPMEVT3_OFFSET:
            case IOHPMEVT4_OFFSET:
            case IOHPMEVT5_OFFSET:
            case IOHPMEVT6_OFFSET:
            case IOHPMEVT7_OFFSET:
            case IOHPMEVT8_OFFSET:
            case IOHPMEVT9_OFFSET:
            case IOHPMEVT10_OFFSET:
            case IOHPMEVT11_OFFSET:
            case IOHPMEVT12_OFFSET:
            case IOHPMEVT13_OFFSET:
            case IOHPMEVT14_OFFSET:
            case IOHPMEVT15_OFFSET:
            case IOHPMEVT16_OFFSET:
            case IOHPMEVT17_OFFSET:
            case IOHPMEVT18_OFFSET:
            case IOHPMEVT19_OFFSET:
            case IOHPMEVT20_OFFSET:
            case IOHPMEVT21_OFFSET:
            case IOHPMEVT22_OFFSET:
            case IOHPMEVT23_OFFSET:
            case IOHPMEVT24_OFFSET:
            case IOHPMEVT25_OFFSET:
            case IOHPMEVT26_OFFSET:
            case IOHPMEVT27_OFFSET:
            case IOHPMEVT28_OFFSET:
            case IOHPMEVT29_OFFSET:
            case IOHPMEVT30_OFFSET:
            case IOHPMEVT31_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.hpm = 0;
                write_register(i, 8, temp + 1);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.hpm = 1;
                write_register(i, 8, temp + 1);
                if ( offset < IOHPMEVT8_OFFSET ) {
                    fail_if( ( (temp + 1) != read_register(i, 8) ) );
                } else {
                    fail_if( ( (temp) != read_register(i, 8) ) );
                }
                offset += 8;
                break;
    
            case TR_REQ_IOVA_OFFSET:
            case TR_REQ_CTRL_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.dbg = 0;
                write_register(i, 8, temp + 2);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.dbg = 1;
                write_register(i, 8, temp + 2);
                fail_if( ( (temp + 2) != read_register(i, 8) ) );
                offset += 8;
                break;
            case TR_RESPONSE_OFFSET:
                temp = read_register(i, 8);
                write_register(i, 8, temp + 1);
                fail_if( ( temp != read_register(i, 8) ) );
                offset += 8;
                break;

            case ICVEC_OFFSET:
                offset += 4;
                break;
            case MSI_ADDR_0_OFFSET:
            case MSI_ADDR_1_OFFSET:
            case MSI_ADDR_2_OFFSET:
            case MSI_ADDR_3_OFFSET:
            case MSI_ADDR_4_OFFSET:
            case MSI_ADDR_5_OFFSET:
            case MSI_ADDR_6_OFFSET:
            case MSI_ADDR_7_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.igs = WIS;
                write_register(i, 8, temp + 8);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.igs = 0;
                write_register(i, 8, temp + 8);
                fail_if( ( (temp + 8) != read_register(i, 8) ) );
                offset += 8;
                break;
            case MSI_ADDR_8_OFFSET:
            case MSI_ADDR_9_OFFSET:
            case MSI_ADDR_10_OFFSET:
            case MSI_ADDR_11_OFFSET:
            case MSI_ADDR_12_OFFSET:
            case MSI_ADDR_13_OFFSET:
            case MSI_ADDR_14_OFFSET:
            case MSI_ADDR_15_OFFSET:
                temp = read_register(i, 8);
                g_reg_file.capabilities.igs = WIS;
                write_register(i, 8, temp + 8);
                fail_if( ( temp != read_register(i, 8) ) );
                g_reg_file.capabilities.igs = 0;
                write_register(i, 8, temp + 8);
                fail_if( ( (temp + 8) == read_register(i, 8) ) );
                offset += 8;
                break;
            case MSI_DATA_0_OFFSET:
            case MSI_DATA_1_OFFSET:
            case MSI_DATA_2_OFFSET:
            case MSI_DATA_3_OFFSET:
            case MSI_DATA_4_OFFSET:
            case MSI_DATA_5_OFFSET:
            case MSI_DATA_6_OFFSET:
            case MSI_DATA_7_OFFSET:
            case MSI_VEC_CTRL_0_OFFSET:
            case MSI_VEC_CTRL_1_OFFSET:
            case MSI_VEC_CTRL_2_OFFSET:
            case MSI_VEC_CTRL_3_OFFSET:
            case MSI_VEC_CTRL_4_OFFSET:
            case MSI_VEC_CTRL_5_OFFSET:
            case MSI_VEC_CTRL_6_OFFSET:
            case MSI_VEC_CTRL_7_OFFSET:
                write_register(i, 4, 0);
                temp = read_register(i, 4);
                g_reg_file.capabilities.igs = WIS;
                write_register(i, 4, temp + 1);
                fail_if( ( temp != read_register(i, 4) ) );
                g_reg_file.capabilities.igs = 0;
                write_register(i, 4, temp + 1);
                fail_if( ( (temp + 1) != read_register(i, 4) ) );
                offset += 4;
                break;
            case MSI_DATA_8_OFFSET:
            case MSI_DATA_9_OFFSET:
            case MSI_DATA_10_OFFSET:
            case MSI_DATA_11_OFFSET:
            case MSI_DATA_12_OFFSET:
            case MSI_DATA_13_OFFSET:
            case MSI_DATA_14_OFFSET:
            case MSI_DATA_15_OFFSET:
            case MSI_VEC_CTRL_8_OFFSET:
            case MSI_VEC_CTRL_9_OFFSET:
            case MSI_VEC_CTRL_10_OFFSET:
            case MSI_VEC_CTRL_11_OFFSET:
            case MSI_VEC_CTRL_12_OFFSET:
            case MSI_VEC_CTRL_13_OFFSET:
            case MSI_VEC_CTRL_14_OFFSET:
            case MSI_VEC_CTRL_15_OFFSET:
                write_register(i, 4, 0);
                temp = read_register(i, 4);
                g_reg_file.capabilities.igs = WIS;
                write_register(i, 4, temp + 1);
                fail_if( ( temp != read_register(i, 4) ) );
                g_reg_file.capabilities.igs = 0;
                write_register(i, 4, temp + 1);
                fail_if( ( (temp + 1) == read_register(i, 4) ) );
                offset += 4;
                break;
            default:
                temp = read_register(i, 4);
                write_register(i, 4, temp + 1);
                fail_if( ( temp != read_register(i, 4) ) );
                offset += 1;
                break;
        }
    }
    // writing as two 4B writes
    write_register(MSI_ADDR_0_OFFSET, 4, 0xDEADBEE0);
    write_register(MSI_ADDR_0_OFFSET+4, 4, 0x000000F0);
    temp = read_register(MSI_ADDR_0_OFFSET, 8);
    fail_if( ( temp != 0x000000F0DEADBEE0 ) );

    // Misaligned access
    temp1 = read_register(FQCSR_OFFSET, 4);
    write_register(FQCSR_OFFSET, 8, 0);
    fail_if( ( temp1 != read_register(FQCSR_OFFSET, 4) ) );
    temp1 = read_register(FQCSR_OFFSET, 8);
    fail_if( ( temp1 != 0xFFFFFFFFFFFFFFFF ) );
    temp1 = read_register(FQCSR_OFFSET, 3);
    fail_if( ( temp1 != 0xFFFFFFFFFFFFFFFF ) );

    // DIsable ATS only registers
    g_reg_file.capabilities.ats = 0;
    g_reg_file.capabilities.hpm = 0;

    temp = read_register(PQB_OFFSET, 8);
    write_register(PQB_OFFSET, 8, temp + 1);
    fail_if( ( temp != read_register(PQB_OFFSET, 8) ) );

    temp = read_register(PQH_OFFSET, 4);
    write_register(PQH_OFFSET, 4, temp + 1);
    fail_if( ( temp != read_register(PQH_OFFSET, 4) ) );

    temp = read_register(PQT_OFFSET, 4);
    write_register(PQT_OFFSET, 4, temp + 1);
    fail_if( ( temp != read_register(PQT_OFFSET, 4) ) );

    write_register(ICVEC_OFFSET, 8, 0x0000000000005555);
    fail_if( ( read_register(ICVEC_OFFSET, 8) != 0x0000000000000055) );
    g_reg_file.capabilities.hpm = 1;
    write_register(ICVEC_OFFSET, 8, 0x0000000000005555);
    fail_if( ( read_register(ICVEC_OFFSET, 8) != 0x0000000000000555) );
    g_reg_file.capabilities.ats = 1;
    write_register(ICVEC_OFFSET, 8, 0x0000000000005555);
    fail_if( ( read_register(ICVEC_OFFSET, 8) != 0x0000000000005555) );

    END_TEST();
    return 0;
}
