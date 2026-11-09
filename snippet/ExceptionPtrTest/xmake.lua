target("ExceptionPtrTest")
    set_kind("binary")
    add_files("main.cpp")
    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()
