#include <iostream>

template<class T>
struct TemplateFoo
{
    int int_bar;
};


struct Foo
{
    double double_bar;
};


int main()
{
    std::cout << "Hello, CppAnalyze!" << std::endl;

    TemplateFoo<double> double_foo;
    double_foo.int_bar = 2;

    Foo foo;
    foo.double_bar = 2.3;

    return 1;
}
