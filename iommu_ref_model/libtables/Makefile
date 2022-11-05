CFLAGS := -O0 -g -Wall -Werror -I../libiommu/include/ -Iinclude
CC := gcc
NAME := tables
SRCS = src/build_ddt.c src/build_pdt.c src/build_g_stage_pt.c src/build_s_stage_pt.c src/build_vs_stage_pt.c src/translate_gpa.c
OBJS = $(SRCS:.c=.o)

lib: lib$(NAME).a

lib$(NAME).a: $(OBJS)
	ar rcD $@ $^ > /dev/null 2>&1
	ranlib $@

OBJ_APP = $(SRCS_APP:.c=.o)

clean:
	$(RM) src/*.o *.a* tags log
