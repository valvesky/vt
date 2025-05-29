CC := gcc
FLAGS := -O2 --fast-math -Wall -Wextra
LINKS := -lSDL2 -lSDL2_ttf

.PHONY: install

install:
	${CC} ${FLAGS} ${LINKS} -DDEBUG vt_shell.c vt.c -o vt
