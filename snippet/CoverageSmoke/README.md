# CoverageSmoke

这是一个不依赖 GoogleTest 和 dlog 的最小覆盖率验证程序，使用 clang-cl 的 LLVM source-based coverage，并由 grcov 生成 HTML 报告。

请确保 `grcov` 已经安装并在 PATH 中，然后在 **x64 Native Tools Command Prompt for VS 2019** 中执行。这个独立配置不依赖根项目的 `fmt` 包。

```bat
xmake -F snippet\CoverageSmoke\xmake.lua f -c -m coverage -y
xmake r -F snippet\CoverageSmoke\xmake.lua coverage_smoke
```

程序会按以下顺序寻找 LLVM：`LLVM_COVERAGE_ROOT`、`LLVMInstallDir`、`LLVM_HOME`、Visual Studio 的 `VCINSTALLDIR`/`VSINSTALLDIR`，以及 PATH 中的 `clang-cl.exe`。因此推荐从 **x64 Native Tools Command Prompt for VS 2019** 执行，不需要写死安装目录。

如果使用 Scoop 安装 grcov，可以执行：

```bat
scoop install grcov
```

如果使用独立版 LLVM 且没有加入 PATH，可以只设置一个环境变量：

```bat
set "LLVM_COVERAGE_ROOT=<LLVM 根目录>"
```

如果系统中已经设置了 `LLVM_HOME`，也可以使用：

```bat
set "LLVM_COVERAGE_ROOT=%LLVM_HOME%"
```

预期输出：

```text
coverage smoke test passed
```

报告默认输出到目标目录：

```text
build\windows\x64\coverage\coverage_smoke.profraw
build\windows\x64\coverage\coverage-html\index.html
```

检查工具：

```bat
clang-cl --version
llvm-cov --version
llvm-profdata --version
grcov --version
```

如果 `xmake r` 输出 `coverage report:`，并且生成 `index.html`，就说明 clang-cl 和 grcov 的覆盖率流程已经生效。

