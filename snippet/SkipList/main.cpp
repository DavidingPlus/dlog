#include <iostream>

#include "skiplist.h"


// #define FILE_PATH "./store/dumpFile"


int main()
{

    // 键值中的 key 用 int 型，如果用其他类型，需要自定义比较函数，而且如果修改 key 的类型，同时需要修改 skipList.loadFile 函数。
    SkipList<int, std::string> skipList(6);
    skipList.insertElement(1, "学");
    skipList.insertElement(3, "算法");
    skipList.insertElement(7, "认准");
    skipList.insertElement(8, "微信公众号：代码随想录");
    skipList.insertElement(9, "学习");
    skipList.insertElement(19, "算法不迷路");
    skipList.insertElement(19, "赶快关注吧你会发现详见很晚！");

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
