// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_ATS_H__
#define __IOMMU_ATS_H__
typedef union {
    struct {
        uint64_t reserved0:12;
        uint64_t PID:20;
        uint64_t PV:1;
        uint64_t PRIV:1;
        uint64_t EXEC:1;
        uint64_t reserved1:5;
        uint64_t DID:24;
        uint64_t PAYLOAD;
    };
    uint64_t raw[2];
} page_rec_t;

#define PQ_ENTRY_SZ sizeof(page_rec_t)

// IOMMU generated notifications (invalidation requests and
// page group responses)
// IOMMU response to requests from the IO bridge
// Message Code Routing r[2:0] Type  Description
// 00000001     010            MsgD  Invalidate Request Message, see § Section 10.3.1
// 00000010     010            Msg   Invalidate Completion Message, see § Section 10.3.2
// 00000100     000            Msg   Page Request Message, see § Section 10.4.1
// 00000101     010            Msg   PRG Response Message, see § Section 10.4.2
#define INVAL_REQ_MSG_CODE   0x01
#define INVAL_COMPL_MSG_CODE 0x02
#define PAGE_REQ_MSG_CODE    0x04
#define PRGR_MSG_CODE        0x05
typedef struct {
    uint8_t   MSGCODE;
    uint8_t   TAG;
    uint32_t  RID;
    uint8_t   PV;
    uint32_t  PID;
    uint8_t   PRIV;
    uint8_t   EXEC_REQ;
    uint8_t   DSV;
    uint8_t   DSEG;
    uint64_t  PAYLOAD;
} ats_msg_t;

// Page Request Group Response - response field encoding
//Value      |Status   |Meaning
//-----------+---------+--------------------------------------------------------------------
//0000b      |Success  |All pages within the associated PRG were successfully made resident.
//-----------+---------+--------------------------------------------------------------------
//0001b      |Invalid  |One or more pages within the associated PRG do not exist or requests
//           |Request  |access privilege(s) that cannot be granted. Unless the page mapping
//           |         |associated with the Function is altered, re-issuance of the associated
//           |         |request will never result in success.
//-----------+---------+--------------------------------------------------------------------
//1110b:0010b|Unused   |Unused Response Code values. A Function receiving such a message shall
//           |         |process it as if the message contained a Response Code of
//           |         |Response Failure.
//-----------+---------+--------------------------------------------------------------------
//1111b      | Response|One or more pages within the associated request group have
//           | Failure |encountered/caused a catastrophic error.  This response disables the
//           |         |Page Request Interface at the Function. Any pending page requests for
//           |         |other PRGs will be satisfied at the convenience of the host. The
//           |         |Function shall ignore any subsequent PRG Response Messages, pending
//           |         |re-enablement of the Page Request Interface.
//-----------+---------+--------------------------------------------------------------------
#define PRGR_SUCCESS          0x0UL
#define PRGR_INVALID_REQUEST  0x1UL
#define PRGR_RESPONSE_FAILURE 0xFUL

typedef struct {
    uint8_t  busy;
    uint8_t  DSV;
    uint8_t  DSEG;
    uint16_t RID;
    uint8_t  num_rsp_rcvd;
} itag_tracker_t;

#define MAX_ITAGS 2
extern uint8_t allocate_itag(iommu_t *iommu, uint8_t DSV, uint8_t DSEG, uint16_t RID, uint8_t *itag);
extern void send_msg_iommu_to_hb(ats_msg_t *msg);
extern uint8_t any_ats_invalidation_requests_pending(iommu_t *iommu);
#endif //__IOMMU_ATS_H__
