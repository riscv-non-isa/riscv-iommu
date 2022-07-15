// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_COMMAND_QUEUE_H__
#define __IOMMU_COMMAND_QUEUE_H__
#define IOTINVAL 1
#define IODIR    2
#define IOFENCE  3
#define ATS      4

#define VMA       0
#define GVMA      1
#define INVAL_DDT 0
#define INVAL_PDT 1
#define IOFENCE_C 0
#define INVAL     0
#define PRGR      1
typedef struct {
    uint64_t low;
    uint64_t high;
} command_t;

void do_inval_ddt(uint8_t DV, uint32_t DID);
void do_inval_pdt(uint32_t DID, uint32_t PID);
void do_iotinval_vma(uint8_t GV, uint8_t AV, uint8_t PSCV, uint32_t GSCID, uint32_t PSCID, uint64_t ADDR);
void do_iotinval_gvma(uint8_t GV, uint8_t AV, uint32_t GSCID, uint64_t ADDR);
void do_ats_msg( uint8_t MSGCODE, uint8_t TAG, uint8_t DSV, uint8_t DSEG, uint16_t RID, 
                  uint8_t PV, uint32_t PID, uint64_t PAYLOAD);
void do_iofence_c(uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WIS_BIT, uint64_t ADDR, uint32_t DATA);
void do_pending_iofence();
void queue_any_blocked_ats_inval_req();
extern uint8_t g_ats_inv_req_timeout;
#endif // __IOMMU_COMMAND_QUEUE_H__

