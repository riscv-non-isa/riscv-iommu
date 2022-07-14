CFLAGS := -fPIE -ftrapv -Wl,nxcompat -fstack-protector-all -Wformat-security -D_FORTIFY_SOURCE=2 -O0 -g -Wall -Werror -fcf-protection=full -I../include/
CC := gcc
NAME := tables
SRCS = build_ddt.c build_iohgatp.c
OBJS = $(SRCS:.c=.o)

lib: lib$(NAME).a

lib$(NAME).a: $(OBJS)
	ar rcD $@ $^ > /dev/null 2>&1
	ranlib $@

OBJ_APP = $(SRCS_APP:.c=.o)

clean:
	$(RM) *.o *.a* tags log
