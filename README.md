# HiBerCommonInstaller

通用安装器框架（Generic installer framework）：**核心 + 流程控制器 + 拓展主机 + GUI/TUI/CLI 三壳**，高度可复用（含其依赖子模块）。第一个产品化接入方：NeoServerUpdateModpack（NSUM）安装程序。

- **协议**：LGPL-2.1
- **命名空间**：`hci`（target 前缀 `hci_`）
- **核心零 Qt**：C++17 + nlohmann-json + Lua 5.4 + libzippp + cpr（vcpkg）
- **品牌铁律**：所有产品属性可自定义，但必须显示 `Powered by HiBer Common Installer Module`（CLI/TUI 大号 banner）

## 六大件

| 部件 | 位置 | 说明 |
|---|---|---|
| 通用安装核心 | `core/` (`hci_core`) | 零 Qt：总线（事件 + 服务注册表）、安装上下文/变量、执行函数库（文件/解压/下载/快捷方式/注册表/模板）、payload 三源（dir/zip/qrc）、Lua 5.4 内嵌脚本（`IScriptEngine`）、日志桥（`ILogSink`）、可移植层、入口判定 + banner |
| 流程控制器 | `flow/` (`hci_flow`) | JSON 声明式步骤机：`ui 交互` 与 `type 执行` 步骤统一驱动，`${var}` 模板、Lua 条件（when）、失败策略（abort/ignore）、卸载流程 |
| 拓展主机 | `ext/` (`hci_ext`) | 统一 `IHciExtension` + 能力声明；三加载：静态注册宏 / DLL 放置（`HciGetExtension` 导出）/ `.hci` ZIP 包（meta.json + dll + sha256 + 缓存解压）；扩展间通讯 = 事件总线 + 服务注册表 |
| CLI 壳 | `cli/` (`hci_cli`) | 控制台子系统；核心参数（mode/path/silent/json…）；`=====JSON-BEGIN=====` 协议；退出码 0/1/2 |
| GUI 壳 | `gui/` | M4：静态 Qt + HiBerGUILibCPP，预设 9 页 + PageFactory |
| TUI 壳 | `tui/` | M5：C++ ANSI + wcwidth 宽字符表 + 组件库 + 拓展面板 |

## 快速开始（demo）

```powershell
# 构建（需 vcvars64 环境 + vcpkg 工具链）
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=H:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt6 目录> -G Ninja
cmake --build build

# 运行 demo 安装流程（静默）
build/examples/hci_demo_run   # 或直接：
build/cli/hci_cli --product build/examples/demo/product.json --flow install --silent --path <目标目录>
```

## 入口模型（四式）

① 库模式（`hci::entry::runInstall`）；② 终端启动 → 保终端 + UTF-8；③ 双击（无参数）→ 默认模式（product `defaultMode`），GUI 释放终端；④ CreateProcessA 无终端带参数 → 无终端逻辑 + 日志落文件。

## 里程碑

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | 决策定稿 + 仓库骨架 + 静态 Qt | ✅（Qt6 6.11.1 static 已构建至 H:/Qt-static） |
| M1 | core + flow + cli + Lua（零 Qt 全链路） | ✅ |
| M2 | 拓展主机三加载 + 参数处理器 | ✅ |
| M3 | demo 独立构建自证 | ✅ |
| M4 | GUI 壳（静态 Qt） | ✅ |
| M5 | TUI 壳 | ✅ |
| M6 | NSUM 集成 | ✅ |
| M7 | 文档收尾 | ✅ |

## 文档库

- [docs/README.md](docs/README.md) — 文档库总索引
- [docs/product-config.md](docs/product-config.md) — product.json 完整 schema
- [docs/flow-script.md](docs/flow-script.md) — 流程脚本语言参考
- [docs/core-api.md](docs/core-api.md) — hci_core 公共 API 逐字参考
- [docs/extension-dev.md](docs/extension-dev.md) — 拓展开发指南
- [docs/shell-cli.md](docs/shell-cli.md) / [docs/shell-gui.md](docs/shell-gui.md) / [docs/shell-tui.md](docs/shell-tui.md) — 三壳详细文档
- [docs/build-guide.md](docs/build-guide.md) — 构建与单文件分发
- [docs/nsum-integration.md](docs/nsum-integration.md) — NSUM 接入指南
- [docs/pitfalls.md](docs/pitfalls.md) — 踩坑清单

详见 [DESIGN.md](DESIGN.md)（权威设计文档）与 [usage.md](usage.md)（精简手册）。