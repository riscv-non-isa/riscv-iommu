// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"

uint8_t
allocate_itag(
    iommu_t *iommu,
    uint8_t DSV, uint8_t DSEG, uint16_t RID, uint8_t *itag) {
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ )
        if ( iommu->itag_tracker[i].busy == 0 ) break;

    if ( i == MAX_ITAGS )
        return 1;

    iommu->itag_tracker[i].busy = 1;
    iommu->itag_tracker[i].DSV = DSV;
    iommu->itag_tracker[i].DSEG = DSEG;
    iommu->itag_tracker[i].RID = RID;
    iommu->itag_tracker[i].num_rsp_rcvd = 0;
    *itag = i;
    return 0;
}
uint8_t
any_ats_invalidation_requests_pending(iommu_t *iommu) {
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ )
        if ( iommu->itag_tracker[i].busy == 1 )
            return 1;
    return 0;
}
void
do_pending_iofence_inval_reqs(iommu_t *iommu) {
    uint8_t i, itags_busy;

    itags_busy = 0;
    // Check if there are more pending invalidations
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( iommu->itag_tracker[i].busy == 1 )
            itags_busy = 1;
    }
    // No more pending invalidations - continue any pending IOFENCE.C
    if ( iommu->iofence_wait_pending_inv == 1 && itags_busy == 0 ) {
        do_pending_iofence(iommu);
    }
    if ( iommu->iofence_wait_pending_inv == 0 ) {
        // If there were ATS.INVAL_REQ waiting on free
        // itags then unblock them if any itag is now
        // available unless a IOFENCE is blocking
        queue_any_blocked_ats_inval_req(iommu);
    }
    return;
}
uint8_t
handle_invalidation_completion(
    iommu_t *iommu, ats_msg_t *inv_cc) {

    uint32_t itag_vector;
    uint8_t cc, i;
    itag_vector = get_bits(31, 0, inv_cc->PAYLOAD);
    cc = get_bits(34, 32, inv_cc->PAYLOAD);
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_vector & (1UL << i) ) {
            if ( iommu->itag_tracker[i].busy == 0 )
                return 1; // Unexpected completion
            if ( (iommu->itag_tracker[i].DSV == 1) &&
                 (inv_cc->DSV != 1 || inv_cc->DSEG != iommu->itag_tracker[i].DSEG) )
                return 1; // Unexpected completion
            if ( iommu->itag_tracker[i].RID != inv_cc->RID )
                return 1; // Unexpected completion
            iommu->itag_tracker[i].num_rsp_rcvd =
                (iommu->itag_tracker[i].num_rsp_rcvd + 1) & 0x07;
            if ( iommu->itag_tracker[i].num_rsp_rcvd == cc )  {
                iommu->itag_tracker[i].busy = 0;
            }
        }
    }
    do_pending_iofence_inval_reqs(iommu);
    return 0;
}
void
do_ats_timer_expiry(iommu_t *iommu, uint32_t itag_vector) {
    uint8_t i;
    for ( i = 0; i < MAX_ITAGS; i++ ) {
        if ( itag_vector & (1UL << i) ) {
            iommu->itag_tracker[i].busy = 0;
        }
    }
    iommu->ats_inv_req_timeout = 1;
    do_pending_iofence_inval_reqs(iommu);
    return;
}
void
handle_page_request(
    iommu_t *iommu,
    ats_msg_t *pr) {
    ats_msg_t prgr;
    device_context_t DC;
    page_rec_t prec;
    int endian;
    uint8_t DDI[3];
    uint8_t L, R, W;
    uint16_t PRGI;
    uint32_t device_id, cause, status, response_code, PRPR;
    uint64_t prec_addr;
    uint64_t pqb;
    uint32_t pqh;
    uint32_t pqt;
    uint64_t pa_mask = ((1UL << (iommu->reg_file.capabilities.pas)) - 1);

    endian = iommu->reg_file.fctl.be ? BIG_ENDIAN : LITTLE_ENDIAN;

    PRPR = 0;
    device_id =  ( pr->DSV == 1 ) ? (pr->RID | (pr->DSEG << 16)) : pr->RID;
    if ( iommu->reg_file.ddtp.iommu_mode == Off ) {
        cause = 256; // "All inbound transactions disallowed"
        report_fault(iommu, cause, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, 0,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_RESPONSE_FAILURE;
        goto send_prgr;
    }
    if ( iommu->reg_file.ddtp.iommu_mode == DDT_Bare ) {
        cause = 260; // "Transaction type disallowed"
        report_fault(iommu, cause, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, 0,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_INVALID_REQUEST;
        goto send_prgr;
    }
    // 3. If `capabilities.MSI_FLAT` is 0 then the IOMMU uses base-format device
    //    context. Let `DDI[0]` be `device_id[6:0]`, `DDI[1]` be `device_id[15:7]`, and
    //    `DDI[2]` be `device_id[23:16]`.
    if ( iommu->reg_file.capabilities.msi_flat == 0 ) {
        DDI[0] = get_bits(6,  0, device_id);
        DDI[1] = get_bits(15, 7, device_id);
        DDI[2] = get_bits(23, 16, device_id);
    }
    // 4. If `capabilities.MSI_FLAT` is 0 then the IOMMU uses extended-format device
    //    context. Let `DDI[0]` be `device_id[5:0]`, `DDI[1]` be `device_id[14:6]`, and
    //    `DDI[2]` be `device_id[23:15]`.
    if ( iommu->reg_file.capabilities.msi_flat == 1 ) {
        DDI[0] = get_bits(5,  0, device_id);
        DDI[1] = get_bits(14, 6, device_id);
        DDI[2] = get_bits(23, 15, device_id);
    }
    // 5. The `device_id` is wider than that supported by the IOMMU mode if any of the
    //    following conditions hold. If the following conditions hold then stop and
    //    report "Transaction type disallowed" (cause = 260).
    //    a. `ddtp.iommu_mode` is `2LVL` and `DDI[2]` is not 0
    //    b. `ddtp.iommu_mode` is `1LVL` and either `DDI[2]` is not 0 or `DDI[1]` is not 0
    if ( iommu->reg_file.ddtp.iommu_mode == DDT_2LVL && DDI[2] != 0 ) {
        cause = 260; // "Transaction type disallowed"
        report_fault(iommu, cause, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, 0,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_INVALID_REQUEST;
        goto send_prgr;
    }
    if ( iommu->reg_file.ddtp.iommu_mode == DDT_1LVL && (DDI[2] != 0 || DDI[1] != 0) ) {
        cause = 260; // "Transaction type disallowed"
        report_fault(iommu, cause, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, 0,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_INVALID_REQUEST;
        goto send_prgr;
    }
    // To process a "Page Request" or "Stop Marker" message, the IOMMU first
    // locates the device-context to determine if ATS and PRI are enabled for
    // the requestor.
    if ( locate_device_context(iommu, &DC, device_id, pr->PV, pr->PID, &cause) ) {
        report_fault(iommu, cause, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, 0,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_RESPONSE_FAILURE;
        goto send_prgr;
    }
    PRPR = DC.tc.PRPR;
    if ( DC.tc.EN_PRI == 0 ) {
        // 7. if any of the following conditions hold then stop and report
        //    "Transaction type disallowed" (cause = 260).
        //   * Transaction type is a PCIe "Page Request" Message and `DC.tc.EN_PRI` is 0.
        report_fault(iommu, 260, PAGE_REQ_MSG_CODE, 0, PCIE_MESSAGE_REQUEST, DC.tc.DTF,
                     device_id, pr->PV, pr->PID, pr->PRIV);
        response_code = PRGR_INVALID_REQUEST;
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
    if ( iommu->reg_file.pqcsr.pqon == 0 || iommu->reg_file.pqcsr.pqen == 0 ) {
        response_code = PRGR_RESPONSE_FAILURE;
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
    if ( iommu->reg_file.pqcsr.pqmf == 1 ) {
        response_code = PRGR_RESPONSE_FAILURE;
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
    if ( iommu->reg_file.pqcsr.pqof == 1 ) {
        response_code = PRGR_SUCCESS;
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
    pqh = iommu->reg_file.pqh.index;
    pqt = iommu->reg_file.pqt.index;
    pqb = iommu->reg_file.pqb.ppn;
    if ( ((pqt + 1) & ((1UL << (iommu->reg_file.pqb.log2szm1 + 1)) - 1)) == pqh ) {
        iommu->reg_file.pqcsr.pqof = 1;
        generate_interrupt(iommu, PAGE_QUEUE);
        response_code = SUCCESS;
        goto send_prgr;
    }
    prec.DID      = device_id;
    prec.PID      = pr->PID;
    prec.PV       = pr->PV;
    prec.PRIV     = (pr->PV == 0) ? 0 : pr->PRIV;
    prec.EXEC     = (pr->PV == 0) ? 0 : pr->EXEC_REQ;
    prec.PAYLOAD  = pr->PAYLOAD;
    prec.reserved0= 0;
    prec.reserved1= 0;
    prec_addr = ((pqb * PAGESIZE) | (pqt * PQ_ENTRY_SZ));
    status = (prec_addr & ~pa_mask) ?
             ACCESS_FAULT :
             write_memory((char *)&prec, prec_addr, 16,
                          iommu->reg_file.iommu_qosid.rcid,
                          iommu->reg_file.iommu_qosid.mcid, PMA, endian);
    if ( (status & ACCESS_FAULT) || (status & DATA_CORRUPTION) ) {
        iommu->reg_file.pqcsr.pqmf = 1;
        generate_interrupt(iommu, PAGE_QUEUE);
        response_code = PRGR_RESPONSE_FAILURE;
        goto send_prgr;
    }

    pqt = (pqt + 1) & ((1UL << (iommu->reg_file.pqb.log2szm1 + 1)) - 1);
    iommu->reg_file.pqt.index = pqt;
    generate_interrupt(iommu, PAGE_QUEUE);
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
    R    = get_bits(0,  0, pr->PAYLOAD);
    W    = get_bits(1,  1, pr->PAYLOAD);
    L    = get_bits(2,  2, pr->PAYLOAD);
    PRGI = get_bits(11, 3, pr->PAYLOAD);
    if ( L == 0 || (L == 1 && R == 0 && W == 0) ) {
        return;
    }
    prgr.MSGCODE = PRGR_MSG_CODE;
    prgr.TAG = 0;
    prgr.RID = pr->RID;
    prgr.DSV = pr->DSV;
    prgr.DSEG = pr->DSEG;
    prgr.PRIV = 0;
    prgr.EXEC_REQ = 0;

    // For IOMMU generated "Page Request Group Response" messages that have status
    // Invalid Request or Success, the PRG-response-PASID-required (PRPR) bit when
    // set to 1 indicates that the IOMMU response message should include a PASID if
    // the associated "Page Request" had a PASID.  For IOMMU generated "Page Request
    // Group Response" with response code set to Response Failure, if the "Page Request"
    // had a PASID then response is generated with a PASID.
    if ( response_code == PRGR_INVALID_REQUEST || response_code == PRGR_SUCCESS ) {
        if ( PRPR == 1 ) {
            prgr.PV = pr->PV;
            prgr.PID = pr->PID;
        } else {
            prgr.PV = 0;
            prgr.PID = 0;
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
    prgr.PAYLOAD = ((uint64_t)pr->RID << 48UL) |
                   ((uint64_t)response_code << 44UL) |
                   ((uint64_t)PRGI << 32UL);
    send_msg_iommu_to_hb(&prgr);
    return;
}
