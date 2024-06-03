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
    uint64_t addr, uint8_t size, char *data){
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    memcpy(data, &memory[addr], size);
    return 0;
}

uint8_t
read_memory_for_AMO(
    uint64_t addr, uint8_t size, char *data) {
    // Same for now
    return read_memory(addr, size, data);
}

uint8_t
write_memory(
    char *data, uint64_t addr, uint32_t size) {
    if ( addr == access_viol_addr ) return ACCESS_FAULT;
    if ( addr == data_corruption_addr ) return DATA_CORRUPTION;
    memcpy(&memory[addr], data, size);
    return 0;
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

    *read = ( req->tr.read_writeAMO == READ ) ? 1 : 0;
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
    *exec = ( (*read && req->exec_req &&
                (req->tr.at == ADDR_TYPE_UNTRANSLATED || req->pid_valid)) ) ? 1 : 0;
    *priv = ( req->pid_valid && req->priv_req ) ? S_MODE : U_MODE;
    return;
}
