/**
 * @file ltest.h
 * @author DavidingPlus (davidingplus@qq.com)
 * @brief 测试类头文件。
 *
 * Copyright (c) 2024 电子科技大学 刘治学
 *
 */

#ifndef _LTEST_H_
#define _LTEST_H_

#include <string>


class LTest
{

public:

    static std::string foo() { return std::string("hello world"); }

    std::pair<int, int> gee(int first, int second) const;
};


#endif
