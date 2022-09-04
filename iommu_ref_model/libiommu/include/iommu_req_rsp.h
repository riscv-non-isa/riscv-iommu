// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_REQ_RSP_H__
#define __IOMMU_REQ_RSP_H__

typedef enum {
    // 00 - Untranslated  - IOMMU may treat the address as either virtual or physical.
    // 01 - Trans. Req.   - The IOMMU will return the translation of the address
    //                      contained in the address field of the request as a read
    //                      completion. 
    // 10 - Translated    - The address in the transaction has been translated by an IOMMU. 
    //                      If the Function associated with the device_id is allowed to 
    //                      present physical addresses to the system memory, then the IOMMU
    //                      might not translate this address. If the Function is not allowed
    //                      to present physical addresses, then the TA may treat this as an UR.
    ADDR_TYPE_UNTRANSLATED = 0,
    ADDR_TYPE_PCIE_ATS_TRANSLATION_REQUEST = 1,
    ADDR_TYPE_TRANSLATED = 2
} addr_type_t;
#define READ      0
#define WRITE     1
typedef struct {
    addr_type_t at;
    uint64_t    iova;
    uint32_t    length;
    uint8_t     read_writeAMO;
    uint32_t    msi_wr_data;
} iommu_trans_req_t;

// Request to IOMMU from the host bridge
typedef struct {
    // Device ID input
    uint32_t device_id;
    // Process ID input (e.g. PASID present)
    uint8_t  pid_valid;
    uint32_t process_id;
    uint8_t  no_write;
    uint8_t  exec_req;
    uint8_t  priv_req;
    uint8_t  is_cxl_dev;
    // Translation request
    iommu_trans_req_t tr;
} hb_to_iommu_req_t;

// Translation completion status
typedef enum {
    // This Completion Status has a nominal meaning of “success”.
    SUCCESS = 0,

    // A status that applies to a posted or non-posted Request 
    // that specifies some action or access to some space that 
    // is not supported by the Completer. 
    // OR
    // A status indication returned with a Completion for a 
    // non-posted Request that suffered an Unsupported Request
    // at the Completer.
    UNSUPPORTED_REQUEST = 1,

    // A status that applies to a posted or non-posted Request 
    // that the Completer is permanently unable to complete 
    // successfully, due to a violation of the Completer’s 
    // programming model or to an unrecoverable error associated
    // with the Completer.
    // OR
    // A status indication returned with a Completion for a 
    // non-posted Request that suffered a Completer Abort at the
    // Completer.
    COMPLETER_ABORT = 4
} status_t;

// Translation response from iommu to host bridge
typedef struct {
    uint64_t PPN;
    uint8_t S;
    uint8_t N;
    uint8_t CXL_IO;
    uint8_t Global;
    uint8_t Priv;
    uint8_t U;
    uint8_t R;
    uint8_t W;
    uint8_t Exe;
    uint8_t AMA;
    uint8_t PBMT;
    uint8_t is_msi;
    uint8_t is_mrif_wr;
    uint16_t mrif_nid;
} iommu_trans_rsp_t;

// IOMMU response to requests from the IO bridge
typedef struct {
    status_t          status;
    iommu_trans_rsp_t trsp;
} iommu_to_hb_rsp_t;

#endif // __IOMMU_REQ_RSP_H__
