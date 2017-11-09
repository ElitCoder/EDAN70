#!/bin/bash

cd test/false_sharing_test/
make clean

cd ../false_sharing_valgrind/
make clean

cd ../../valgrind-3.13.0/
make clean
make distclean
rm -rf inst/
rm -rf autom4te.cache/
