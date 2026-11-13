add_requires("benchmark")

target("dlog-bench")
    set_default(false)
    set_kind("binary")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/bench")
    add_files("*.cpp")
    add_deps("dlog")
    add_packages("benchmark")
    apply_current_platform_target_config()

    before_run(function (target)
        -- 每次运行使用独立的路径交接文件，避免并发运行时画错结果。
        local marker = os.tmpfile()
        target:data_set("bench.result_marker", marker)
        os.setenv("DLOG_BENCH_RESULT_PATH_FILE", marker)
    end)

    after_run(function (target)
        local marker = target:data("bench.result_marker")
        if marker and os.isfile(marker) then
            local result = io.readfile(marker)
            os.rm(marker)
            local find_tool = import("lib.detect.find_tool")
            local python = find_tool("python3") or find_tool("python")
            if not python then
                os.raise("找不到 Python；结果已保存，请安装 Python 3.8 或以上版本后运行绘图脚本：%s", result)
            end
            os.execv(python.program, {path.join(os.projectdir(), "bench", "plot", "plot_results.py"), result})
        end
    end)
target_end()
