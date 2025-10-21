// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_PMU_H__
#define __IOMMU_PMU_H__
// The following table lists the standard events that can be counted:
// | *eventID*  | *Event counted*              | *IDT settings supported*
// | 0          | Do not count                 |
// | 1          | Untranslated requests        | 0
// | 2          | Translated requests          | 0
// | 3          | ATS Translation requests     | 0
// | 4          | TLB miss                     | 0/1
// | 5          | Device Directory Walks       | 0
// | 6          | Process Directory Walks      | 0
// | 7          | S/VS-stage Page Table Walks  | 0/1
// | 8          | G-stage Page Table Walks     | 0/1
// | 9 - 16383 | reserved for future standard | -
#define NO_EVENT             0
#define UNTRANSLATED_REQUEST 1
#define TRANSLATED_REQUEST   2
#define TRANSLATION_REQUEST  3
#define IOATC_TLB_MISS       4
#define DDT_WALKS            5
#define PDT_WALKS            6
#define S_VS_PT_WALKS        7
#define G_PT_WALKS           8

void count_events(iommu_t *iommu, uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    uint32_t DID, uint8_t GSCV, uint32_t GSCID, uint16_t eventID);
#endif // __IOMMU_PMU_H__
