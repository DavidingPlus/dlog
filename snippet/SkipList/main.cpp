#include "skiplist.h"

#include <iostream>
#include <string>


// #define FILE_PATH "./store/dumpFile"


int main()
{
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

    // skipList.dumpFile();
    // skipList.loadFile();

    skipList.searchElement(9);
    skipList.searchElement(18);

    skipList.displayList();

    skipList.deleteElement(3);
    skipList.deleteElement(7);

    std::cout << "skipList size: " << skipList.size() << std::endl;

    skipList.displayList();
}
