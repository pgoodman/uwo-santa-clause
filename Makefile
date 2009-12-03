
CC = gcc
CFLAGS = -O0 -g -pedantic -pedantic-errors -Wall -Werror -c -ansi
OBJ_FILE = santaclaus
OBJS = main.o sem.o set.o

all: ${OBJ_FILE} clean

clean:
	-rm *.o

realclean: clean
	-rm ${OBJ_FILE}

${OBJ_FILE}: ${OBJS}
	${CC} -pthread ${OBJS} -o $@

%.o: %.c
	${CC} ${CFLAGS} -c $*.c
