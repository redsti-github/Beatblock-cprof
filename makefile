GCC_FLAGS = -fPIC -O3 -flto -s

cprof.so: cprof.c
	gcc $(GCC_FLAGS) -shared $^ -o $@

cprof.dll: cprof.c
	x86_64-w64-mingw32-gcc $(GCC_FLAGS) -D WINDOWS -shared -static -static-libgcc -I /mingw64/include/ $^ -o $@ "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Beatblock\\lua51.dll"
