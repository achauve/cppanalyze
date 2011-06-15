#!/bin/sh

clang -cc1 -load build-debug/libcppanalyze.so -plugin rename -x c++ -std=c++98 -fcxx-exceptions -fexceptions tests/test.cpp
