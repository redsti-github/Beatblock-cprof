
cprof.so: cprof.cpp
	g++ -fPIC -c cprof.cpp -o cprof.o -Ofast -g
	g++ -fPIC -shared -o cprof.so cprof.o -L liblua5.1-c++.so -Ofast -g
	rm cprof.o
