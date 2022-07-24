CFLAGS := -fPIE -ftrapv -Wl,nxcompat -fstack-protector-all -Wformat-security -D_FORTIFY_SOURCE=2 -O0 -g -Wall -Werror -fcf-protection=full -Iinclude
CC := gcc
NAME := iommu
SRCS = src/iommu_reg.c src/iommu_translate.c src/iommu_faults.c src/iommu_interrupt.c src/iommu_s_vs_stage_trans.c src/iommu_g_stage_trans.c src/iommu_msi_trans.c src/iommu_device_context.c src/iommu_command_queue.c src/iommu_utils.c src/iommu_atc.c src/iommu_process_context.c src/iommu_ats.c src/iommu_hpm.c
OBJS = $(SRCS:.c=.o)

lib: lib$(NAME).a

lib$(NAME).a: $(OBJS)
	ar rcD $@ $^ > /dev/null 2>&1
	ranlib $@

clean:
	$(RM) src/*.o *.a* tags log
