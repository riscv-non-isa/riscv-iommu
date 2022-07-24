// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_REF_API_H__
#define __IOMMU_REF_API_H__

extern uint8_t read_memory(uint64_t addr, uint8_t size, char *data);
extern uint8_t read_memory_for_AMO(uint64_t address, uint8_t size, char *data);
extern uint8_t write_memory(char *data, uint64_t address, uint8_t size);

extern uint64_t read_register(uint16_t offset, uint8_t num_bytes);
extern void write_register(uint16_t offset, uint8_t num_bytes, uint64_t data);
extern int reset_iommu(uint8_t num_hpm, uint8_t hpmctr_bits, uint16_t eventID_mask, 
                       uint8_t num_vec_bits, uint8_t reset_iommu_mode, 
                       capabilities_t capabilities, fctrl_t fctrl);
extern void iommu_translate_iova(hb_to_iommu_req_t req, iommu_to_hb_rsp_t *rsp_msg);
extern void iommu_handle_message(hb_to_iommu_req_t req, iommu_to_hb_rsp_t *rsp_msg);

extern void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW);
extern void send_msg_iommu_to_hb(ats_msg_t *prgr);

#endif // __IOMMU_REF_API_H__
