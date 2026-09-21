-- 这个文件既可以通过 `xmake -F` 单独执行，也可以被根项目的
-- snippet/xmake.lua 引入。只有单独执行时才需要声明一个独立项目。
local standalone = path.absolute(os.projectdir()) == path.absolute(os.scriptdir())
if standalone then
    set_xmakever("3.0.9")
    set_project("CoverageSmoke")
    set_languages("cxx17")
    add_rules("mode.debug")
end

target("CoverageSmoke")
    set_kind("binary")
    set_default(false)
    add_files("main.cpp")

    if is_plat("windows") then
        set_toolchains("clang-cl")
    end

    local coverage_mode = is_mode("coverage")

    local function append_unique(values, value)
        if value and value ~= "" then
            value = path.absolute(value)
            for _, existing in ipairs(values) do
                if existing == value then
                    return
                end
            end
            table.insert(values, value)
        end
    end

    local function append_llvm_candidates(values, value)
        append_unique(values, value)
        if value and value ~= "" then
            append_unique(values, path.join(value, "x64"))
        end
    end

    if coverage_mode then
        -- clang-cl 的源代码覆盖率需要 LLVM profile runtime；
        -- XMake 的 mode.coverage 会追加 gcov 风格的 --coverage，不能直接用于这里。
        -- VS 2019 自带的 LLVM 12 在覆盖率 runtime 与 /Zi 同时使用时，
        -- 可能在进程退出写 profraw 时崩溃；coverage mapping 已经包含源码行信息，
        -- 因此这个验证目标不需要额外的 PDB 调试信息。
        set_symbols("none")
        --
        -- LLVM 的探测放在 on_load 中：根项目解析 xmake.lua 时不会因为
        -- coverage_smoke 没有被选中而要求当前终端必须已经加载 VS 环境。
        local coverage_tools
        set_policy("check.auto_ignore_flags", false)

        on_load(function (target)
            local llvm_candidates = {}
            append_llvm_candidates(llvm_candidates, os.getenv("LLVM_COVERAGE_ROOT"))
            append_llvm_candidates(llvm_candidates, os.getenv("LLVMInstallDir"))
            append_llvm_candidates(llvm_candidates, os.getenv("LLVM_HOME"))

            local vc_install_dir = os.getenv("VCINSTALLDIR")
            if vc_install_dir then
                append_llvm_candidates(llvm_candidates, path.join(vc_install_dir, "Tools", "Llvm"))
            end

            local vc_tools_install_dir = os.getenv("VCToolsInstallDir")
            if vc_tools_install_dir then
                local vc_tools_dir = path.directory(path.directory(vc_tools_install_dir))
                append_llvm_candidates(llvm_candidates, path.join(vc_tools_dir, "Llvm"))
            end

            local vs_install_dir = os.getenv("VSINSTALLDIR")
            if vs_install_dir then
                append_llvm_candidates(llvm_candidates, path.join(vs_install_dir, "VC", "Tools", "Llvm"))
            end

            -- 普通 PowerShell 通常没有 VS Native Tools 注入的这些环境变量。
            -- VS Installer 自带的 vswhere 可以找到 VS 根目录，因此不需要把
            -- 某台机器上的 D:\\... 安装路径写进项目配置。
            local vswhere = path.join(os.getenv("ProgramFiles(x86)") or "",
                "Microsoft Visual Studio", "Installer", "vswhere.exe")
            if os.isfile(vswhere) then
                local output = os.iorunv(vswhere, {
                    "-latest",
                    "-products", "*",
                    "-property", "installationPath"
                })
                for installation_path in string.gmatch(output or "", "[^\r\n]+") do
                    append_llvm_candidates(llvm_candidates,
                        path.join(installation_path, "VC", "Tools", "Llvm"))
                end
            end

            -- 独立版 LLVM 如果已经加入 PATH，也可以从 clang-cl.exe 反推出 LLVM 根目录。
            for bin_dir in string.gmatch(os.getenv("PATH") or "", "[^;]+") do
                bin_dir = bin_dir:gsub('^"(.*)"$', "%1")
                if os.isfile(path.join(bin_dir, "clang-cl.exe")) then
                    append_unique(llvm_candidates, path.directory(bin_dir))
                end
            end

            local llvm_root
            local profile_runtimes
            for _, candidate in ipairs(llvm_candidates) do
                local runtimes = os.files(path.join(candidate, "lib", "clang", "*", "lib", "windows", "clang_rt.profile-x86_64.lib"))
                if #runtimes > 0 and os.isfile(path.join(candidate, "bin", "llvm-profdata.exe")) and os.isfile(path.join(candidate, "bin", "llvm-cov.exe")) then
                    llvm_root = candidate
                    profile_runtimes = runtimes
                    break
                end
            end

            if not llvm_root then
                raise("LLVM coverage tools were not found; run a VS Native Tools prompt or set LLVM_COVERAGE_ROOT")
            end

            local profile_runtime_dir = path.directory(profile_runtimes[1])
            local llvm_bin = path.join(llvm_root, "bin")
            local llvm_profdata = path.join(llvm_bin, "llvm-profdata.exe")
            local llvm_cov = path.join(llvm_bin, "llvm-cov.exe")

            if not os.isfile(llvm_profdata) or not os.isfile(llvm_cov) then
                raise("llvm-profdata.exe or llvm-cov.exe was not found below: " .. llvm_bin)
            end

            target:add("cxflags", "-fprofile-instr-generate")
            target:add("cxflags", "-fcoverage-mapping")
            target:add("linkdirs", profile_runtime_dir)
            target:add("links", "clang_rt.profile-x86_64")

            coverage_tools = {llvm_bin = llvm_bin}
        end)

        before_run(function (target)
            local targetdir = path.absolute(target:targetdir())
            local profile_dir = path.join(targetdir, "coverage-data")
            os.mkdir(targetdir)
            os.rm(profile_dir)
            os.mkdir(profile_dir)
            os.rm(path.join(targetdir, "coverage-html"))
            os.setenv("LLVM_PROFILE_FILE", path.join(profile_dir, "coverage_smoke.profraw"))
        end)

        after_run(function (target)
            local targetdir = path.absolute(target:targetdir())
            local profile_dir = path.join(targetdir, "coverage-data")
            local profile = path.join(profile_dir, "coverage_smoke.profraw")
            local html_dir = path.join(targetdir, "coverage-html")

            if not os.isfile(profile) then
                raise("LLVM profile was not generated: " .. profile)
            end

            local targetfile = path.absolute(target:targetfile())
            local binary_dir = path.directory(targetfile)
            local source_dir = path.absolute(os.scriptdir())
            local grcov = os.getenv("GRCOV") or "grcov"
            local llvm_bin = coverage_tools.llvm_bin

            os.execv(grcov, {
                profile_dir,
                "--source-dir", source_dir,
                "--binary-path", binary_dir,
                "--llvm-path", llvm_bin,
                "--llvm",
                "--branch",
                "--ignore-not-existing",
                "--output-types", "html",
                "--output-path", html_dir
            })

            print("coverage report: %s", html_dir)
        end)
    end
target_end()
