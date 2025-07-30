CC := gcc -O2 --fast-math 

PKGS:=freetype2
CFLAGS:=`pkg-config --cflags ${PKGS}`
LINKS:= -lSDL3 -ldl -lGL -lm -lfreetype ${CFLAGS}

.PHONY: debug

install:
	${CC} vt.c -o vt ${LINKS}

debug:
	${CC} -g -DDEBUG vt.c -o vt ${LINKS}
