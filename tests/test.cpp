#include "test.h"

#include <iostream>
#include <vector>
#include <cassert>

void Foo::setBar(double v)
{
    double_bar = v;
}

int main()
{
    std::cout << "Hello, CppAnalyze!" << std::endl;

    TemplateFoo<double> double_foo;
    double_foo.int_bar = 2;

    assert(double_foo.int_bar == 2);

    Foo foo;
    foo.double_bar = 2.3;

    std::vector<int> arr(10);
    arr[0] = 1;

    return 1;
}
