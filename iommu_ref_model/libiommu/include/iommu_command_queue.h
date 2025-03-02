// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_COMMAND_QUEUE_H__
#define __IOMMU_COMMAND_QUEUE_H__
#define IOTINVAL 1
#define IOFENCE  2
#define IODIR    3
#define ATS      4

#define VMA       0
#define GVMA      1

#define INVAL_DDT 0
#define INVAL_PDT 1

#define IOFENCE_C 0

#define INVAL     0
#define PRGR      1
typedef union {
    struct {
        uint64_t opcode:7;
        uint64_t func3:3;
        uint64_t av:1;
        uint64_t rsvd:1;
        uint64_t pscid:20;

        uint64_t pscv:1;
        uint64_t gv:1;
        uint64_t nl:1;
        uint64_t rsvd1:9;
        uint64_t gscid:16;
        uint64_t rsvd2:4;

        uint64_t rsvd3:10;
        uint64_t addr_63_12:52;
        uint64_t rsvd4:2;
    } iotinval;
    struct {
        uint64_t opcode:7;
        uint64_t func3:3;
        uint64_t av:1;
        uint64_t wsi:1;
        uint64_t pr:1;
        uint64_t pw:1;
        uint64_t reserved:18;

        uint64_t data:32;

        uint64_t addr_63_2:62;
        uint64_t reserved1:2;
    } iofence;
    struct {
        uint64_t opcode:7;
        uint64_t func3:3;
        uint64_t rsvd:2;
        uint64_t pid:20;

        uint64_t rsvd1:1;
        uint64_t dv:1;
        uint64_t rsvd2:6;
        uint64_t did:24;

        uint64_t rsvd3;
    } iodir;
    struct {
        uint64_t opcode:7;
        uint64_t func3:3;
        uint64_t rsvd:2;
        uint64_t pid:20;

        uint64_t pv:1;
        uint64_t dsv:1;
        uint64_t rsvd1:6;
        uint64_t rid:16;
        uint64_t dseg:8;

        uint64_t payload;
    } ats;
    struct {
        uint64_t opcode:7;
        uint64_t func3:3;
        uint64_t low:54;
        uint64_t high:64;
    } any;
    struct {
        uint64_t low;
        uint64_t high;
    };
} command_t;

#define CQ_ENTRY_SZ sizeof(command_t)

void do_inval_ddt(uint8_t DV, uint32_t DID);
void do_inval_pdt(uint32_t DID, uint32_t PID);
void do_iotinval_vma(uint8_t GV, uint8_t AV, uint8_t NL, uint8_t PSCV, uint32_t GSCID, uint32_t PSCID, uint64_t ADDR);
void do_iotinval_gvma(uint8_t GV, uint8_t AV, uint8_t NL, uint32_t GSCID, uint64_t ADDR);
void do_ats_msg( uint8_t MSGCODE, uint8_t TAG, uint8_t DSV, uint8_t DSEG, uint16_t RID,
                  uint8_t PV, uint32_t PID, uint64_t PAYLOAD);
uint8_t do_iofence_c(uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WIS_BIT, uint64_t ADDR, uint32_t DATA);
void do_pending_iofence();
void queue_any_blocked_ats_inval_req();
extern uint8_t g_ats_inv_req_timeout;
#endif // __IOMMU_COMMAND_QUEUE_H__
