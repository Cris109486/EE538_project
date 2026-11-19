# 在自己的 VS Code 中构建、运行与调试

本页对应 **Windows 本地 C++17 初版**，使用已解压的 LLVM-MinGW、CMake 和 Ninja。
它不等同于 Ubuntu 16.04 / GCC 5.4 虚拟机环境。项目标准暂保留 proposal 的 C++17，虚拟机兼容路线仍待确定。

## 第一次操作

1. 在 VS Code 中打开整个 `E:\26fall\538\Project` 文件夹。
2. 按 `Ctrl+Shift+P`，执行 `Developer: Reload Window`，使配置重新加载。
3. 按 `Ctrl+Shift+B`，运行 **EE538: Build project**。这是构建整个项目，不是单独编译当前头文件。
4. 构建成功后，按 `Ctrl+Shift+P` → `Tasks: Run Task` → **EE538: Run example** → 选择 `fcfs`。
5. 终端会显示摘要；完整输出保存在 `out/fcfs.json`。

2026-11-18 已检查用户触发的 Windows Debug 构建记录：主程序和测试程序均已链接生成。随后按用户要求协助启动测试，CTest 的 core / integration 两组均通过，其中 integration 包含 38 次 CLI 回归运行。F5 调试仍待验证。
编辑器没有红线不代表编译或测试通过；请以终端结果为准。

## 手算预期结果（已由回归测试核对）

`examples/hand_calculated.json` 中一个请求有 4 个输入 token、2 个实际输出 token。
固定 prefill chunk 为 2；每个 prefill 批次耗时 1000 us、动态能量 0.2 J。
最终 prefill 产生第一个输出 token，随后一个 decode 批次耗时 1000 us、动态能量 0.1 J。

| 时间区间 / us | 操作 | 输入进度 | 输出进度 |
|---|---|---|---|
| 0–1000 | prefill 2 | 2/4 | 0/2 |
| 1000–2000 | prefill 2 | 4/4 | 1/2 |
| 2000–3000 | decode 1 | 4/4 | 2/2 |

预期 `makespan_us = 3000`，`first_token_us = 2000`，`dynamic_j = 0.5`。
后台功率为 1 W，在 0.003 s 内贡献 0.003 J，因此 `total_j = 0.503`。
KV 接纳时预留 6 token，完成后占用归零，deadline 恰好满足。
机器运行耗时随电脑而变，不与模拟的 3000 us 混为一谈。
对应预期文件：`examples/hand_calculated.expected.json`。

这个单请求样例只验证生命周期与配置，三种策略预期相同。它不是策略优势或节能证据。

## 测试与断点

- `Tasks: Run Task` → **EE538: Run tests**：构建后执行 CTest。C++ 用例检查堆/图/历史缓冲区/KV/合法批次和生命周期；Python 用例检查 CLI、手算结果、取消、容量和非法输入。
- 快捷键没有反应时，鼠标点击顶部 **Terminal → Run Task**，再选择相同任务。
- 打开 `src/main.cpp`，按 `F5`，选择 **EE538: Debug hand-calculated example**。配置使用 C/C++ 扩展和 LLVM-MinGW 的 `lldb-mi.exe`；启动后应停在 `main`，断点调试链路需要用户实际验证。
- 按 `F9` 添加/移除断点，`F10` 单步跳过，`F11` 进入函数，`F5` 继续，`Shift+F5` 停止。
- 在 `src/simulator.cpp` 的 `execute` 中观察 `now_`、`runtime_` 和 `kv_`，可以跟踪模拟时钟、token 进度及资源记账。
- 调试输出保存在 `out/debug-fcfs.json`。

## 在 VS Code 的 PowerShell 终端执行

不需要全局修改 PowerShell 执行策略。以下 `Bypass` 只作用于该子进程。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dev.ps1 -Action Build
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dev.ps1 -Action Run -Policy fcfs
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dev.ps1 -Action Run -Policy speed
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dev.ps1 -Action Run -Policy energy
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\dev.ps1 -Action Test
```

`Run` 和 `Test` 都先增量构建，避免执行旧的可执行文件。模拟时钟直接推进，不调用 sleep。

## 工具路径与可移植性

本机的绝对路径位于 `.vscode/` 和 `scripts/toolchain.local.json`；两者都已被 Git 忽略。
脚本仅修改其自身进程的 PATH，外部工具保存在 `D:\CodexData\toolchains`，不进入提交。
换电脑时创建自己的 `scripts/toolchain.local.json`，字段如下，值必须是存在的工具绝对路径：

```json
{
  "compiler": "D:/your-tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe",
  "cmake": "D:/your-tools/cmake/bin/cmake.exe",
  "ninja": "D:/your-tools/ninja/ninja.exe",
  "python": "D:/your-tools/python/python.exe"
}
```

`python` 可省略，此时 CMake 尝试检测 Python；若没有找到，只运行 C++ 测试，完整检查还需补上 Python 测试。
如 Python 路径因应用升级而失效，需要在本地配置中更新。
其他系统使用 CMake 和其本地 C++17 编译器独立构建；这不代表已经在该系统验证。

## 遇到错误

- 仍报找不到 `ee538/types.hpp`：确认打开的是整个 Project 文件夹，然后执行 `C/C++: Select a Configuration`，选择 **Windows LLVM-MinGW C++17**。
- CMake 找不到编译器：检查本地工具配置中的路径。
- 编译失败：复制终端中第一条 `error:` 及其上下文。
- F5 失败：发送 Debug Console 中的错误，先保留完整报错，不要反复替换调试器。

配置依据：[C/C++ IntelliSense](https://code.visualstudio.com/docs/cpp/configure-intellisense)、[C/C++ 调试配置](https://code.visualstudio.com/docs/cpp/launch-json-reference)。
