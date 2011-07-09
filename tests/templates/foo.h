#ifndef FOO_H
#define FOO_H

#include <utility>

template<class T>
struct TemplateFoo
{
    T t_bar;
    std::pair<T, T> t_pair;

    void copy(const TemplateFoo<T>& other)
    {
        t_bar = other.t_bar;
        t_pair.first = other.t_pair.first;

        TemplateFoo<int> int_foo;
        int_foo.t_bar = 2;

        TemplateFoo<T> t_foo;
        t_foo.t_pair.first = 0;
        t_foo.t_bar = 0;
    }
};


template<class T>
void processFoo(TemplateFoo<T>& t_foo)
{
    t_foo.t_bar = 0;
    t_foo.t_pair.first = 0;
}

#endif // FOO_H
