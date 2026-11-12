if not get_config("with_gtest") then
    return
end

includes("config.lua")


add_requires("gtest")

local gtest_groups = {
    "core",
    "utils",
    "testrun"
}

if is_plat("windows") then
    target("logmanager_dll_probe")
        set_default(false)
        set_kind("shared")
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
        add_deps("dlog")
        add_defines("DLOG_DLL_EXIT_PROBE")
        add_files("core/logmanager_test_helper.cpp")
        apply_current_platform_target_config()
    target_end()
end

-- 请用 xmake test 执行测试。
target("tests")
    set_default(false)

    set_kind("binary")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")
    add_deps("dlog")
    if is_plat("windows") then
        add_deps("logmanager_dll_probe")
    end
    add_packages("gtest")

    before_build(function ()
        os.tryrm("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
        os.mkdir("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
    end)

    on_install(function () end)
    apply_current_platform_target_config()

    add_files("main.cpp")
    apply_gtest_summary_config("tests", gtest_groups)

    for _, group in ipairs(gtest_groups) do
        add_tests(group, {
            realtime_output = true,
            files = {group .. "/*.cpp"}
        })
    end
target_end()
