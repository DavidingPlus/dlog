add_requires("gtest")


target("SkipListTest")
    set_kind("binary")
    add_files("main1.cpp")
target_end()

target("SkipListUnitTest")
    set_kind("binary")
    add_files("main2.cpp")
    add_packages("gtest")
target_end()

target("SkipListStressTest")
    set_kind("binary")
    add_files("main3.cpp")
target_end()
