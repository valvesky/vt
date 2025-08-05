# strict C99
CC := gcc -Wall -Wextra
OPTIMIZATIONS:= -O2 --fast-math 

FLAGS_VK:= --std=c99 -pedantic -lSDL3 -lvulkan
FLAGS_GL:= --std=c99 -D_VT_OPENGL -lSDL3 -ldl -lX11 -lGL -lm

.PHONY: default debug install

default: debug

debug:
	# ./compile_spirv.sh
	${CC} -g -DDEBUG vt.c -o vt ${FLAGS_GL}

install:
	# ./compile_spirv.sh
	${CC} ${OPTIMIZATIONS} vt.c -o vt ${FLAGS_GL}
