// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include <stdio.h>
#include <inttypes.h>
#include "iommu.h"
#include "tables_api.h"
#include "test_app.h"

uint8_t
read_memory(
    uint64_t addr, uint8_t size, char *data, uint32_t rcid, uint32_t mcid,
    uint32_t pma, int endian){
    uint8_t tmp;
    uint32_t i, j;
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    memcpy(data, &memory[addr], size);
    if (endian == BIG_ENDIAN) {
        if (size == 4) {
            tmp = data[0]; data[0] = data[3]; data[3] = tmp;
            tmp = data[1]; data[1] = data[2]; data[2] = tmp;
        } else if ((size % 8) == 0) {
            for (i = 0; i < size; i += 8) {
                for (j = 0; j < 4; j++) {
                    tmp = data[i + j];
                    data[i + j] = data[i + 7 - j];
                    data[i + 7 - j] = tmp;
                }
            }
        } else {
            printf("%s: Invalid size %u for BIG_ENDIAN\n", __func__, size);
            exit(1);
        }
    }
    return 0;
}
uint8_t
read_memory_test(
    uint64_t addr, uint8_t size, char *data) {
    return read_memory(addr, size, data, 0, 0, PMA, test_endian);
}
uint8_t
read_memory_for_AMO(
    uint64_t addr, uint8_t size, char *data, uint32_t rcid, uint32_t mcid,
    uint32_t pma, int endian) {
    // Same for now
    return read_memory(addr, size, data, rcid, mcid, pma, endian);
}

uint8_t
write_memory(
    char *data, uint64_t addr, uint32_t size, uint32_t rcid, uint32_t mcid,
    uint32_t pma, int endian) {
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    if (endian == BIG_ENDIAN) {
        if (size == 4) {
            /* data[] is BE, store as LE in memory[] */
            memory[addr + 0] = (uint8_t)data[3];
            memory[addr + 1] = (uint8_t)data[2];
            memory[addr + 2] = (uint8_t)data[1];
            memory[addr + 3] = (uint8_t)data[0];
        } else if ((size % 8) == 0) {
            char *src = data;
            char *dst = (char *)&memory[addr];
            uint32_t i, j;
            for (i = 0; i < size; i += 8) {
                for (j = 0; j < 4; j++) {
                    dst[i + j]        = src[i + 7 - j];
                    dst[i + 7 - j]    = src[i + j];
                }
            }
        } else {
            printf("%s: Invalid size %u for BIG_ENDIAN\n", __func__, size);
            exit(1);
        }
    } else {
        memcpy(&memory[addr], data, size);
    }
    return 0;
}
uint8_t
write_memory_test(
    char *data, uint64_t addr, uint32_t size) {
    return write_memory(data, addr, size, 0, 0, PMA, test_endian);
}


void
iommu_to_hb_do_global_observability_sync(
    uint8_t PR, uint8_t PW){
    pr_go_requested = PR;
    pw_go_requested = PW;
    return;
}

void
send_msg_iommu_to_hb(
    ats_msg_t *msg) {
    if ( exp_msg.MSGCODE != msg->MSGCODE ||
         exp_msg.TAG != msg->TAG ||
         exp_msg.RID != msg->RID ||
         exp_msg.PV  != msg->PV ||
         exp_msg.PID != msg->PID ||
         exp_msg.PRIV != msg->PRIV ||
         exp_msg.EXEC_REQ != msg->EXEC_REQ ||
         exp_msg.DSV != msg->DSV ||
         exp_msg.DSEG != msg->DSEG ||
         exp_msg.PAYLOAD != msg->PAYLOAD )
        exp_msg_received = 0;
    else
        exp_msg_received = 1;
    message_received = 1;
    memcpy(&rcvd_msg, msg, sizeof(ats_msg_t));
    return;
}

void
get_attribs_from_req(
    hb_to_iommu_req_t *req, uint8_t *read, uint8_t *write, uint8_t *exec, uint8_t *priv) {

    *read = (req->tr.read_writeAMO == READ && req->exec_req && req->tr.at == ADDR_TYPE_UNTRANSLATED) ?
            0 : ( req->tr.read_writeAMO == READ ) ? 1 : 0;

    *write = ( req->tr.read_writeAMO == WRITE ) ?  1 : 0;

    // The No Write flag, when Set, indicates that the Function is requesting read-only
    // access for this translation.
    // The TA (IOMMU) may ignore the No Write Flag, however, if the TA responds with a
    // translation marked as read-only then the Function must not issue Memory Write
    // transactions using that translation. In this case, the Function may issue another
    // translation request with the No Write flag Clear, which may result in a new
    // translation completion with or without the W (Write) bit Set.
    // Upon receiving a Translation Request with the NW flag Clear, TAs are permitted to
    // mark the associated pages dirty. Functions MUST not issue such Requests
    // unless they have been given explicit write permission.
    // Note ATS Translation requests are read - so read_writeAMO is READ for these requests
    *write = ( (req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) &&
                 (req->no_write == 0) ) ? 1 : *write;

    // If a Translation Request has a PASID, the Untranslated Address Field is an address
    // within the process address space indicated by the PASID field.
    // If a Translation Request has a PASID with either the Privileged Mode Requested
    // or Execute Requested bit Set, these may be used in constructing the Translation
    // Completion Data Entry.  The PASID Extended Capability indicates whether a Function
    // supports and is enabled to send and receive TLPs with the PASID.
    *exec = ( req->tr.read_writeAMO == READ && (req->exec_req &&
                (req->tr.at == ADDR_TYPE_UNTRANSLATED || req->pid_valid)) ) ? 1 : 0;
    *priv = ( req->pid_valid && req->priv_req ) ? S_MODE : U_MODE;
    return;
}
// If the GPA superpage overlaps a virtual interrupt file region, reduce *gst_page_sz to
// the largest smaller page size that avoids the overlap.
void
handle_virtual_interrupt_file_overlap(
    device_context_t *DC, uint64_t gpa, uint64_t *gst_page_sz) {
    uint64_t m = DC->msi_addr_mask.mask << 12;
    uint64_t p = DC->msi_addr_pattern.pattern << 12;
    // If MSI page table mode is Off or if the initial page sizes is base page
    // size, there is nothing to do.
    if (DC->msiptp.MODE == MSIPTP_Off || *gst_page_sz == PAGESIZE)
        return;
    for (uint64_t sz = PAGESIZE << 36; sz >= (PAGESIZE << 9); sz >>= 9) {
        uint64_t mask = m & ~(sz - 1);
        *gst_page_sz = (*gst_page_sz >= sz && ((gpa & mask) == (p & mask))) ?
                       (sz >> 9) : *gst_page_sz;
    }
}
