#! /usr/bin/make -f

all: ../target/directio.so

../target/directio.so: main.c
	mkdir -p ../target
	/bin/bash -c 'gcc -I$$JAVA_HOME/include{,/linux} -o ../target/directio.so -shared main.c -lc -std=c99 -fPIC -D_GNU_SOURCE'

