// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_PMU_H__
#define __IOMMU_PMU_H__

#ifndef RVI_IOMMU_NO_SHORT_NAMES
#define NO_EVENT             RVI_IOMMU_NO_EVENT
#define UNTRANSLATED_REQUEST RVI_IOMMU_UNTRANSLATED_REQUEST
#define TRANSLATED_REQUEST   RVI_IOMMU_TRANSLATED_REQUEST
#define TRANSLATION_REQUEST  RVI_IOMMU_TRANSLATION_REQUEST
#define IOATC_TLB_MISS       RVI_IOMMU_IOATC_TLB_MISS
#define DDT_WALKS            RVI_IOMMU_DDT_WALKS
#define PDT_WALKS            RVI_IOMMU_PDT_WALKS
#define S_VS_PT_WALKS        RVI_IOMMU_S_VS_PT_WALKS
#define G_PT_WALKS           RVI_IOMMU_G_PT_WALKS
#endif /* RVI_IOMMU_NO_SHORT_NAMES */

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
#define RVI_IOMMU_NO_EVENT             0
#define RVI_IOMMU_UNTRANSLATED_REQUEST 1
#define RVI_IOMMU_TRANSLATED_REQUEST   2
#define RVI_IOMMU_TRANSLATION_REQUEST  3
#define RVI_IOMMU_IOATC_TLB_MISS       4
#define RVI_IOMMU_DDT_WALKS            5
#define RVI_IOMMU_PDT_WALKS            6
#define RVI_IOMMU_S_VS_PT_WALKS        7
#define RVI_IOMMU_G_PT_WALKS           8

void count_events(iommu_t *iommu, uint8_t PV, uint32_t PID, uint8_t PSCV, uint32_t PSCID,
    uint32_t DID, uint8_t GSCV, uint32_t GSCID, uint16_t eventID);
#endif // __IOMMU_PMU_H__
