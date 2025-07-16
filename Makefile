CC := gcc
FLAGS := -O2 --fast-math -Wall -Wextra
LINKS := -lSDL2 -lSDL2_ttf

.PHONY: debug

install:
	${CC} ${FLAGS} ${LINKS} vt.c -o vt

debug:
	${CC} ${FLAGS} ${LINKS} -DDEBUG -g vt_screen2.c vt.c -o vt
