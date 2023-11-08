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
    uint64_t page_sz, gst_page_sz, pa, gpa;
    uint32_t cause;
    uint64_t iotval2, iotval;
    uint8_t is_msi, is_mrif, DTF, check_access_perms;
    uint32_t mrif_nid;
    uint64_t dest_mrif_addr;
    uint8_t PSCV, GV, PV;
    uint32_t DID, PID, GSCID, PSCID;
    pte_t vs_pte;
    gpte_t g_pte;
    uint8_t ioatc_status, gst_fault;
    uint64_t napot_ppn, napot_iova, napot_gpa;

    // Classify transaction type
    iotval2 = 0;
    iotval = req->tr.iova;
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
        if ( req->exec_req )
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

    is_read = ( req->tr.read_writeAMO == READ ) ? 1 : 0;
    is_write = ( req->tr.read_writeAMO == WRITE ) ?  1 : 0;

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
    is_write = ( (req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) && 
                 (req->no_write == 0) ) ? 1 : is_write; 

    // If a Translation Request has a PASID, the Untranslated Address Field is an address 
    // within the process address space indicated by the PASID field.
    // If a Translation Request has a PASID with either the Privileged Mode Requested 
    // or Execute Requested bit Set, these may be used in constructing the Translation 
    // Completion Data Entry.  The PASID Extended Capability indicates whether a Function
    // supports and is enabled to send and receive TLPs with the PASID.
    is_exec = ( (is_read && req->exec_req &&
                (req->tr.at == ADDR_TYPE_UNTRANSLATED || req->pid_valid)) ) ? 1 : 0;
    priv = ( req->pid_valid && req->priv_req ) ? S_MODE : U_MODE;

    // The process to translate an `IOVA` is as follows:
    // 1. If `ddtp.iommu_mode == Off` then stop and report "All inbound transactions
    //    disallowed" (cause = 256).
    if ( g_reg_file.ddtp.iommu_mode == Off ) {
        cause = 256; // "All inbound transactions disallowed"
        goto stop_and_report_fault;
    }

    // 2. If `ddtp.iommu_mode == Bare` and any of the following conditions hold then
    //    stop and report "Transaction type disallowed" (cause = 260); else go to step
    //    21 with translated address same as the `IOVA`.
    //    a. Transaction type is a Translated request (read, write/AMO, read-for-execute)
    //       or is a PCIe ATS Translation request.
    if ( g_reg_file.ddtp.iommu_mode == DDT_Bare ) {
        if ( req->tr.at == ADDR_TYPE_TRANSLATED || 
             req->tr.at == ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST) {
            cause = 260; // "Transaction type disallowed" 
            goto stop_and_report_fault;
        } 
        pa = req->tr.iova;
        page_sz = PAGESIZE;
        vs_pte.X = vs_pte.W = vs_pte.R = 1;
        vs_pte.PBMT = PMA;
        g_pte.X = g_pte.W = g_pte.R = 1;
        is_msi = 0;
        goto step_20;
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

    // 8. If request is a Translated request and DC.tc.T2GPA is 0 then the translation 
    //    process is complete.  Go to step 21
    if ( (req->tr.at == ADDR_TYPE_TRANSLATED) && (DC.tc.T2GPA == 0) ) {
        pa = req->tr.iova;
        page_sz = PAGESIZE;
        vs_pte.X = vs_pte.W = vs_pte.R = 1;
        vs_pte.PBMT = PMA;
        g_pte.X = g_pte.W = g_pte.R = 1;
        is_msi = 0;
        goto step_20;
    }

    // 9. If request is a Translated request and DC.tc.T2GPA is 1 then the IOVA is a GPA. 
    //    Go to step 17 with following page table information:
    //    ◦ Let A bit the IOVA (the IOVA is a GPA)
    //    ◦ Let iosatp.MODE be Bare
    //       * The `PSCID` value is not used when first-stage mode is `Bare`.
    //    ◦ Let iohgatp be value in DC.iohgatp field
    if ( (req->tr.at == ADDR_TYPE_TRANSLATED) && (DC.tc.T2GPA == 1) ) {
        iosatp.MODE = IOSATP_Bare;
        SUM = 0;
        iohgatp = DC.iohgatp;
        goto step_17;
    }

    //10. If `DC.tc.pdtv` is set to 0 then go to step 16 with the following 
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
        goto step_17;
    }

    //11. If DPE is 1 and there is no process_id associated with the transaction
    //    then let process_id be the default value of 0.
    if ( DC.tc.DPE == 1 && req->pid_valid == 0 ) {
        req->pid_valid = 1;
        req->process_id = 0;
        req->priv_req = 0;
    }

    //12. If DPE is 0 and there is no `process_id` associated with the transaction then
    //    go to step 17 with the following page table information:
    //    ◦ Let iosatp.MODE be Bare
    //       * The `PSCID` value is not used when first-stage mode is `Bare`.
    //    ◦ Let iohgatp be value in DC.iohgatp field
    //13. If DC.fsc.pdtp.MODE = Bare then go to step 17 with the following page
    //    table information:
    //    ◦ Let iosatp.MODE be Bare
    //       * The `PSCID` value is not used when first-stage mode is `Bare`.
    //    ◦ Let iohgatp be value in DC.iohgatp field
    if ( req->pid_valid == 0 || DC.fsc.pdtp.MODE == PDTP_Bare ) {
        iosatp.MODE = IOSATP_Bare;
        SUM = 0;
        iohgatp = DC.iohgatp;
        goto step_17;
    }

    // 14. Locate the process-context (`PC`) as specified in Section 2.4.2
    if ( locate_process_context(&PC, &DC, req->device_id, req->process_id, &cause, &iotval2, TTYP) )
        goto stop_and_report_fault;

    // 15. if any of the following conditions hold then stop and report 
    //     "Transaction type disallowed" (cause = 260).  
    //     a. The transaction requests supervisor privilege but PC.ta.ENS is not set.
    if ( PC.ta.ENS == 0 && req->pid_valid && req->priv_req ) {
        cause = 260; // "Transaction type disallowed" 
        goto stop_and_report_fault;
    }

    // 16. Go to step 17 with the following page table information:
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
    goto step_17;

step_17:
    // 17. Use the process specified in Section "Two-Stage Address Translation" of the
    //     RISC-V Privileged specification cite:[PRIV] to determine the GPA accessed by
    //     the transaction. If a fault is detected by the first stage address translation
    //     process then stop and report the fault. If the translation process is completed
    //     successfully then let `A` be the translated GPA.
    PSCV  = (iosatp.MODE == IOSATP_Bare) ? 0 : 1;
    GV    = (iohgatp.MODE == IOHGATP_Bare) ? 0 : 1;
    GSCID = iohgatp.GSCID;
    PV    = req->pid_valid;
    DID   = req->device_id;
    PID   = req->process_id;
    check_access_perms = ( TTYP != PCIE_ATS_TRANSLATION_REQUEST ) ? 1 : 0;
    if ( (ioatc_status = lookup_ioatc_iotlb(req->tr.iova, check_access_perms, priv, is_read, is_write,
                  is_exec, SUM, PSCV, PSCID, GV, GSCID, &cause, &pa, &page_sz, 
                  &vs_pte, &g_pte)) == IOATC_FAULT )
        goto stop_and_report_fault;

    // Hit in IOATC - complete translation.
    if ( ioatc_status == IOATC_HIT ) goto step_20;

    // Count misses in TLB
    count_events(PV, PID, PSCV, PSCID, DID, GV, GSCID, IOATC_TLB_MISS);
    if ( two_stage_address_translation(req->tr.iova, check_access_perms, DID, is_read, is_write, is_exec,
                                        PV, PID, PSCV, PSCID, iosatp, priv, SUM, DC.tc.SADE,
                                        GV, GSCID, iohgatp, DC.tc.GADE, DC.tc.SXL,
                                        &cause, &iotval2, &gpa, &page_sz, &vs_pte) )
        goto stop_and_report_fault;
    
    // 18. If MSI address translations using MSI page tables is enabled
    //     (i.e., `DC.msiptp.MODE != Off`) then the MSI address translation process
    //     specified in <<MSI_TRANS>> is invoked. If the GPA `A` is not determined to be
    //     the address of a virtual interrupt file then the process continues at step 19.
    //     If a fault is detected by the MSI address translation process then stop and
    //     report the fault else the process continues at step 20.
    if ( msi_address_translation(gpa, is_exec, &DC, &is_msi, &is_mrif, &mrif_nid, &dest_mrif_addr,
                                 &cause, &iotval2, &pa, &gst_page_sz, &g_pte) )
        goto stop_and_report_fault;
    if ( is_msi == 1 ) goto skip_gpa_trans;

    // 19. Use the second-stage address translation process specified in Section
    //     "Two-Stage Address Translation" of the RISC-V Privileged specification
    //     cite:[PRIV] to translate the GPA `A` to determine the SPA accessed by the
    //     transaction. If a fault is detected by the address translation process then
    //     stop and report the fault.
    if ( (gst_fault = second_stage_address_translation(gpa, check_access_perms, DID, 
                          is_read, is_write, is_exec, PV, PID, PSCV, PSCID, GV, GSCID,
                          iohgatp, DC.tc.GADE, DC.tc.SXL, &pa, &gst_page_sz, &g_pte) ) ) {
        if ( gst_fault == GST_PAGE_FAULT ) goto guest_page_fault;
        if ( gst_fault == GST_ACCESS_FAULT ) goto access_fault;
        goto data_corruption;
    }

skip_gpa_trans:
    // The page-based memory types (PBMT), if Svpbmt is supported, obtained 
    // from the IOMMU address translation page tables. When two-stage address
    // translation is performed the IOMMU provides the page-based memory type
    // as resolved between the G-stage and VS-stage page table
    // The G-stage page tables provide the PBMT as PMA, IO, or NC
    // If VS-stage page tables have PBMT as PMA, then G-stage PBMT is used
    // else VS-stage PBMT overrides.
    // The IO bridge resolves the final memory type
    vs_pte.PBMT = ( vs_pte.PBMT != PMA ) ? vs_pte.PBMT : g_pte.PBMT;

    // Updated resp PA if needed based on the resolved S/VS and G-stage page size
    // The page size is smaller of VS or G stage page table size
    page_sz = ( gst_page_sz < page_sz ) ? gst_page_sz : page_sz;
    pa      = (pa & ~(page_sz - 1)) | (req->tr.iova & (page_sz - 1));

    // Cache the translation in the IOATC
    // In the IOTLB the IOVA & PPN is stored in the NAPOT format
    napot_ppn = (((pa & ~(page_sz - 1)) | ((page_sz/2) - 1))/PAGESIZE);
    napot_iova = (((req->tr.iova & ~(page_sz - 1)) | ((page_sz/2) - 1))/PAGESIZE);
    napot_gpa = (((gpa & ~(page_sz - 1)) | ((page_sz/2) - 1))/PAGESIZE);
    if ( req->tr.at == ADDR_TYPE_UNTRANSLATED ) {
        // For Untranslated Requests cache the translations for future re-use
        cache_ioatc_iotlb(napot_iova, GV, PSCV, iohgatp.GSCID, PSCID,
                          &vs_pte, &g_pte, napot_ppn, ((page_sz > PAGESIZE) ? 1 : 0));
    } 
    if ( (TTYP == PCIE_ATS_TRANSLATION_REQUEST) &&
         ((DC.tc.T2GPA == 1 && ((g_fill_ats_trans_in_ioatc & FILL_IOATC_ATS_T2GPA) != 0) ) || 
          ((g_fill_ats_trans_in_ioatc & FILL_IOATC_ATS_ALWAYS) != 0)) ) {
        // If in T2GPA mode, cache the final GPA->SPA translation as 
        // the translated requests may hit on this 
        // If T2GPA is 0, then cache the IOVA->SPA translation if
        // IOMMU has been configured to do so
        cache_ioatc_iotlb((DC.tc.T2GPA == 1) ? napot_gpa : napot_iova,
                                     GV, (DC.tc.T2GPA == 1) ? 0 : PSCV,
                          iohgatp.GSCID, (DC.tc.T2GPA == 1) ? 0 : PSCID,
                          &vs_pte, &g_pte, napot_ppn, ((page_sz > PAGESIZE) ? 1 : 0));
        // Return the GPA as translation response if T2GPA is 1
        pa = (DC.tc.T2GPA == 1) ? gpa : pa;
    }

step_20:
    // 20. Translation process is complete
    rsp_msg->status               = SUCCESS;
    // The PPN and size is returned in same format as for ATS translation response
    // see below comments on for format details.
    rsp_msg->trsp.PPN             = ((pa & ~(page_sz - 1)) | ((page_sz/2) - 1))/PAGESIZE;
    rsp_msg->trsp.S               = (page_sz > PAGESIZE) ? 1 : 0;
    rsp_msg->trsp.is_msi          = is_msi;
    rsp_msg->trsp.is_mrif         = is_msi & is_mrif;
    rsp_msg->trsp.dest_mrif_addr  = dest_mrif_addr;
    rsp_msg->trsp.mrif_nid        = mrif_nid;
    rsp_msg->trsp.PBMT            = vs_pte.PBMT;

    if ( TTYP == PCIE_ATS_TRANSLATION_REQUEST ) {
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
        rsp_msg->trsp.CXL_IO = (req->is_cxl_dev && ((vs_pte.PBMT != PMA) || (is_msi == 1))) ? 1 : 0;
        rsp_msg->trsp.N      = 0;
        rsp_msg->trsp.AMA    = 0;
        rsp_msg->trsp.Global = vs_pte.G;
        rsp_msg->trsp.U      = (is_msi & is_mrif);
        rsp_msg->trsp.R      = (vs_pte.R & g_pte.R);
        rsp_msg->trsp.W      = (vs_pte.W & g_pte.W & vs_pte.D & g_pte.D);
        rsp_msg->trsp.Exe    = (vs_pte.X & g_pte.X & vs_pte.R & g_pte.R);
    }
    return;

return_unsupported_request:
    rsp_msg->status = UNSUPPORTED_REQUEST;
    return;

return_completer_abort:
    rsp_msg->status = COMPLETER_ABORT;
    return;

data_corruption:    
    cause = 274;                 // First/second-stage PT data corruption
    goto stop_and_report_fault;

access_fault:    
    // Stop and raise a access-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) cause = 1;       // Instruction access fault
    else if ( is_read ) cause = 5;  // Read access fault
    else cause = 7;                 // Write/AMO access fault
    goto stop_and_report_fault;

guest_page_fault:
    // Stop and raise a page-fault exception corresponding 
    // to the original access type.
    if ( is_exec ) cause = 20;      // Instruction guest page fault
    else if ( is_read ) cause = 21; // Read guest page fault
    else cause = 23;                // Write/AMO guest page fault
    // If the CAUSE is a guest-page fault then bits 63:2 of the zero-extended
    // guest-physical-address are reported in iotval2[63:2]. If bit 0 of iotval2
    // is 1, then guest-page-fault was caused by an implicit memory access for
    // VS-stage address translation. If bit 0 of iotval2 is 1, and the implicit
    // access was a write then bit 1 of iotval2 is set to 1 else it is set to 0.
    iotval2 = (gpa & ~0x3);
    goto stop_and_report_fault;

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
        vs_pte.raw = g_pte.raw = 0;
        is_msi = 0;
        page_sz = PAGESIZE;
        goto step_20;
    }
    *((char *)0) = 0; // unexpected cause
    return;
}
