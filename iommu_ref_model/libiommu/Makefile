CFLAGS := -g -Wall -Werror -Iinclude -fprofile-arcs -ftest-coverage 
CC := gcc
NAME := iommu
SRCS = src/iommu_reg.c src/iommu_translate.c src/iommu_faults.c src/iommu_interrupt.c src/iommu_two_stage_trans.c src/iommu_second_stage_trans.c src/iommu_msi_trans.c src/iommu_device_context.c src/iommu_command_queue.c src/iommu_utils.c src/iommu_atc.c src/iommu_process_context.c src/iommu_ats.c src/iommu_hpm.c
OBJS = $(SRCS:.c=.o)

lib: lib$(NAME).a

lib$(NAME).a: $(OBJS)
	ar rcD $@ $^ > /dev/null 2>&1
	ranlib $@

clean:
	$(RM) src/*.o *.a* tags log src/*.gc* src/*.gcov *.gcov
