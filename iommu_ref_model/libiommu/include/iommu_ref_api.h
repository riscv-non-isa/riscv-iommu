// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com
#ifndef __IOMMU_REF_API_H__
#define __IOMMU_REF_API_H__

extern uint8_t read_memory(uint64_t addr, uint8_t size, char *data,
                           uint32_t rcid, uint32_t mcid, uint32_t pma, int endian);
extern uint8_t read_memory_for_AMO(uint64_t address, uint8_t size, char *data,
                                   uint32_t rcid, uint32_t mcid, uint32_t pma,
                                   int endian);
extern uint8_t write_memory(char *data, uint64_t address, uint32_t size,
                            uint32_t rcid, uint32_t mcid, uint32_t pma,
                            int endian);
extern uint8_t read_memory_test(uint64_t addr, uint8_t size, char *data);
extern uint8_t write_memory_test(char *data, uint64_t address, uint32_t size);

extern uint64_t read_register(iommu_t *iommu, uint16_t offset, uint8_t num_bytes);
extern void write_register(iommu_t *iommu, uint16_t offset, uint8_t num_bytes, uint64_t data);

#define FILL_IOATC_ATS_T2GPA  0x01
#define FILL_IOATC_ATS_ALWAYS 0x02
extern int reset_iommu(iommu_t *iommu, uint8_t num_hpm, uint8_t hpmctr_bits, uint16_t eventID_limit,
                       uint8_t num_vec_bits, uint8_t reset_iommu_mode,
                       uint8_t max_iommu_mode, uint32_t max_devid_mask,
                       uint8_t gxl_writeable, uint8_t fctl_be_writeable,
                       uint8_t fill_ats_trans_in_ioatc, capabilities_t capabilities,
                       fctl_t fctl, uint64_t sv57_bare_pg_sz, uint64_t sv48_bare_pg_sz,
                       uint64_t sv39_bare_pg_sz, uint64_t sv32_bare_pg_sz,
                       uint64_t sv57x4_bare_pg_sz, uint64_t sv48x4_bare_pg_sz,
                       uint64_t sv39x4_bare_pg_sz, uint64_t sv32x4_bare_pg_sz);
extern void iommu_translate_iova(iommu_t *iommu, hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp_msg);
extern void handle_page_request(iommu_t *iommu, ats_msg_t *pr);
extern uint8_t handle_invalidation_completion(iommu_t *iommu, ats_msg_t *inv_cc);
extern void do_ats_timer_expiry(iommu_t *iommu, uint32_t itag_vector);
extern void process_commands(iommu_t *iommu);

extern void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW);
extern void send_msg_iommu_to_hb(ats_msg_t *prgr);
extern void get_attribs_from_req(hb_to_iommu_req_t *req, uint8_t *read,
                                 uint8_t *write, uint8_t *exec, uint8_t *priv);

#endif // __IOMMU_REF_API_H__
