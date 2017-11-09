#!/bin/bash
cd test/
cd false_sharing_test/

make -j 5
if [[ $? -ne 0 ]]; then
	exit 1
fi

cd ../false_sharing_valgrind/

make -j 5
if [[ $? -ne 0 ]]; then
	exit 1
fi

cd ../../valgrind-3.13.0/
./fdo.sh
