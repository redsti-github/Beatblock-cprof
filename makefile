GCC_FLAGS = -fPIC -O3 -flto

cprof.so: cprof.c
	gcc $(GCC_FLAGS) -shared -Llua5.1 $^ -o $@
