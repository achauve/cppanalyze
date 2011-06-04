#ifndef CPPANALYZE_TEST_H
#define CPPANALYZE_TEST_H

template<class T>
struct TemplateFoo
{
    int int_bar;
};


struct Foo
{
    Foo():
        double_bar(0.)
    {}

    double double_bar;

    void inlineSetBar(double v)
    {
        double_bar = v;
    }

    void setBar(double v);
};

#endif // CPPANALYZE_TEST_H
