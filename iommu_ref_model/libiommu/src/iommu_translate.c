// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

#include "iommu.h"

void 
iommu_translate_iova(
    hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp_msg) {

    uint8_t DDI[3];
    device_context_t DC;
    process_context_t PC;
    iosatp_t iosatp;
    iohgatp_t iohgatp;
    uint8_t is_read, is_write, is_exec, priv, SUM, TTYP;
    uint8_t R, W, X, G, UNTRANSLATED_ONLY, PBMT; 
    uint64_t page_sz;
    uint32_t cause;
    uint64_t iotval2, iotval;
    uint64_t pa;
    uint8_t is_unsup, is_msi, is_mrif_wr, DTF;
    uint32_t mrif_nid;
    uint32_t PSCID;

    // Classify transaction type
    iotval2 = 0;
    iotval = req->tr.iova;
    is_read = is_write = is_exec = 0;
    is_unsup = is_msi = is_mrif_wr = 0;
    priv = U_MODE;
    DTF = 0;
    PSCID = 0;

    // Count events
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED )
        count_events(req->pid_valid, req->process_id, 0 /* PSCV */, 0 /*PSCID*/,
                     req->device_id, 0 /* GSCV */, 0 /* GSCID */, UNTRANSLATED_REQUEST);
    if ( req->tr.at == ADDR_TYPE_TRANSLATED )
        count_events(req->pid_valid, req->process_id, 0 /* PSCV */, 0 /*PSCID*/,
                     req->device_id, 0 /* GSCV */, 0 /* GSCID */, TRANSLATED_REQUEST);
    if ( req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST )
        count_events(req->pid_valid, req->process_id, 0 /* PSCV */, 0 /*PSCID*/,
                     req->device_id, 0 /* GSCV */, 0 /* GSCID */, TRANSLATION_REQUEST);
    TTYP = TTYPE_NONE;
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.read_writeAMO == READ ) {
        if ( req->pid_valid && req->exec_req )
            TTYP = UNTRANSLATED_READ_FOR_EXECUTE_TRANSACTION;
        else
            TTYP = UNTRANSLATED_READ_TRANSACTION;
    }
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.read_writeAMO == WRITE )
        TTYP = UNTRANSLATED_WRITE_AMO_TRANSACTION;
    if ( req->tr.at == ADDR_TYPE_TRANSLATED && req->tr.read_writeAMO == READ ) {
        if ( req->pid_valid && req->exec_req )
            TTYP = TRANSLATED_READ_FOR_EXECUTE_TRANSACTION;
        else
            TTYP = TRANSLATED_READ_TRANSACTION;
    }
    if ( req->tr.at == ADDR_TYPE_TRANSLATED && req->tr.read_writeAMO == WRITE )
        TTYP = TRANSLATED_WRITE_AMO_TRANSACTION;

    if ( req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST )
        TTYP = PCIE_ATS_TRANSLATION_REQUEST;

    // Has to be one of the valid ttypes - this only for debug - not architectural
    if ( TTYP == TTYPE_NONE ) *((char *)0) = 0;

    if ( req->tr.read_writeAMO == READ ) is_read = 1;
    if ( req->tr.read_writeAMO == WRITE ) is_write = 1;

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
    if ( (req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) && (req->no_write == 0) ) 
        is_write = 1;
    // If a Translation Request has a PASID, the Untranslated Address Field is an address 
    // within the process address space indicated by the PASID field.
    // If a Translation Request has a PASID with either the Privileged Mode Requested 
    // or Execute Requested bit Set, these may be used in constructing the Translation 
    // Completion Data Entry.  The PASID Extended Capability indicates whether a Function
    // supports and is enabled to send and receive TLPs with the PASID.
    if ( req->tr.read_writeAMO == READ && req->pid_valid && req->exec_req &&
         (req->tr.at != ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) ) 
        is_exec = 1;
    if ( req->pid_valid && req->priv_req ) priv = S_MODE;

    // The process to translate an `IOVA` is as follows:
    // 1. If `ddtp.iommu_mode == Off` then stop and report "All inbound transactions
    //    disallowed" (cause = 256).
    if ( g_reg_file.ddtp.iommu_mode == Off ) {
        cause = 256; // "All inbound transactions disallowed"
        goto stop_and_report_fault;
    }

    // 2. If `ddtp.iommu_mode == Bare` and any of the following conditions hold then
    //    stop and report "Transaction type disallowed" (cause = 260); else go to step
    //    18 with translated address same as the `IOVA`.
    //    a. Transaction type is a Translated request (read, write/AMO, read-for-execute)
    //       or is a PCIe ATS Translation request.
    //    b. Transaction type is a PCIe "Page Request" Message.
    if ( g_reg_file.ddtp.iommu_mode == DDT_Bare ) {
        if ( req->tr.at == ADDR_TYPE_TRANSLATED || 
             req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) {
            cause = 260; // "Transaction type disallowed" 
            goto stop_and_report_fault;
        } 
        pa = req->tr.iova;
        page_sz = PAGESIZE;
        R = W = X = 1;
        G = 0;
        PBMT = PMA;
        goto step_18;
    }
    // 3. If `capabilities.MSI_FLAT` is 0 then the IOMMU uses base-format device
    //    context. Let `DDI[0]` be `device_id[6:0]`, `DDI[1]` be `device_id[15:7]`, and
    //    `DDI[2]` be `device_id[23:16]`.
    if ( g_reg_file.capabilities.msi_flat == 0 ) {
        DDI[0] = get_bits(6,  0, req->device_id);
        DDI[1] = get_bits(15, 7, req->device_id);
        DDI[2] = get_bits(23, 16, req->device_id);
    }
    // 4. If `capabilities.MSI_FLAT` is 0 then the IOMMU uses extended-format device
    //    context. Let `DDI[0]` be `device_id[5:0]`, `DDI[1]` be `device_id[14:6]`, and
    //    `DDI[2]` be `device_id[23:15]`.
    if ( g_reg_file.capabilities.msi_flat == 1 ) {
        DDI[0] = get_bits(5,  0, req->device_id);
        DDI[1] = get_bits(14, 6, req->device_id);
        DDI[2] = get_bits(23, 15, req->device_id);
    }
    // 5. The `device_id` is wider than that supported by the IOMMU mode if any of the
    //    following conditions hold. If the following conditions hold then stop and
    //    report "Transaction type disallowed" (cause = 260).
    //    a. `ddtp.iommu_mode` is `2LVL` and `DDI[2]` is not 0
    //    b. `ddtp.iommu_mode` is `1LVL` and either `DDI[2]` is not 0 or `DDI[1]` is not 0
    if ( g_reg_file.ddtp.iommu_mode == DDT_2LVL && DDI[2] != 0 ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    } 
        
    if ( g_reg_file.ddtp.iommu_mode == DDT_1LVL && (DDI[2] != 0 || DDI[1] != 0) ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    } 

    // 6. Use `device_id` to then locate the device-context (`DC`) as specified in
    //    section 2.4.1 of IOMMU specification.
    if ( locate_device_context(&DC, req->device_id, req->pid_valid, req->process_id, &cause) )
        goto stop_and_report_fault;
    DTF = DC.tc.DTF;

    // 7. if any of the following conditions hold then stop and report
    //    "Transaction type disallowed" (cause = 260).
    //   * Transaction type is a Translated request (read, write/AMO, read-for-execute)
    //     or is a PCIe ATS Translation request and `DC.tc.EN_ATS` is 0.
    //   * Transaction type is a PCIe "Page Request" Message and `DC.tc.EN_PRI` is 0.
    //   * Transaction has a valid `process_id` and `DC.tc.PDTV` is 0.
    //   * Transaction has a valid `process_id` and `DC.tc.PDTV` is 1 and the
    //     `process_id` is wider than supported by `pdtp.MODE`.
    //   * Transaction type is not supported by the IOMMU.
    if ( DC.tc.EN_ATS == 0 && ( req->tr.at == ADDR_TYPE_TRANSLATED || 
                                req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    } 

    if ( req->pid_valid && DC.tc.PDTV == 0 ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    } 

    if ( req->pid_valid && DC.tc.PDTV == 1 ) {
        if ( DC.fsc.pdtp.MODE == PD17 && req->process_id > ((1UL << 17) - 1) ) {
            cause = 260; // "Transaction type disallowed" 
            goto stop_and_report_fault;
        }
        if ( DC.fsc.pdtp.MODE == PD8 && (req->process_id > ((1UL << 8) - 1)) ) {
            cause = 260; // "Transaction type disallowed" 
            goto stop_and_report_fault;
        }
    }


    // 8. If all of the following conditions hold then MSI address translations using
    //    MSI page tables is enabled and the transaction is eligible for MSI address
    //    translation and the MSI address translation process specified in section 2.4.3
    //    is invoked to determine if the `IOVA` is a MSI address and if so translate it.
    //    * `capabilities.MSI_FLAT` (Section 4.3) is 1, i.e., IOMMU support MSI address
    //       translation.
    //    * `IOVA` is a 32-bit aligned address.
    //    * Transaction is a Translated 32-bit write, Untranslated 32-bit write, or is
    //      an ATS translation request.
    //    * Transaction does not have a `process_id` (e.g., PASID present). Transactions
    //       with a `process_id` use a virtual address as IOVA and are not MSI.
    //    * `DC.msiptp.MODE != Bare` i.e., MSI address translation using MSI page tables
    //       is enabled.
    //    If the `IOVA` is determined to be not an MSI then the process continues at
    //    step 9.
    if ( (g_reg_file.capabilities.msi_flat == 1) &&
         ((req->tr.iova & 0x3) == 0) &&
         ((req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) ||
          (req->tr.at == ADDR_TYPE_TRANSLATED && req->tr.length == 4) ||
          (req->tr.at == ADDR_TYPE_UNTRANSLATED && req->tr.length == 4)) &&
         (req->pid_valid == 0) &&
         (DC.msiptp.MODE != MSIPTP_Bare) ) {

        if ( msi_address_translation( req->tr.iova, req->tr.msi_wr_data, req->tr.at, &DC,
              &cause, &pa, &R, &W, &UNTRANSLATED_ONLY, &is_msi, &is_unsup, &is_mrif_wr, &mrif_nid,
              0, 0, 0, 0, req->device_id, 
              ((DC.iohgatp.MODE == IOHGATP_Bare) ? 0 : 1), DC.iohgatp.GSCID) ) {
            goto stop_and_report_fault;
        }
        if ( is_msi == 0 ) goto step_9;
        if ( is_unsup == 1 ) goto return_unsupported_request;

        // MSI address translation completed
        page_sz = PAGESIZE;
        G = 0;
        X = 0;
        PBMT = PMA;
        goto step_18;
    }

step_9:
    // 9. If request is a Translated request and DC.tc.T2GPA is 0 then the translation 
    //    process is complete.  Go to step 18
    if ( (req->tr.at == ADDR_TYPE_TRANSLATED) && (DC.tc.T2GPA == 0) ) {
        pa = req->tr.iova;
        page_sz = PAGESIZE;
        goto step_18;
    }

    //10. If request is a Translated request and DC.tc.T2GPA is 1 then the IOVA is a GPA. 
    //    Go to step 16 with following page table information:
    //    ◦ Let iosatp.MODE be Bare
    //       * The `PSCID` value is not used when first-stage mode is `Bare`.
    //    ◦ Let iohgatp be value in DC.iohgatp field
    if ( (req->tr.at == ADDR_TYPE_TRANSLATED) && (DC.tc.T2GPA == 1) ) {
        iosatp.MODE = IOSATP_Bare;
        SUM = 0;
        iohgatp = DC.iohgatp;
        goto step_16;
    }

    //11. If `DC.tc.pdtv` is set to 0 then go to step 16 with the following 
    //    page table information:
    //    * Let `iosatp.MODE` be value in `DC.fsc.MODE` field
    //    * Let `iosatp.PPN` be value in `DC.fsc.PPN` field
    //    * Let `PSCID` be value in `DC.ta.PSCID` field
    //    * Let `iohgatp` be value in `DC.iohgatp` field 
    //    * If a G-stage page table is not active in the device-context
    //      (`DC.iohgatp.mode` is `Bare`) then `iosatp` is a a S-stage page-table else
    //      it is a VS-stage page table.
    if ( DC.tc.PDTV == 0 ) {
        iosatp.MODE = DC.fsc.iosatp.MODE;
        iosatp.PPN = DC.fsc.iosatp.PPN;
        PSCID = DC.ta.PSCID;
        SUM = 0;
        iohgatp = DC.iohgatp;
        goto step_16;
    }

    //12. If there is no `process_id` associated with the transaction or if
    //    `DC.fsc.pdtp.MODE = Bare` then go to step 16 with the following page table
    //    information:
    //    ◦ Let iosatp.MODE be Bare
    //       * The `PSCID` value is not used when first-stage mode is `Bare`.
    //    ◦ Let iohgatp be value in DC.iohgatp field
    if ( req->pid_valid == 0 || DC.fsc.pdtp.MODE == PDTP_Bare ) {
        iosatp.MODE = IOSATP_Bare;
        SUM = 0;
        iohgatp = DC.iohgatp;
        goto step_16;
    }

    // 13. Locate the process-context (`PC`) as specified in Section 2.4.2
    if ( locate_process_context(&PC, &DC, req->device_id, req->process_id, &cause, &iotval2, TTYP) )
        goto stop_and_report_fault;

    // 14. if any of the following conditions hold then stop and report 
    //     "Transaction type disallowed" (cause = 260).  
    //     a. The transaction requests supervisor privilege but PC.ta.ENS is not set.
    if ( PC.ta.ENS == 0 && req->pid_valid && req->priv_req ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    }

    // 15. Go to step 16 with the following page table information:
    //     * Let `iosatp.MODE` be value in `PC.fsc.MODE` field
    //     * Let `iosatp.PPN` be value in `PC.fsc.PPN` field
    //     * Let `PSCID` be value in `PC.ta.PSCID` field
    //     * Let `iohgatp` be value in `DC.iohgatp` field 
    //     * If a G-stage page table is not active in the device-context
    //       (`DC.iohgatp.mode` is `Bare`) then `iosatp` is a a S-stage page-table else
    //       it is a VS-stage page table.
    iosatp.MODE = PC.fsc.iosatp.MODE;
    iosatp.PPN = PC.fsc.iosatp.PPN;
    PSCID = PC.ta.PSCID;
    SUM = PC.ta.SUM;
    iohgatp = DC.iohgatp;
    goto step_16;

step_16:

    // Miss in IOATC - continue to page table translations
    // 16. If a G-stage page table is not active in the device-context then use the
    //     single stage address translation process specified in Section 4.3.2 of the
    //     RISC-V privileged specification. If a fault is detecting by the single stage
    //     address translation process then stop and report the fault.
    // 17. If a G-stage page table is active in the device-context then use the
    //     two-stage address translation process specified in Section 8.5 of the RISC-V
    //     privileged specification. If a fault is detecting by the single stage address
    //     translation process then stop and report the fault.
    if ( s_vs_stage_address_translation(req->tr.iova, priv, is_read, is_write, is_exec,
                        SUM, iosatp, PSCID, iohgatp, &cause, &iotval2, &pa, &page_sz, &R, &W, &X, &G, 
                        &PBMT, &UNTRANSLATED_ONLY, req->pid_valid, req->process_id, req->device_id,
                        TTYP, DC.tc.T2GPA, DC.tc.SADE, DC.tc.GADE) )
        goto stop_and_report_fault;

step_18:
    // 18. Translation process is complete
    rsp_msg->status          = SUCCESS;
    // The PPN and size is returned in same format as for ATS translation response
    // see below comments on for format details.
    rsp_msg->trsp.PPN        = ((pa & ~(page_sz - 1)) | ((page_sz/2) - 1))/PAGESIZE;
    rsp_msg->trsp.S          = (page_sz > PAGESIZE) ? 1 : 0;
    rsp_msg->trsp.is_msi     = is_msi;
    rsp_msg->trsp.is_mrif_wr = is_mrif_wr;
    rsp_msg->trsp.mrif_nid   = mrif_nid;
    rsp_msg->trsp.PBMT       = PBMT;

    if ( req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
        // When a Success response is generated for a ATS translation request, the setting
        // of the Priv, N, CXL.io, and AMA fields is as follows:
        // * Priv field of the ATS translation completion is always set to 0 if the request
        //   does not have a PASID. When a PASID is present then the Priv field is set to
        //   the value in "Privilege Mode Requested" field as the permissions provided
        //   correspond to those the privilege mode indicate in the request.
        // If Priv is Set, R, W, and Exe refer to permissions granted to entities operating
        // in Privileged Mode in the requesting Function. If Priv is Clear, R, W, and Exe 
        // refer to permissions granted to entities operating in Non-Privileged Mode in the
        // requesting Function.
        // Note: Since the Priv bit is Set only when the requesting Function Sets the 
        // Privileged Mode Requested bit, Functions that never set that bit should always 
        // receive the Priv bit Clear and thus don’t need to cache it.
        // An ATC that receives a translation with R=W=0b for one privilege level may not 
        // assume anything about what it might receive for the other privilege level.
        // * N field of the ATS translation completion is always set to 0. The device may
        //   use other means to determine if the No-snoop flag should be set in the
        //   translated requests.
        // * If requesting device is not a CXL device then CXL.io is set to 0.
        // * If requesting device is a CXL type 1 or type 2 device
        //   ** If the address is determined to be a MSI then the CXL.io bit is set to 1.
        //   ** If the memory type, as determined by the Svpbmt extension, is NC or IO then
        //      the CXL.io bit is set to 1. If the memory type is PMA then the determination
        //      of the setting of this bit is `UNSPECIFIED`. If the Svpbmt extension is not
        //      supported then the setting of this bit is `UNSPECIFIED`.
        //   ** In all other cases the setting of this bit is `UNSPECIFIED`.
        // * The AMA field is by default set to 000b. The IOMMU may support an
        //   implementation specific method to provide other encodings.
        // If S is Set, then the translation applies to a range that is larger than
        // 4096 bytes. If S = 1b, then bit 12 of the Translated Address is used to 
        // indicate whether or not the range is larger than 8192 bytes. If bit 12 
        // is 0b, then the range size is 8192 bytes, but it is larger than 8192 bytes
        // if Set. If S = 1b and bit 12 = 1b, then bit 13 is used to determine if 
        // the range is la than 16384 bytes or not. If bit 13 is 0b, then the range 
        // size is 16384 bytes, but it is larger than 16384 bytes if Set.  Low-order 
        // address bits are consumed in sequence to indicate the size of the range 
        // associated with the translation.
        //                            Address Bits                            |S |Translation
        //                                                                    |  | Range Size
        // 63:32  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 |  | in Bytes
        //-----------------------------------------------------------------------------------
        //   x     x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x |0 |  4K
        //   x     x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  x  0  1  1  1 |1 |  64K
        //   x     x  x  x  x  x  x  x  x  x  x  x  0  1  1  1  1  1  1  1  1 |1 |  2M
        //   x     x  x  x  x  x  x  x  x  x  x  0  1  1  1  1  1  1  1  1  1 |1 |  4M
        //   x     0  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1 |1 |  1G
        // If the translation could be successfully completed but the requested 
        // permissions are not present (Execute requested but no execute permission; 
        // no-write not requested and no write permission; no read permission) then a 
        // Success response is returned with the denied permission (R, W or X) set to 0
        // and the other permission bits set to value determined from the page tables. 
        // The X permission is granted only if the R permission is also granted. 
        // Execute-only translations are not compatible with PCIe ATS as PCIe requires 
        // read permission to be granted if the execute permission is granted.
        // When a Success response is generated for a ATS translation request, no fault 
        // records are reported to software through the fault/event reporting mechanism; 
        // even when the response indicates no access was granted or some permissions 
        // were denied.
        // If the translation request has an address determined to be an MSI address 
        // using the rules defined by the Section 2.1.3.6 but the MSI PTE is configured 
        // in MRIF mode then a Success response is generated with R, W, and U bit set to
        // 1. The U bit being set to 1 in the response instructs the device that it must
        // only use Untranslated requests to access the implied 4 KiB memory range
        rsp_msg->trsp.Priv   = (req->pid_valid && req->priv_req) ? 1 : 0;
        rsp_msg->trsp.CXL_IO = (req->is_cxl_dev && ((PBMT != PMA) || (is_msi == 1))) ? 1 : 0;
        rsp_msg->trsp.N      = 0;
        rsp_msg->trsp.AMA    = 0;
        rsp_msg->trsp.Global = G;
        rsp_msg->trsp.U      = UNTRANSLATED_ONLY;
        rsp_msg->trsp.R      = R;
        rsp_msg->trsp.W      = W;
        rsp_msg->trsp.Exe    = (X & R);
    }
    return;

return_unsupported_request:
    rsp_msg->status = UNSUPPORTED_REQUEST;
    return;

return_completer_abort:
    rsp_msg->status = COMPLETER_ABORT;
    return;

stop_and_report_fault:
    // No faults are logged in the fault queue for PCIe ATS Translation Requests.
    if ( req->tr.at != ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST ) {
        report_fault(cause, iotval, iotval2, TTYP, DTF,
                     req->device_id, req->pid_valid, req->process_id, req->priv_req);
        // Translated and Untranslated requests get UR response
        goto return_unsupported_request;
    }
    // ATS translation requests that encounter a configuration error results in a
    // Completer Abort (CA) response to the requester. The following cause codes
    // belong to this category:
    // * Instruction access fault (cause = 1)
    // * Read access fault (cause = 5)
    // * Write/AMO access fault (cause = 7)
    // * MSI PTE load access fault (cause = 261)
    // * MSI PTE misconfigured (cause = 263)
    // * PDT entry load access fault (cause = 265)
    // * PDT entry misconfigured (cause = 267)
    if ( (cause == 1) || (cause == 5) || (cause == 7) || (cause == 261) || 
         (cause == 263) || (cause == 265) || (cause == 267) )
        goto return_completer_abort;

    // If there is a permanent error or if ATS transactions are disabled then a
    // Unsupported Request (UR) response is generated. The following cause codes
    // belong to this category:
    // * All inbound transactions disallowed (cause = 256)
    // * DDT entry load access fault (cause = 257)
    // * DDT entry not valid (cause = 258)
    // * DDT entry misconfigured (cause = 259)
    // * Transaction type disallowed (cause = 260)
    if ( (cause == 256) || (cause == 257) || (cause == 258) || (cause == 259) || (cause == 260) ) 
        goto return_unsupported_request;

    // When translation could not be completed due to PDT entry being not present, MSI
    // PTE being not present, or first and/or second stage PTE being not present or
    // misconfigured then a Success Response with R and W bits set to 0 is generated.
    // The translated address returned with such completions is undefined. The
    // following cause codes belong to this category:
    // * Instruction page fault (cause = 12)
    // * Read page fault (cause = 13)
    // * Write/AMO page fault (cause = 15)
    // * Instruction guest page fault (cause = 20)
    // * Read guest-page fault (cause = 21)
    // * Write/AMO guest-page fault (cause = 23)
    // * PDT entry not valid (cause = 266)
    // * MSI PTE not valid (cause = 262)
    if ( (cause = 12) || (cause == 13) || (cause == 15) || (cause == 20) || (cause == 21) ||
         (cause == 23) || (cause == 266) || (cause == 262) ) {
        // Return response with R=W=0. The rest of the field are undefined. The reference
        // model sets them to 0.
        R = W = UNTRANSLATED_ONLY = G = pa = 0;
        page_sz = PAGESIZE;
        PBMT = PMA;
        goto step_18;
    }
    *((char *)0) = 0; // unexpected cause
    return;
}
