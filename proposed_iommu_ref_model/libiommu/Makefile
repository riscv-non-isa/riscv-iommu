CFLAGS := -fPIE -ftrapv -Wl,nxcompat -fstack-protector-all -Wformat-security -D_FORTIFY_SOURCE=2 -O0 -g -Wall -Werror -fcf-protection=full -I../include
CC := gcc
NAME := iommu
SRCS = iommu_reg.c iommu_translate.c iommu_faults.c iommu_interrupt.c iommu_s_vs_stage_trans.c iommu_g_stage_trans.c iommu_msi_trans.c iommu_device_context.c iommu_command_queue.c iommu_utils.c iommu_atc.c iommu_process_context.c iommu_ats.c iommu_pmu.c
OBJS = $(SRCS:.c=.o)

lib: lib$(NAME).a

lib$(NAME).a: $(OBJS)
	ar rcD $@ $^ > /dev/null 2>&1
	ranlib $@

clean:
	$(RM) *.o *.a* tags log
