# RISC-V IOMMU golden reference model
This code implements the RISC-V IOMMU specification - https://github.com/riscv-non-isa/riscv-iommu - and
is intended to be a golden reference model for the specification.

# Files organization
- libiommu  - Files implementing the specification
- libtables - a support library to build page and directory tables
- test      - a sample test application illustrating how to invoke and use libiommu and libtables

# Building and Running tests
The project builds two libraries libiommu.a and libtables.a. The test application links to both
the libraries and uses the API provided by libtables to build the memory resident tables.

The test application may be run by invoking `make run`. The test application should print the status
of the tests and a coverage report on the reference model. The coverage report may be used to guide
generation of further tests.

The reference model, may be used in conjuction with a IOMMU to verify the behavior of the IOMMU. For
example, the tests may provide identical stimulus to the IOMMU reference model and the IOMMU and 
compare the results. The debug interface of the IOMMU may be used to provide stimulus when its simpler
not to use real IO devices to generate the stimulus.

The stock test application generates the following output.
```bash
 ___ ___  __  __ __  __ _   _       _____         _
|_ _/ _ \|  \/  |  \/  | | | |     |_   _|__  ___| |_ ___
 | | | | | |\/| | |\/| | | | |       | |/ _ \/ __| __/ __|
 | | |_| | |  | | |  | | |_| |       | |  __/\__ \ |_\__ \
|___\___/|_|  |_|_|  |_|\___/        |_|\___||___/\__|___/

Running IOMMU test suite
Test 01 : All inbound transactions disallowed      : PASS
Test 02 : Bare mode tests                          : PASS
Test 03 : Too wide device_id                       : PASS
Test 04 : Non-leaf DDTE invalid                    : PASS
:
:
Test 27 : Illegal commands and CQ mem faults       : PASS
Test 28 : Sv32 mode                                : PASS
Test 29 : Misc. Register Access tests              : PASS

Coverage report
File 'src/iommu_atc.c'                             :100.00%
File 'src/iommu_ats.c'                             :100.00%
File 'src/iommu_command_queue.c'                   :100.00%
:
:
File 'src/iommu_reg.c'                             :100.00%
File 'src/iommu_s_vs_stage_trans.c'                :100.00%
File 'src/iommu_translate.c'                       :100.00%
File 'src/iommu_utils.c'                           :100.00%

```

# Reference model API
To drive the reference mode, the model exposes the following APIs that must be
implemented by the test bench:
1. uint8_t read_memory(uint64_t address, uint8_t size, char *data);

This function is used by the reference model to read memory. The bit 0 of the 
return value may be set to 1 to indicate an access violation and the bit 1 set 
to 1 to indicate that the data returned is corrupted and thus must cause a data 
corruption fault. If neither bits are set then the read is assumed to be 
successful and the data bytes placed in the buffer identified by the data 
parameter.

2. uint8_t read_memory_for_AMO(uint64_t address, uint8_t size, char *data);

This API works is used by the reference model to read memory for an atomic memory
operation but is otherwise identical to the read_memory API.

3. uint8_t write_memory(char *data, uint64_t address, uint32_t size);

This function is used by the reference model to write memory. The bit 0 of the 
return value may be set to 1 to indicate an access violation. If the return value is
0 then the write is assumed to be successful and the data bytes in the buffer 
identified by the data parameter are written to the memory identified by the address
parameter.

4. uint64_t read_register(uint16_t offset, uint8_t num_bytes);

This function is provided by the reference model to read a memory mapped IOMMU
register. The register is identified by the offset parameter and the number of bytes
read is identified by the num_bytes parameter. The register data is returned by the
function. If the access is invalid then the function returns all 1's i.e. an abort
response.

5. void write_register(uint16_t offset, uint8_t num_bytes, uint64_t data);

This function is provided by the reference model to write a memory mapped IOMMU
register. The register is identified by the offset parameter and the number of bytes
written is identified by the num_bytes parameter. The data to be written is provided
in the data parameter. If the access is invalid then the function drops the write i.e.
an abort response.

6. int reset_iommu(uint8_t num_hpm, uint8_t hpmctr_bits, uint16_t eventID_mask, 
                       uint8_t num_vec_bits, uint8_t reset_iommu_mode, 
                       capabilities_t capabilities, fctrl_t fctrl);

This function is provided by the reference model to establish the resset default state.
The num_hpm indicates the number of hardware performace monitoring counters to be 
implemented by the model and the hpmctr_bits indicates the width of the counters in bits.
The eventID_mask indicates the width of the eventID field in the hardware performance
monitoring event registers that should be implemented by the reference model. The 
num_vec_bits indicates the width in bits of the vector field of the ICVEC register. The
default IOMMU mode - Off or Bare - is selected by the reset_iommu_mode parameter. The 
capabilities of the IOMMU that should be implemented are specified by the capabilities
parameter. The default value of the feature control register is provided by the fctrl
parameter. The function returns 0 if the reference model could be successfully initialized
with the provided parameters.

7. void iommu_translate_iova(hb_to_iommu_req_t *req, iommu_to_hb_rsp_t *rsp_msg);

This function is used by the test bench to invoke the translation request interface in the
IOMMU. The translation response is returned in the buffer pointed to by rsp_msg.

8. void handle_page_request(ats_msg_t *pr);

This function is used by the test bench to send a page request message to the IOMMU.

9. uint8_t handle_invalidation_completion(ats_msg_t *inv_cc);

This function is used by the test bench to send a invalidation completion message to the IOMMU.

10. void do_ats_timer_expiry(uint32_t itag_vector);

This function is used by the test bench to signal a timeout for one or more ATS invalidation
requests sent by the IOMMU. 

11. void process_commands(void);

This function when invoked causes the IOMMU to process a command from the command queue. This
function acts like a "clock" and in each invocation processes one command. If multiple command
processing is required then the function should be invoked for each command.

12. void iommu_to_hb_do_global_observability_sync(uint8_t PR, uint8_t PW);

This function is invoked by the IOMMU to request the test bench to perform the global observability
actions for previous reads if PR is 1 and for previous writes if PW is 1. When the function returns
the IOMMU model assumes that all previous read and/or write are globally observed as requested.

13. void send_msg_iommu_to_hb(ats_msg_t *prgr);

This function is invoked by the IOMMU to send a ATS message - Invalidation request or a page request
group response - to the test bench.




