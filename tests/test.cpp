#include "test.h"

#include <iostream>

int main()
{
    std::cout << "Hello, CppAnalyze!" << std::endl;

    TemplateFoo<double> double_foo;
    double_foo.int_bar = 2;

    Foo foo;
    foo.double_bar = 2.3;

    return 1;
}
