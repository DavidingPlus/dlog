/**
 * @file ltest.cpp
 * @author DavidingPlus (davidingplus@qq.com)
 * @brief 测试类源文件。
 *
 * Copyright (c) 2024 电子科技大学 刘治学
 *
 */

#include "ltest.h"


std::pair<int, int> LTest::gee(int first, int second) const
{
    std::pair<int, int> res;

    res.first = first;
    res.second = second;


    return res;
}
