// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#ifndef __TABLES_API_H__
#define __TABLES_API_H__
uint8_t add_dev_context( device_context_t *DC, uint32_t device_id);
uint8_t add_g_stage_pte( iohgatp_t iohgatp, uint64_t gpa, gpte_t gpte, uint8_t add_level);
#endif // __TABLES_API_H__
