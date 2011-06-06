#ifndef CPPANALYZE_TEST_H
#define CPPANALYZE_TEST_H

template<class T>
struct TemplateFoo
{
    int int_bar;

    void inlineSetBar(int v)
    {
        int_bar = v;
    }

    TemplateFoo():
        int_bar(0)
    {
        int_bar = 0;
    }

    TemplateFoo(const TemplateFoo<T>& rhs):
        int_bar(rhs.int_bar)
    {
        int_bar = rhs.int_bar;
    }
};


struct Foo
{
    Foo():
        double_bar(0.)
    {}

    Foo(const Foo& rhs):
        double_bar(rhs.double_bar)
    {
        double_bar = rhs.double_bar;
    }

    double double_bar;

    void inlineSetBar(double v)
    {
        double_bar = v;
    }

    void setBar(double v);
};

#endif // CPPANALYZE_TEST_H
