#!/bin/make

CROSS_COMPILE ?= 

CC ?= ${CROSS_COMPILE}gcc
LD ?= ${CROSS_COMPILE}ld
OBJCOPY ?= ${CROSS_COMPILE}objcopy

INC ?= -I/home/user/work/linux-work/caf/install/include/
LIBS ?= 

CFLAGS = -Wall -O0 -g -funsigned-char -I. ${INC}
LDFLAGS = -g -O0 -Wl,--gc-sections,--relax -L/usr/local/lib ${LIBS}


TARGET=sim_test


all: ${TARGET}

OBJS= \
	sim_test.o


	

$(OBJS): %.o: %.c ${DEPS}
	${CC} ${CFLAGS} -c $< -o $@

${TARGET}: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} -o $@


.PHONY: exec
exec: ${TARGET}
	./${TARGET}

.PHONY: clean
clean:
	rm -f ${OBJS} ${TARGET}
