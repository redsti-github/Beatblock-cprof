
cprof.so: cprof.cpp
	g++ -fPIC -c cprof.cpp -o cprof.o -Ofast
	g++ -fPIC -shared -o cprof.so cprof.o -L liblua5.1-c++.so -Ofast -flto
	rm cprof.o
