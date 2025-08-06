// Copyright (c) 2022 by Rivos Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Author: ved@rivosinc.com

// Global functions
extern int8_t reset_system(uint8_t mem_gb, uint16_t num_vms);
extern int8_t enable_cq(uint32_t nppn);
extern int8_t enable_fq(uint32_t nppn);
extern int8_t enable_disable_pq(uint32_t nppn, uint8_t enable_disable);
extern int8_t enable_iommu(uint8_t iommu_mode);
extern void iodir(uint8_t f3, uint8_t DV, uint32_t DID, uint32_t PID);
extern void iotinval( uint8_t f3, uint8_t GV, uint8_t AV, uint8_t PSCV, uint32_t GSCID, uint32_t PSCID, uint64_t address);
extern void iofence(uint8_t f3, uint8_t PR, uint8_t PW, uint8_t AV, uint8_t WSI_bit, uint64_t addr, uint32_t data);
extern void ats_command( uint8_t f3, uint8_t DSV, uint8_t PV, uint32_t PID, uint8_t DSEG, uint16_t RID, uint64_t payload);
extern void generic_any(command_t cmd);
extern void send_translation_request(uint32_t did, uint8_t pid_valid, uint32_t pid, uint8_t no_write,
             uint8_t exec_req, uint8_t priv_req, uint8_t is_cxl_dev, addr_type_t at, uint64_t iova,
             uint32_t length, uint8_t read_writeAMO,
             hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp);
extern int8_t check_rsp_and_faults(hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp, status_t status,
             uint16_t cause, uint64_t exp_iotval2);
extern int8_t check_faults(uint16_t cause, uint8_t  exp_PV, uint32_t exp_PID,
             uint8_t  exp_PRIV, uint32_t exp_DID, uint64_t exp_iotval, uint8_t ttyp, uint64_t exp_iotval2);
extern int8_t check_exp_pq_rec(uint32_t DID, uint32_t PID, uint8_t PV, uint8_t PRIV, uint8_t EXEC,
             uint16_t reserved0, uint8_t reserved1, uint64_t PAYLOAD);
extern uint64_t get_free_gppn(uint64_t num_gppn, iohgatp_t iohgatp);
extern uint64_t add_device(uint32_t device_id, uint32_t gscid, uint8_t en_ats, uint8_t en_pri, uint8_t t2gpa,
           uint8_t dtf, uint8_t prpr,
           uint8_t gade, uint8_t sade, uint8_t dpe, uint8_t sbe, uint8_t sxl, uint8_t gipc,
           uint8_t iohgatp_mode, uint8_t iosatp_mode, uint8_t pdt_mode,
           uint8_t msiptp_mode, uint8_t msiptp_pages, uint64_t msi_addr_mask,
           uint64_t msi_addr_pattern);

// Global variables
extern ats_msg_t exp_msg;
extern ats_msg_t rcvd_msg;
extern uint8_t exp_msg_received;
extern uint8_t message_received;
extern int8_t *memory;
extern uint64_t access_viol_addr;
extern uint64_t data_corruption_addr;
extern uint8_t pr_go_requested;
extern uint8_t pw_go_requested;
extern uint64_t next_free_page;
extern uint64_t next_free_gpage[65536];

// Macros
#define FOR_ALL_TRANSACTION_TYPES(at, pid_valid, exec_req, priv_req, no_write, code)\
    for ( at = 0; at < 3; at++ ) {\
        for ( pid_valid = 0; pid_valid < 2; pid_valid++ ) {\
            for ( exec_req = 0; exec_req < 2; exec_req++ ) {\
                for ( priv_req = 0; priv_req < 2; priv_req++ ) {\
                    for ( no_write = 0; no_write < 2; no_write++ )  {\
                        code\
                    }\
                }\
            }\
        }\
    }

#define START_TEST(__STR__)\
    test_num++;\
    printf("Test %02d : %-40s : ", test_num, __STR__);
#define fail_if(__COND__) if __COND__ {printf("\x1B[31mFAIL. Line %d\x1B[0m\n", __LINE__); return -1;}
#define END_TEST() {printf("\x1B[32mPASS\x1B[0m\n");}
