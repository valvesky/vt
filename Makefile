CC := gcc --std=c99 -mbmi -Wall -Wextra -Wmissing-declarations
OPTIMIZATIONS:= -O2 -msse2 --fast-math 

FLAGS_VK:= -pedantic -lSDL3 -lvulkan
FLAGS_GL:= -D_VT_OPENGL -lSDL3 -ldl -lX11 -lGL -lm

.PHONY: default debug install

default: debug

debug:
	# ./compile_spirv.sh
	${CC} -g -DDEBUG vt.c -o vt ${FLAGS_GL}

install:
	# ./compile_spirv.sh
	${CC} ${OPTIMIZATIONS} vt.c -o vt ${FLAGS_GL}
