#!/bin/sh

clang  -cc1 -load build-debug/libcppanalyze.so -plugin rename -x c++ -std=c++98 -fcxx-exceptions -fexceptions tests/templates/tu_1.cpp
clang  -cc1 -load build-debug/libcppanalyze.so -plugin rename -x c++ -std=c++98 -fcxx-exceptions -fexceptions tests/templates/tu_2.cpp
clang  -cc1 -load build-debug/libcppanalyze.so -plugin rename -x c++ -std=c++98 -fcxx-exceptions -fexceptions tests/test.cpp

## uncomment to dump the AST in addition to run cppanalyze plugin
#clang  -cc1 -ast-dump -load build-debug/libcppanalyze.so -add-plugin rename -x c++ -std=c++98 -fcxx-exceptions -fexceptions tests/test.cpp
