CFLAGS := -fPIE -ftrapv -fstack-protector-all -Wformat-security -D_FORTIFY_SOURCE=2 -O0 -g -Wall -Werror -fcf-protection=full -I../include -I../libtables 
CC := gcc
SRCS_APP = test_app.c
OBJ_APP = $(SRCS_APP:.c=.o)
iommu: $(OBJ_APP)
	$(CC) -static -o $@ $^ $(CFLAGS) ../libiommu/libiommu.a ../libtables/libtables.a

clean:
	$(RM) *.o *.so* iommu tags log
