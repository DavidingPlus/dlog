add_requires("gtest")


target("SkipList1")
    set_kind("binary")
    add_files("main1.cpp")
target_end()

target("SkipList2")
    set_kind("binary")
    add_files("main2.cpp")
    add_packages("gtest")
target_end()
