#include "skiplist.h"

#include <ctime>
#include <iostream>
#include <string>


int main()
{
    // rand/srand 使用运行库的随机序列状态；整个程序启动时播种一次，不能在 SkipList 构造函数中重复播种。
    // std::srand() 设置的是 C 运行库中 std::rand() 使用的随机序列状态，不是某个 SkipList 对象的成员状态。更精确地说，状态的共享范围因运行库而异；POSIX 定义了进程级共享序列，MSVC 文档则描述了当前线程和应用级状态。无论哪种情况，它都不是每个 SkipList 各自独立的状态。
    std::srand(static_cast<unsigned int>(std::time(nullptr)));


    // 键值中的 key 用 int 型，如果用其他类型，需要自定义比较函数，而且如果修改 key 的类型，同时需要修改 skipList.loadFile 函数。
    SkipList<int, std::string> skipList(6);
    skipList.insertElement(1, "Study");
    skipList.insertElement(3, "Algorithms");
    skipList.insertElement(7, "Trust");
    skipList.insertElement(8, "WeChat Official Account: Code Thinking");
    skipList.insertElement(9, "Learning");
    skipList.insertElement(19, "Stay on the Algorithm Path");
    skipList.insertElement(19, "Follow now and stay on track with algorithms!");

    std::cout << "skipList size: " << skipList.size() << std::endl;

    skipList.dumpFile();
    skipList.loadFile();

    skipList.searchElement(9);
    skipList.searchElement(18);

    skipList.displayList();

    skipList.deleteElement(3);
    skipList.deleteElement(7);

    std::cout << "skipList size: " << skipList.size() << std::endl;

    skipList.displayList();
}
