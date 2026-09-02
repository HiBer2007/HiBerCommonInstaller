# HiBerCommonInstaller 文档库

通用安装器框架（HiBer Common Installer Module）的完整参考文档。代码与文档不一致时**以代码为准**；本文档依据实际头文件/实现逐字编写（铁律见 AGENTS.md 文档依据条款）。

## 文档导航

| 文档 | 内容 | 适用读者 |
|------|------|----------|
| [product-config.md](product-config.md) | `product.json` 完整 schema：全部字段（含提权/拓展配置段）、JSON 结构、示例、`%VAR%` 占位 | 产品接入方（定义你的安装器） |
| [flow-script.md](flow-script.md) | 流程脚本语言：11 种交互页（含语言/Git/提权）、全部执行步骤（含下载后端链）、Lua 条件/脚本、回退、卸载/修复/升级 | 流程编写者 |
| [core-api.md](core-api.md) | `hci_core` 公共 API 逐字参考：总线/上下文/变量/执行库/下载后端/lang/提权/payload/脚本/日志/端口/入口/拓展注册表 | 库使用者、嵌入开发者 |
| [extension-dev.md](extension-dev.md) | 拓展开发：接口契约、宿主 API（含 extensionConfig）、注册表、下载后端注册、三种加载方式、`.hci` 包格式、官方拓展（hci_git/hci_winget/hci_apt） | 拓展作者 |
| [shell-cli.md](shell-cli.md) | CLI 壳：参数表（含 --lang）、JSON 协议、退出码、help 插件段、交互/静默语义 | CLI 使用者/脚本作者 |
| [shell-gui.md](shell-gui.md) | GUI 壳：入口模型、语言前置、标题头/欢迎页布局、9 预设页、内容驱动尺寸动效、进度页、静默/测试钩子、qrc 单文件分发 | GUI 集成者 |
| [shell-tui.md](shell-tui.md) | TUI 壳：ANSI 渲染架构、wcwidth 宽字符、组件集、语言（--lang/tr）、自动化钩子、字节级验证法 | TUI 使用者 |
| [build-guide.md](build-guide.md) | 构建指南：依赖、构建选项表（含 HCI_ICON_PATH）、/utf-8 编码、动态/静态 Qt、qrc 哈希内嵌、宿主集成、故障排查 | 构建维护者 |
| [pitfalls.md](pitfalls.md) | 踩坑清单：跨 DLL 静态、退出卸载、Lua 语法、qrc 压缩字节、AUTORCC 哈希、静默弹框挂死、下载异常等 | 所有开发者 |

## 顶层文档

- [../README.md](../README.md) — 项目概览、六大件、里程碑、快速开始
- [../DESIGN.md](../DESIGN.md) — 权威设计文档（架构决策与选型依据）
- [../usage.md](../usage.md) — 精简使用手册（快速上手）

## 代码结构速览

```
core/    hci_core   零 Qt 核心：总线/上下文/变量/执行库/下载后端(direct·github·powershell·curl)/payload/
                    lang/提权/脚本/日志/端口/入口/拓展注册表
flow/    hci_flow   流程控制器：JSON 步骤机 + Lua + 回退历史 + 下载链
ext/     hci_ext    拓展主机：接口 + 三种加载 + 注册表（步骤/参数/下载后端）
         hci_git    Git 策略通用拓展（git_plan/git_refresh + 三参数）
         hci_winget winget 下载后端拓展
         hci_apt    apt 下载后端拓展
cli/     hci_cli    CLI 壳（控制台子系统，零 Qt）
gui/     hci_gui    GUI 壳（Qt6 + HiBerGUILibrary，静态/动态 Qt，qrc 单文件分发）
tui/     hci_tui    TUI 壳（C++ ANSI + wcwidth）
examples/           demo 产品（普通 + qrc 单文件两个变体）+ 拓展示例
```

## 快速索引

- 我要定义一个产品 → [product-config.md](product-config.md) + [flow-script.md](flow-script.md)
- 我要写一个安装流程（含下载链/多启动）→ [flow-script.md](flow-script.md)
- 我要写一个拓展（参数/步骤/下载后端/DLL/包）→ [extension-dev.md](extension-dev.md)
- 我要在脚本里调用执行函数 → [core-api.md](core-api.md)
- 我要构建/打包单文件安装器 → [build-guide.md](build-guide.md)