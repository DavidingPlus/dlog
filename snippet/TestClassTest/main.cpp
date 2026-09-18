#include <iostream>

#include "ltest.h"


int main()
{
    std::cout << LTest::foo() << std::endl;

    auto p = LTest().gee(3, 4);

    std::cout << p.first << std::endl;
    std::cout << p.second << std::endl;

    return 0;
}
