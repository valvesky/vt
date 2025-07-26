CC := g++
FLAGS := -O2 --fast-math -Wall -Wextra
LINKS := -lSDL2 -lSDL2_ttf

.PHONY: debug

install:
	${CC} ${FLAGS} vt.cpp -o vt ${LINKS}

debug:
	${CC} ${FLAGS} ${LINKS} -DDEBUG -g vt.c -o vt
