CC := gcc -O2 --fast-math 

LINKS:= -lSDL3 -ldl -lGL -lm

.PHONY: debug

debug:
	${CC} -g -DDEBUG vt.c -o vt ${LINKS}

install:
	${CC} vt.c -o vt ${LINKS}

