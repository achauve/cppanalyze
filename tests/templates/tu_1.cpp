#include "foo.h"

int main()
{
    TemplateFoo<int> bar;
    bar.copy(TemplateFoo<int>());

    return 1;
}
