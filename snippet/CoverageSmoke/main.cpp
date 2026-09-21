#include <iostream>


// 这个函数有三个互斥出口，测试会分别走到负数、零和正数路径。
int classify(int value)
{
    if (value < 0)
    {
        return -1;
    }

    if (value == 0)
    {
        return 0;
    }

    return 1;
}


int main()
{
    // 一次运行覆盖 classify 的三个分支，便于验证覆盖率工具确实收到了运行数据。
    const bool passed = classify(-1) == -1 && classify(0) == 0 && classify(1) == 1;

    std::cout << (passed ? "coverage smoke test passed\n" : "coverage smoke test failed\n");
    return passed ? 0 : 1;
}
