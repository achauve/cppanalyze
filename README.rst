================
CppAnalyze
================

CppAnalyze is a `Clang`_-based C++ project to analyze C++ code.


The goal of this project is to provide a tool to analyze the code of a c++ project,
so as to:

 * rename members, functions, ... according to some project conventions
 * detect bad constructs not wanted in the project and emit a warning/error
 * correct the simplest ones


.. note::
   Currently CppAnalyze is hardly more than a Clang plugin example, and
   still in the state of a prototype.


Build
=====

You need a recent version of LLVM and Clang (the best is to get the svn
version, see the Clang getting started page) compiled with cmake.

In the directory where you cloned/extracted CppAnalyze::

    mkdir build-debug
    cd build-debug
    ccmake ..
    make


Test
====

For now, just run test.sh.


References
==========

* A really nice and up-to-date tutorial on Clang: http://www.cs.bgu.ac.il/~mirskyil/CsWiki/Blogs/Post_Mirskyil_4c83cc1f85da2
* Some notes on using Clang fro chromium: http://code.google.com/p/chromium/wiki/WritingClangPlugins
* A clang plugin for chromium to detect bad c++ constructs: http://src.chromium.org/viewvc/chrome/trunk/src/tools/clang/plugins/
* A very interesting project based on Clang: http://code.google.com/p/include-what-you-use/


License
=======

This code is released under the MIT License (see LICENSE.TXT for details).

Copyright © 2011 Adrien Chauve


.. _Clang: http://clang.llvm.org
