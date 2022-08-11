// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#include "iommu.h"
#ifndef __TABLES_API_H__
#define __TABLES_API_H__
uint64_t add_dev_context(device_context_t *DC, uint32_t device_id);
uint64_t add_process_context(device_context_t *DC, process_context_t *PC, uint32_t process_id);
uint64_t add_g_stage_pte(iohgatp_t iohgatp, uint64_t gpa, gpte_t gpte, uint8_t add_level);
uint64_t add_s_stage_pte(iosatp_t satp, uint64_t va, pte_t pte, uint8_t add_level);
uint64_t add_vs_stage_pte(iosatp_t satp, uint64_t va, pte_t pte, uint8_t add_level, iohgatp_t iohgatp);
uint8_t translate_gpa (iohgatp_t iohgatp, uint64_t gpa, uint64_t *spa);
void print_dev_context(device_context_t *DC, uint32_t device_id);
void print_process_context(process_context_t *DC, uint32_t device_id, uint32_t process_id);



extern uint64_t get_free_ppn(uint64_t num_ppn);
extern uint64_t get_free_gppn(uint64_t num_gppn, iohgatp_t iohgatp);

#endif // __TABLES_API_H__
