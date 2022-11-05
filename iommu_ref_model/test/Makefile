CFLAGS := -O0 -g -Wall -Werror -I../libiommu/include -I../libtables/include -lgcov --coverage
CC := gcc
SRCS_APP = test_app.c tbapi.c test_utils.c
OBJ_APP = $(SRCS_APP:.c=.o)
iommu: $(OBJ_APP)
	$(CC) -o $@ $^ $(CFLAGS) ../libiommu/libiommu.a ../libtables/libtables.a

clean:
	$(RM) *.o *.a* tags log *.gc* src/*.gcov iommu
