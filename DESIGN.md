# HiBerCommonInstaller — 通用安装器框架设计文档（定稿）

> 本文档为 HiBerCommonInstaller 项目的权威设计；实现与文档不一致时以本文档为准并修订本文档。

## 1. 定位

通用、高度可复用的安装器框架（含其依赖子模块一起复用）。第一个产品化接入方 = NeoServerUpdateModpack（NSUM），
作为其安装程序（原 modules/NeoInstaller 的完整替代）。

- 协议：LGPL-2.1
- 仓库：`git@github.com:HiBer2007/HiBerCommonInstaller.git`，单仓库多 target
- 命名空间：`hci`；target 前缀 `hci_`
- 品牌铁律：所有产品属性（含名称）可自定义，但必须显示 **`Powered by HiBer Common Installer Module`**
  ——CLI/TUI 加载时与产品名组成大号拼接字（toilet 风格）banner 输出；不可关闭。

## 2. 架构总览（六大件，依赖单向向下）

```
                 ┌─ 入口 entry（位于核心）──┐
                 │ 参数解析 · 终端判定 · 模式分发 │
                 └──┬──────────┬──────────┬───────┘
       库模式(静态/动态)│  GUI 壳  │  TUI 壳  │  CLI 壳
                 ▼   │    ▼     │    ▼    │    ▼
      ┌───────────────────────────────────────────┐
      │           拓展主机（hci_ext）               │
      │ interface 定义 · 加载/注册 · 扩展间通讯     │
      └──────────────────┬────────────────────────┘
      ┌──────────────────▼────────────────────────┐
      │      流程控制器（hci_flow）                 │
      │ 流程脚本(JSON+Lua) · 步骤机 · 变量域 · 卸载  │
      └──────────────────┬────────────────────────┘
      ┌──────────────────▼────────────────────────┐
      │       通用安装核心（hci_core，零 Qt）       │
      │ 总线(事件+服务注册) · 执行函数库 · payload源 │
      │ 安装上下文 · Lua 引擎 · 日志桥 · 可移植层    │
      └────────────────────────────────────────────┘
```

- 核心零 Qt：C++17 + nlohmann-json + Lua 5.4 + libzippp（vcpkg）
- GUI 壳依赖 Qt6 + HiBerGUILibCPP（submodule，静态链接）
- 日志桥接 CommonLoggerCPP 风格：`IConfigurableLogSink` / 导出符号注入（沿 NSUM 插件日志铁律）

## 3. 仓库布局

```
HiBerCommonInstaller/
├── CMakeLists.txt          # 根：入口判定选项（HCI_BUILD_CLI/GUI/TUI/EXAMPLES）+ 自包含版本资源
├── vcpkg.json              # nlohmann-json, lua, libzippp
├── LICENSE / README.md / DESIGN.md / .gitignore
├── core/                   # hci_core（STATIC，HCI_CORE_SHARED=ON → SHARED）
│   ├── include/hci/        # bus.h context.h vars.h payload.h exec.h script.h log.h port.h entry.h
│   └── src/
├── flow/                   # hci_flow（JSON 解析 + 步骤机 + Lua 绑定 + 卸载流程）
├── ext/                    # hci_ext（拓展主机：接口 + 三种加载 + 服务注册表 + 参数处理器）
├── cli/                    # hci_cli（CLI 壳 EXE，控制台子系统）
├── gui/                    # hci_gui_shell（静态 Qt，预设页 + PageFactory）
├── tui/                    # hci_tui_shell（C++ ANSI + wcwidth + 组件库）
├── extensions/             # 随仓库分发的内置拓展（示例：git-download、nsum-args）
├── HiBerGUILibCPP/         # submodule（GUI 壳依赖）
└── examples/               # demo 产品（product.json + flow + 独立构建自证）
```

## 4. 通用安装核心 hci_core

| 组件 | 职责 |
|---|---|
| `hci::Bus` | 事件总线（topic publish/subscribe，同步调用 + 队列异步投递）+ **服务注册表**（按接口类型注册/查询服务，供扩展间调用） |
| `hci::InstallContext` | 安装上下文：变量域（product 常量 + 流程变量 + 运行时变量）、状态、取消令牌、失败策略 |
| `hci::Vars` | `{name}` 模板插值；`eval` 经 Lua 表达式 |
| `hci::IDeploySource` | payload 三源：`QrcSource`（内嵌）/ `DirSource`（外部目录）/ `ZipSource`（zip 文件）；跳过规则（通配） |
| `hci::Exec` | 执行函数库：file copy/tree/clean、extract（zip 经 libzippp / tar / 7za 后端）、download（HTTP，含 GitHub Releases 模式通用化）、shortcut（IShellLink 参数化）、registry/INI、run（进程 + 等待/退出码/输出捕获）、template（文本模板渲染，install.conf 族） |
| `hci::IScriptEngine` | 脚本引擎接口；默认实现 `LuaEngine`（Lua 5.4，静态链接）；流程步骤 `{type:"script", engine:"lua", code:"…"}` |
| `hci::Log` | 日志桥：输出到 `ILogSink`（默认控制台/文件/无）；GUI 挂 Qt 侧 sink；`Log::Info/Warn/Error` |
| `hci::port` | 可移植层：路径、环境、终端判定——Windows 原生 + POSIX 分支（v1 双平台，Windows 优先） |
| `hci::entry` | 四大入口判定 + banner 输出（ASCII 字体 slant/standard 内置） |

## 5. 入口模型（四式判定）

统一**控制台子系统** + `holdOrReleaseConsole` 铁律（沿主程序 NSUM 同款）：

| 场景 | 判定 | 行为 |
|---|---|---|
| ① 库模式 | 编译为库 | 无 main；API `hci::install(ctx, flow)` / `hci::loadExtensions(...)` |
| ② 终端启动 | 有交互终端（`GetConsoleProcessList`>1 或 isatty） | **不释放终端**：GUI 日志双写 GUI+终端；TUI 加载 TUI；CLI 进 CLI；无参数进默认模式（product.json `defaultMode`） |
| ③ 双击 | 无终端且 argc==1 | 直接默认模式；GUI 模式 `FreeConsole()` |
| ④ 无终端带参数（CreateProcessA） | 无终端且 argc>1 | 解析参数执行无终端逻辑（CLI 流式安装），stdout 不可用时日志落文件 |

- 显式模式参数：`--gui` / `--tui` / `--cli`（核心参数）
- 参数表 = 核心参数（mode/path/silent/json/help/version…）+ **拓展注册的参数处理器**（capability `cliArgs`，
  如 NSUM 的 `--with-editor`：命中后置组件变量，核心不感知）
- banner：`<产品名>` 大号 ASCII（slant/standard 内置，过长自动降档）→ 下一行 `Powered by HiBer Common Installer Module`

## 6. 流程控制器 hci_flow

- 流程脚本 = JSON 声明式步骤列表 + 可选 Lua 脚本块：

```json
{ "id": "install", "vars": { "installDir": "" },
  "steps": [
    { "id": "welcome", "ui": "welcome" },
    { "id": "license", "ui": "license", "source": "qrc:/license/LICENSE" },
    { "id": "extract", "type": "extract", "source": "qrc:/deploy", "target": "{installDir}", "onFail": "abort" },
    { "id": "git",     "type": "download", "asset": "git-for-windows/git", "variant": "MinGit",
      "license": "gpl2", "target": "{installDir}/tools/git" },
    { "id": "script1", "type": "script", "engine": "lua", "code": "if vars.installEditor then ... end" },
    { "id": "shortcut","type": "shortcut", "kind": "desktop", "name": "{productName}.lnk",
      "target": "{installDir}/{mainExe}", "when": "components.editor == true" },
    { "id": "writeconf","type": "template", "file": "install.conf", "header": "# ...",
      "template": { "install_path": "{installDir}", "git_path": "{installDir}/tools/git/bin/git.exe" } },
    { "id": "finish",  "ui": "finish", "launch": "{installDir}/{mainExe}" }
  ] }
```

- 内置步骤类型：`welcome/license/path/component/option/confirm/input（ui 交互，壳渲染）`、
  `extract/copy/clean/download/run/shortcut/template/registry（执行器）`、`script（Lua）`
- `${var}` 插值；`when` 条件（Lua 表达式或简单 eq/exists）；跳转 `next`/`goto`
- 失败策略：`abort`（默认）/`ignore`/`continue`；v1 不做整体事务回滚（卸载流程独立）
- **卸载流程**：同引擎第二条流程（如 `uninstall.json`）+ 注册表卸载入口（`UninstallString` 指向 `hci_cli --flow uninstall --silent`），v1 内置

## 7. 拓展主机 hci_ext

- 统一接口 + 能力声明：

```cpp
struct HciCapabilities {   // 声明式能力位
    bool providesSteps, providesPages, providesTuiPanels,
         providesCliCommands, providesCliArgs, providesServices;
};
class IHciExtension {
public:
    virtual const char* id() const = 0;
    virtual const char* version() const = 0;
    virtual HciCapabilities capabilities() const = 0;
    virtual bool init(hci::HostApi& api) = 0;   // 注册步骤/页面/面板/命令/参数/服务
    virtual void shutdown() = 0;
};
```

- **三种加载**：
  1. 静态链接嵌入：链接静态库 + `HCI_REGISTER_EXTENSION(cls)` 宏 + 链接清单（`--extensions-static` 列表）
  2. DLL 放置加载：`extensions/` 目录扫描，`extern "C" __declspec(dllexport) hci::IHciExtension* HciGetExtension()`
     （**必须 dllexport**，沿 NSUM 插件教训）+ 同名 meta.json
  3. 专有插件包 `.hci` = ZIP 容器（libzippp）：`meta.json`（id/version/entry/sha256/deps/contributions）+ dll + assets；
     加载链：校验 → 解压到 `%LOCALAPPDATA%/<product>/ext-cache/<id>/<version>/`（版本变更换目录）→ `LoadLibrary` → `GetProcAddress(entry)`
- **通讯**：事件总线（topic publish/subscribe）+ 服务注册表（`api.registerService<T>()` / `api.service<T>()`）
- 日志注入沿 NSUM 铁律：`HCI_DECLARE_EXTENSION_LOG_SINK("id")` 宏（导出符号），宿主注入 `LogSink`（带 `[id]` 前缀）

## 8. 三壳

### GUI 壳（静态 Qt + HiBerGUILibCPP）
- 预设 9 页：欢迎 / 许可 / 安装路径（默认路径+非空清空警告）/ 组件（components[] 核心功能）/ 选项 / 下载（多任务进度）/ 进度（步骤流+日志）/ 完成（立即运行）/ 卸载
- `PageFactory`：字符串 id → 页面工厂，流程步骤 `ui` 字段映射；拓展经 capability `pages` 注册新页面
- 组件选择 = 核心功能：product.json `components[]`（id/label/required/defaultChecked/exe/shortcutName），
  三壳统一交互，结果入 `vars.components.<id>`；`--with-editor` 类参数由拓展参数处理器负责
- 复用 HiBerGUILibrary：ProgressCard/AnimatedProgress/ToastNotification 等

### TUI 壳（先 C++ ANSI，Node 后置评估）
- 渲染层：VT 序列 + `SetConsoleOutputCP(CP_UTF8)` + `ENABLE_VIRTUAL_TERMINAL_PROCESSING` + `_O_BINARY`（铁律）
- 宽字符：内置 wcwidth 表（East Asian Width），布局按显示宽度计算
- 组件库：Frame/Page、Menu、RadioList、CheckList、TextInput、Password、Confirm、ProgressBar、Spinner、LogView、StatusBar、Table
- 拓展：capability `tuiPanels` 注册面板工厂；与 GUI 共享流程（同 `ui` 字段，各自渲染）

### CLI 壳
- 子命令 + 选项；`--json` 输出 `=====JSON-BEGIN=====` / `=====JSON-END=====` 标记块，人类日志走 stderr（沿 NSUM 协议）
- 退出码：0 成功 / 1 失败 / 2 参数错误
- 核心参数：`--gui/--tui/--cli`、`--silent`、`--path <dir>`、`--flow <file>`（指定流程脚本）、`--product <file>`、`--json`、`--help/-h`、`--version/-v`
- 拓展参数：capability `cliArgs` 处理器（如 `--with-editor`）

## 9. 产品配置 product.json

```json
{ "productName": "…", "company": "…", "orgName": "…", "version": "1.0.0",
  "icon": "…", "defaultMode": "gui",
  "defaultInstallPath": "", "banner": { "font": "slant" },
  "components": [ { "id": "editor", "label": "…", "required": false,
                    "defaultChecked": false, "exe": "Editor.exe", "shortcutName": "…" } ],
  "shortcuts": [ { "kind": "desktop|startmenu", "name": "…", "targetExe": "…" } ],
  "installConf": { "fileName": "install.conf", "header": "# …",
                   "template": { "install_path": "{installDir}" } },
  "flows": { "install": "qrc:/flows/install.json", "uninstall": "qrc:/flows/uninstall.json" },
  "payload": { "source": "qrc:/deploy|dir|zip", "skip": ["*.dmp", "config/custom/*"] },
  "uninstall": { "registryKey": "Software/…/…", "displayName": "…" } }
```

## 10. NSUM 集成（接入方）

- 主仓库 `git submodule` 引用（路径 modules/NeoInstaller 指向本仓库）
- 根 CMake `INSTALLER_ONLY_BUILD` 分支 add_subdirectory；`installer-static` 预设 + `build_installer.ps1`
  适配：`-DPRODUCT_JSON=<主仓库路径>` + `-DDEPLOY_SOURCE=<build/deploy>`；版本资源由仓库自包含（`ci_add_version_info` + 自带图标）
- **NSUM 配置放主仓库**：NSUM 的 product.json / 部署规则 / install.conf 键 / Git 下载参数由主仓库提供
- 行为映射：8 页 → 页面预设；GitChecker/GitDownloader → `download` 步骤执行器（Git 资源规则入 product）；
  7za 解压 → `extract` 多后端；install.conf → `template` 步骤；快捷方式 → `shortcut` 步骤；
  `--silent/--with-editor/--use-system-git/--use-bundled-git` → CLI 流程（`--with-editor` 经 NSUM 拓展参数处理器）
- 验证：独立构建 demo 自证 + 三壳冒烟 + `clone --recursive` 闭环 + NSUM 安装行为不变（功能实测归用户）

## 11. 构建与依赖

- vcpkg.json：`nlohmann-json`、`lua`、`libzippp`（→ libzip/zlib/bzip2）
- Qt：动态（开发迭代）H:/Qt/6.11.1/msvc2022_64；静态（发布）H:/Qt-static/6.11.1/msvc2022_64（qtbase 源码构建，`-static -release -schannel -no-opengl`）
- 根选项：`HCI_BUILD_CLI=ON`、`HCI_BUILD_GUI=ON`、`HCI_BUILD_TUI=ON`、`HCI_BUILD_EXAMPLES=ON`、`HCI_CORE_SHARED=OFF`
- 双平台：核心/CLI 可移植 API（`hci::port`），Windows 优先验证，POSIX 分支 v1 提供

## 12. 里程碑

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | 决策定稿 + 仓库骨架 + 静态 Qt 构建 | 进行中 |
| M1 | hci_core + hci_flow + hci_cli + Lua 绑定（零 Qt 全链路） | |
| M2 | hci_ext 拓展主机（三加载 + 服务注册表 + 参数处理器） | |
| M3 | demo 产品配置 + 独立构建自证 + 三壳 CLI/TUI v1 成形 | |
| M4 | GUI 壳（预设页 + HiBerGUILib + 静态 Qt 发布链路） | |
| M5 | NSUM 集成（submodule + product.json + 行为映射 + 主仓库提交） | |
| M6 | README/usage/模块索引 + clone --recursive 闭环 + 冒烟收尾 | |

## 13. 风险与待办

- 静态 Qt 源码构建：本机 perl 缺失（已用 `-schannel` 规避 OpenSSL/perl 依赖）；构建约 30–60 分钟后台进行
- vcpkg `qt6` port 已从默认 registry 移除 → 静态 Qt 一律走源码构建（指南见主仓库 docs/workspace/STATIC_QT_BUILD_GUIDE.md）
- TUI 宽字符：wcwidth 表内嵌（约 300 行），Windows Terminal / conhost UTF-8 双验证
- 数字签名 / 插件包升级策略 / 服务定义 ABI：v2