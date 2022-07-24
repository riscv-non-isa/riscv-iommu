// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"

itag_tracker_t itag_tracker[MAX_ITAGS];

uint8_t
allocate_itag(
    uint8_t DSV, uint8_t DSEG, uint16_t RID, uint8_t *itag) { 
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ )
        if ( itag_tracker[i].free == 1 ) break;

    if ( i == MAX_ITAGS )
        return 1;

    itag_tracker[i].free = 0;
    itag_tracker[i].DSV = DSV;
    itag_tracker[i].DSEG = DSEG;
    itag_tracker[i].RID = RID;
    itag_tracker[i].num_rsp_rcvd = 0;
    *itag = i;
    return 0;
}
uint8_t
any_ats_invalidation_requests_pending() {
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ )
        if ( itag_tracker[i].free == 0 ) 
            return 1;
    return 0;
}
uint8_t
handle_invalidation_completion(
    ats_msg_t *inv_cc) {

    uint32_t itag_vector;
    uint8_t cc, i;
    itag_vector = get_bits(31, 0, inv_cc->PAYLOAD);
    cc = get_bits(34, 32, inv_cc->PAYLOAD);
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_vector & (1UL << i) ) {
            if ( itag_tracker[i].free == 1 ) 
                return 1; // Unexpected completion
            if ( (itag_tracker[i].DSV == 1) &&
                 (inv_cc->DSV != 1 || inv_cc->DSEG != itag_tracker[i].DSEG) )
                return 1; // Unexpected completion
            if ( itag_tracker[i].RID != inv_cc->RID )
                return 1; // Unexpected completion
            itag_tracker[i].num_rsp_rcvd = 
                (itag_tracker[i].num_rsp_rcvd + 1) & 0x07;
            if ( itag_tracker[i].num_rsp_rcvd == cc )  {
                itag_tracker[i].free = 1;
            }
        }
    }
    // If there were ATS.INVAL_REQ waiting on free
    // itags then unblock them if any itag is now
    // available
    queue_any_blocked_ats_inval_req();

    // Check if there are more pending invalidations
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_tracker[i].free == 0 ) 
            return 0;
    }
    // No more pending invalidations - continue any pending IOFENCE.C
    do_pending_iofence();
    return 0;
}
void
do_ats_timer_expiry(uint32_t itag_vector) {
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_vector & (1UL << i) ) {
            itag_tracker[i].free = 1;
        }
    }
    g_ats_inv_req_timeout = 1;

    // Check if there are more pending invalidations
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_tracker[i].free == 0 ) 
            return;
    }
    // No more pending invalidations - continue any pending IOFENCE.C
    do_pending_iofence();
    return;
}
void
handle_page_request(
    ats_msg_t *pr) {
    ats_msg_t prgr;
    device_context_t DC;
    page_rec_t prec;
    uint8_t L;
    uint16_t PRGI;
    uint32_t device_id, cause, status, response_code;
    uint64_t prec_addr;
    uint64_t pqb;
    uint32_t pqh;
    uint32_t pqt;

    // To process a "Page Request" or "Stop Marker" message, the IOMMU first
    // locates the device-context to determine if ATS and PRI are enabled for
    // the requestor. 
    device_id =  ( pr->DSV == 1 ) ? (pr->RID | (pr->DSEG << 16)) : pr->RID;
    if ( locate_device_context(&DC, device_id, pr->PV, pr->PID, &cause) ) {
        report_fault(cause, PAGE_REQ_MSG_CODE, 0, MESSAGE_REQUEST, 0, 
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = RESPONSE_FAILURE;
        goto send_prgr;
    }
    if ( DC.tc.EN_ATS == 0 || DC.tc.EN_PRI == 0 ) {
        report_fault(cause, PAGE_REQ_MSG_CODE, 0, MESSAGE_REQUEST, 0, 
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = INVALID_REQUEST;
        goto send_prgr;
    }
    // If ATS and PRI are enabled, i.e. EN_ATS and EN_PRI are both set to 1, 
    // the IOMMU queues the message into an in-memory queue called the page 
    // request-queue (PQ) (See Section 3.3). 
    // When PRI is enabled for a device, the IOMMU may still be unable to report
    // "Page Request" or "Stop Marker" messages through the PQ due to error 
    // conditions such as the queue being disabled, queue being full, or the 
    // IOMMU encountering access faults when attempting to access queue memory.

    // The page-request-queue enable bit enables the 
    // page-request-queue when set to 1. 
    // The page-request-queue is active if pqon reads 1. 
    // The IOMMU may respond to “Page Request” messages received
    // when page-request-queue is off or in the process of being turned
    // off, as specified in Section 2.8.
    if ( g_reg_file.pqcsr.pqon == 0 || g_reg_file.pqcsr.pqen == 0 ) {
        response_code = RESPONSE_FAILURE;
        goto send_prgr;
    }

    // The pqmf bit is set to 1 if the IOMMU encounters an access fault
    // when storing a page-request to the page-request queue. 
    // The "Page Request" message that caused the pqmf or pqof error and
    // all subsequent page-request messages are discarded till software
    // clears the pqof and/or pqmf bits by writing 1 to it.
    // The IOMMU may respond to “Page Request” messages that caused
    // the pqof or pqmf bit to be set and all subsequent “Page Request”
    // messages received while these bits are 1 as specified in Section 2.8.
    if ( g_reg_file.pqcsr.pqmf == 1 ) {
        response_code = RESPONSE_FAILURE;
        goto send_prgr;
    }

    // The page-request-queue-overflow bit is set to 1 if the page-request
    // queue overflows i.e. IOMMU needs to queue a page-request
    // message but the page-request queue is full (i.e., pqh == pqt - 1).
    // When pqof is set to 1, an interrupt is generated if not already
    // pending (i.e. ipsr.pip == 1) and not masked (i.e. pqsr.pie == 1).
    // The "Page Request" message that caused the pqmf or pqof error and
    // all subsequent page-request messages are discarded till software
    // clears the pqof and/or pqmf bits by writing 1 to it.
    // The IOMMU may respond to “Page Request” messages that caused
    // the pqof or pqmf bit to be set and all subsequent “Page Request”
    // messages received while these bits are 1 as specified in Section 2.8.
    if ( g_reg_file.pqcsr.pqof == 1 ) {
        response_code = SUCCESS;
        goto send_prgr;
    }

    // Page-request queue is an in-memory queue data structure used to report 
    // PCIe ATS “Page Request” and "Stop Marker" messages to software. The base 
    // PPN of this in-memory queue and the size of the queue is configured into 
    // a memory-mapped register called page-request queue base (pqb). Each Page 
    // Request record is 16 bytes.  The tail of the queue resides in a IOMMU 
    // controlled read-only memory-mapped register called pqt.  The pqt holds an 
    // index into the queue where the next page-request message will be written 
    // by the IOMMU. Subsequent to writing the message, the IOMMU advances the pqt by 1.
    // The head of the queue resides in a software controlled read/write memory-mapped 
    // register called pqh. The pqh holds an index into the queue where the next 
    // page-request message will be received by software. Subsequent to processing 
    // the message(s) software advances the pqh by the count of the number of messages 
    // processed.
    // If pqh == pqt, the page-request queue is empty.
    // If pqt == (pqh - 1) the page-request queue is full.
    // The IOMMU may be unable to report "Page Request" messages through the queue 
    // due to error conditions such as the queue being disabled, queue being full, or 
    // the IOMMU encountering access faults when attempting to access queue memory. A 
    // memory-mapped page-request queue control and status register (pqcsr) is used to 
    // hold information about such faults. On a page queue full condition the 
    // page-request-queue overflow (pqof) bit is set in pqcsr. If the IOMMU encountered 
    // a fault in accessing the queue memory, page-request-queue memory access fault 
    // (pqmf) bit in pqcsr. While either error bits are set in pqcsr, the IOMMU discards
    // all subsequent "Page Request" messages; including the message that caused the error
    // bits to be set. "Page request" messages that do not require a response, i.e. those 
    // with the "Last Request in PRG" field is 0, are silently discarded. "Page request"
    // messages that require a response, i.e. those with "Last Request in PRG" field set 
    // to 1 and are not Stop Marker messages, may be auto-completed by an IOMMU generated 
    // “Page Request Group Response” message as specified in Section 2.8.
    // When an error bit is in the pqcsr changes state from 0 to 1 or when a new message
    // is produced in the queue, page-request-queue interrupt pending (pip) bit is set 
    // in the pqcsr
    pqh = g_reg_file.pqh.index;
    pqt = g_reg_file.pqt.index;
    pqb = g_reg_file.pqb.ppn;
    if ( pqt == (pqh - 1) ) {
        g_reg_file.pqcsr.pqof = 1;
        generate_interrupt(PAGE_QUEUE);
        response_code = SUCCESS;
        goto send_prgr;
    }
    prec.DID      = device_id;
    prec.PID      = pr->PID;
    prec.PV       = pr->PV;
    prec.PRIV     = (pr->PV == 1) ? 0 : pr->PRIV;
    prec.X        = (pr->PV == 1) ? 0 : pr->EXEC_REQ;
    prec.PAYLOAD  = pr->PAYLOAD;
    prec.reserved = 0;
    prec_addr = ((pqb * 4096) | (pqt * 16));
    status = write_memory((char *)&prec, prec_addr, 16);
    if ( (status & ACCESS_FAULT) || (status & DATA_CORRUPTION) ) {
        g_reg_file.pqcsr.pqmf = 1;
        generate_interrupt(PAGE_QUEUE);
        response_code = RESPONSE_FAILURE;
        goto send_prgr;
    }

    pqt = (pqt + 1) & ((1UL << (g_reg_file.pqb.log2szm1 + 1)) - 1);
    g_reg_file.pqt.index = pqt;
    generate_interrupt(PAGE_QUEUE);
    return;

send_prgr:
    // If EN_PRI is set to 0, or EN_ATS is set to 0, or if the IOMMU is unable to 
    // locate the DC to determine the EN_PRI configuration, or the request could not 
    // be queued into PQ then the IOMMU behavior depends on the type of "Page Request".
    // * If the "Page Request" does not require a response, i.e. the "Last Request in 
    //   PRG" field of the message is set to 0, then such message are silently discarded. 
    //   "Stop Marker" messages do not require a response and are always silently 
    //   discarded on such errors.  
    // * If the "Page Request" needs a response, then the IOMMU itself may generate a 
    //   "Page Request Group Response" message to the device.
    // The payload of a "Page Request" is as follows
    //           +0         |      +1         |      +2         |      +3        |
    //    | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0|
    // 08h|                  Page Address [63:32]                                |
    // 0Ch|      Page Address [31:12]                  | PRGI              |L W R|
    // When the IOMMU generates the response, the status field of the response depends 
    // on the cause of the error.
    // The status is set to Response Failure if the following faults are encountered:
    // * All inbound transactions disallowed (cause = 256)
    // * DDT entry load access fault (cause = 257)
    // * DDT entry misconfigured (cause = 259)
    // * DDT entry not valid (cause = 258)
    // * Page-request queue is not enabled (pqcsr.pqen == 0 or pqcsr.pqon == 0)
    // * Page-request queue encountered a memory access fault (pqcsr.pqmf == 1)
    // The status is set to Invalid Request if the following faults are encountered:
    // * Transaction type disallowed (cause = 260)
    // The status is set to Success if no other faults were encountered but the 
    // "Page Request" could not be queued due to the page-request queue being full 
    // (pqh == pqt - 1) or had a overflow (pqcsr.pqof == 1).
    L    = get_bits(2,  2, pr->PAYLOAD);
    PRGI = get_bits(11, 3, pr->PAYLOAD);
    if ( L == 0 ) {
        return;
    }
    prgr.MSGCODE = PRGR_MSG_CODE;
    prgr.TAG = 0;
    prgr.RID = pr->RID;
    prgr.DSV = pr->DSV;
    prgr.DSEG = pr->DSEG;

    // For IOMMU generated "Page Request Group Response" messages that have status 
    // Invalid Request or Success, the PRG-response-PASID-required (PRPR) bit when 
    // set to 1 indicates that the IOMMU response message should include a PASID if
    // the associated "Page Request" had a PASID.  For IOMMU generated "Page Request 
    // Group Response" with response code set to Response Failure, if the "Page Request" 
    // had a PASID then response is generated with a PASID.
    if ( response_code == INVALID_REQUEST || response_code == SUCCESS ) {
        if ( DC.tc.PRPR == 1 ) {
            prgr.PV = pr->PV;
            prgr.PID = pr->PID;
        } else {
            prgr.PV = 0;
        }
    } else {
        prgr.PV = pr->PV;
        prgr.PID = pr->PID;
    }
    // PAYLOAD encoding of PRGR is as follows
    //           +0         |      +1         |      +2         |      +3        |
    //    | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0|
    // 0Ch|      Destination device ID        |  Resp  |RSVD | PRGI              |
    //    |                                   |  Code                            |
    // 08h|                  Reserved                                            |
    prgr.PAYLOAD = (pr->RID << 16) | (response_code << 9) | PRGI;
    send_msg_iommu_to_hb(&prgr);
    return;
}

