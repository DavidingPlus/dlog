#include <gtest/gtest.h>


int main(int argc, char **argv)
{
    // 重新执行子进程，避免 fork 继承已启动但无法工作的线程池。
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";

    ::testing::InitGoogleTest(&argc, argv);


    return RUN_ALL_TESTS();
}
