#!/bin/bash

if [ -f Makefile ]; then
	make -j 5

	if [[ $? -ne 0 ]]; then
    	exit 1
	fi
fi

if [ ! -f Makefile ]; then
	./autogen.sh
	./configure --prefix=`pwd`/inst

	make clean
	set -e
	make -j 5
fi

#make clean
#make distclean

#./autogen.sh
#./configure --prefix=`pwd`/inst

make -j 5 install
if [[ $? -ne 0 ]]; then
	exit 1
fi

./inst/bin/valgrind --tool=falsegrind ../test/false_sharing_valgrind/false_sharing_valgrind
