// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_INTERRUPT_H__
#define __IOMMU_INTERRUPT_H__

#ifndef RVI_IOMMU_NO_SHORT_NAMES
#define COMMAND_QUEUE           RVI_IOMMU_COMMAND_QUEUE
#define FAULT_QUEUE             RVI_IOMMU_FAULT_QUEUE
#define HPM                     RVI_IOMMU_HPM
#define PAGE_QUEUE              RVI_IOMMU_PAGE_QUEUE
#define MSI_VEC_CTRL_MASK_BIT   RVI_IOMMU_MSI_VEC_CTRL_MASK_BIT
#endif /* RVI_IOMMU_NO_SHORT_NAMES */

#define RVI_IOMMU_COMMAND_QUEUE 0
#define RVI_IOMMU_FAULT_QUEUE   1
#define RVI_IOMMU_HPM           2
#define RVI_IOMMU_PAGE_QUEUE    3

#define RVI_IOMMU_MSI_VEC_CTRL_MASK_BIT 1

extern void generate_interrupt(iommu_t *iommu, uint8_t unit);
extern void release_pending_interrupt(iommu_t *iommu, uint8_t vec);
#endif // __IOMMU_INTERRUPT_H__
