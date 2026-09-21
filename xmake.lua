includes("config.lua")


local version = "0.0.0"
local export_headers_module = "export-headers"
local export_headers_import_options = {rootdir = os.scriptdir(), anonymous = true}


set_version(version)

set_xmakever("3.0.9")
set_project("dlog")
set_description("A Simple Log System.")
set_languages("cxx17")

add_rules("mode.debug", "mode.release")

set_configdir("$(builddir)/config/")
add_configfiles("src/config.h.in")

add_includedirs("$(builddir)/config/")

add_requires("fmt")


option("with_gtest")
    set_default(false)
    set_showmenu(true)
    set_description("Enable GoogleTest-based unit tests.")
option_end()

option("install_in_place")
    set_default(true)
    set_showmenu(true)
    set_description("Install to $(builddir)/$(plat)/$(arch)/$(mode)/install by default.")
option_end()

option("build_shared")
    set_default(default_build_shared_for_current_platform())
    set_showmenu(true)
    set_description("Build the dlog library as a shared library.")
option_end()


define_current_platform_options()

local build_shared = get_config("build_shared")

if build_shared == nil then
    build_shared = default_build_shared_for_current_platform()
end

local install_in_place = get_config("install_in_place")

if install_in_place == nil then
    install_in_place = true
end

if install_in_place then
    -- 请用 xmake install 安装。
    set_installdir("$(builddir)/$(plat)/$(arch)/$(mode)/install")
end

target("dlog")
    set_kind(build_shared and "shared" or "static")

    apply_current_platform_target_config()

    add_packages("fmt", {public = true})

    if build_shared and is_current_win32() then
        -- D_BUILD_SHARED：使用动态库还是静态库。
        add_defines("D_BUILD_SHARED", {public = true})

        -- D_DLL_EXPORT：是否正在编译 DLL 本身。如果是，使用 __declspec(dllexport) 导出符号，否则是用户在使用 DLL 库，使用 __declspec(dllimport) 导入符号。
        add_defines("D_DLL_EXPORT")
    end

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/lib/")

    -- 会将 src 根目录和所有子目录一起匹配。
    add_files("src/**.cpp")

    add_includedirs("src", os.dirs("src/**"), {public = true})

    before_build(function (target)
        io.writefile(path.join(path.directory(target:targetdir()), ".version"), version)
    end)

    before_install(function (target)
        os.tryrm(target:installdir())
        os.mkdir(target:installdir())

        os.cp("$(builddir)/$(plat)/$(arch)/$(mode)/.version", target:installdir())

        local export_headers = import(export_headers_module, export_headers_import_options)
        export_headers.install_export_public_headers(target, target:installdir())
    end)

    before_package(function (target)
        os.tryrm(path.join(target:packagedir(), "$(plat)/$(arch)/$(mode)/"))
        os.mkdir(target:packagedir())

        os.cp("$(builddir)/$(plat)/$(arch)/$(mode)/.version", target:packagedir())
        os.cp("$(builddir)/$(plat)/$(arch)/$(mode)/.version", path.join(target:packagedir(), "$(plat)/$(arch)/$(mode)/.version"))

        local export_headers = import(export_headers_module, export_headers_import_options)
        export_headers.package_export_public_headers(target)
    end)
target_end()


includes("snippet")

if get_config("with_gtest") then
    includes("test")
end
