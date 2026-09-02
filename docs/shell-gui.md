# GUI 壳（hci_gui）参考

Qt6 Widgets + HiBerGUILibrary（submodule）的图形安装壳。控制台子系统（入口模型）：终端启动保留终端输出日志，双击释放控制台。

## 用法

```
hci_gui [options]
  --product <file.json>     product（默认 ./product.json；支持 qrc:/product.json）
  --flow <install|uninstall|file.json>
  --path <dir>              install path override
  --silent, -s              headless run（无窗口）
  --gui / --help / --version
  其它未知参数 → 拓展 cliArgs 处理器（如 --with-editor）
```

## 入口与终端生命周期

1. `hasConsole()`：终端启动 → `setUtf8Console` + ConsoleSink + banner 输出；双击（无控制台）→ FileSink（%TEMP%/hci_gui.log）
2. `holdOrReleaseConsole()`：从终端启动保留；双击独占控制台 → FreeConsole
3. QApplication → 产品/流程加载（qrc 资源需在 QApplication 之后）→ 窗口（silent 无窗口）→ `GuiShell::run()`

## 页面预设（PageFactory，`gui/src/pages.cpp`）

注册表 API（供拓展注册新页面）：

```cpp
using PageFactory = std::function<QWidget*(const nlohmann::json& params,
                                           GuiShell& shell, QVariant& result)>;
void registerPage(const std::string& uiId, PageFactory factory);   // 页面注册
QWidget* createPage(const std::string& uiId, const nlohmann::json& params,
                    GuiShell& shell, QVariant& result);            // 按 id 建页
void unregisterAllPages();
```

内置 8 页（`ui` id ↔ 流程步骤 `ui` 字段）：

| id | 内容 | 结果（QVariant） | Next 门控 |
|---|---|---|---|
| `welcome` | 产品 ASCII 横幅 + 介绍 + Powered by | — | — |
| `license` | 协议文本（只读）+ 接受勾选 | bool | 需勾选 |
| `path` | 路径输入 + 浏览 + 目录清空警告 | string | 非空 |
| `components` | 组件勾选（required 锁定） | QVariantList<bool> | — |
| `option` | 单选（choices/default） | int | — |
| `confirm` | 提示 + "Yes, continue" 勾选 | bool | — |
| `input` | 文本输入 | string | 必填时非空 |
| `finish` | 成败大标题 + 消息 + 「立即运行」勾选 | bool(launch) | — |

- Next/Back/Cancel 页脚按钮；Back（「上一步」）在非首步默认显示（product `backEnabled` 可全局关闭；步骤 `buttons.back:false` 临时禁用），由 FlowRunner 历史回退重跑上一步；Cancel → ctx.cancel() + 终止
- 交互页经**嵌套事件循环**阻塞（`GuiShell::blockOnPage`），用户点击 Next/Back（或取消）后返回
- **多语言**：壳文本（按钮/标签/提示）按当前语言渲染（内置 en/zh 表，`hci::gui::tr`）；语言由流程首个可选步骤 `{"ui":"language","default":"zh"}` 选定（GUI 弹选择小窗；product `defaultLanguage` 为兜底）；产品侧文本（产品名/流程 prompt/message）由产品配置负责
- **欢迎页布局**：左上产品名大字（26pt 粗体）→ 下方引导文本 → 右下角灰色小字 `Powered by HiBer Common Installer Module`（引导文本紧贴其上方）
- 页面被 `deleteLater` 清理（勿在页面内持有跨页指针）
- 未注册的 ui id → 兜底页（"Unknown page"）

## 进度页

- 常驻（`GuiShell::buildProgressPage`）：`HiBerGUI::ProgressCard`（showCard/setProgress）+ 步骤标签 + QProgressBar + 日志 QTextEdit
- `onProgress` 自动切换到进度页；`onMessage` 追加日志（silent 时走 stderr）

## 静默 / 自动化

- `--silent`：GuiFlowUi 全部交互步骤取默认值（同 CLI 静默语义），不建窗口；错误经 stderr 暴露
- `HCI_GUI_AUTOPILOT=1`（环境变量）：等价 silent（测试钩子，供冒烟/CI）

## qrc 单文件分发（HCI_PRODUCT_FILES）

产品配置、流程、payload、LICENSE 全部内嵌进 EXE（单文件分发，静态 Qt 时自包含）：

```cmake
# gui/CMakeLists.txt
if(HCI_PRODUCT_FILES)
    # 条目语法：alias=文件路径（alias 缺省 = 文件名）
    # 例：product.json=<p>;install.json=<p>;deploy/...=<f>;...
```

- qrc 前缀 `/`；`deploy/...` 别名区即 payload 前缀；qrc 资源注册 = `Q_INIT_RESOURCE(hci_product)`（main.cpp，`HCI_EMBED_PRODUCT` 宏控制）
- **资源读取统一走 `QResource` 直读**（`gui::readResource`，`resource_utils.{h,cpp}`）——QFile 打开 qrc 资源在本环境不可靠（已实证）
- 运行：`hci_gui --product qrc:/product.json --flow install --silent --path D:\App`
- 内嵌内容变更后必须**重新 configure**（configure 期生成 qrc；改了 json 只 rebuild 不生效）

## 布局失配守则

改动 `gui_shell.h`/页面共享头类成员后，确认引用它们的每个 TU 重编（Ninja 头依赖可能漏检），部署后必做 GUI 冒烟（启动 → 3-4s → 关闭，无 HEAP CORRUPTION / crash-report 新增）。

## 冒烟清单

1. 静默全流程：`hci_gui ... --silent` → exit 0 + 产物核对
2. 窗口：启动 → 4s 存活 + 有窗口句柄 → CloseMainWindow → exit 0/1 且无崩溃（取消语义 exit 1 正常）
3. 拓展：`extensions/<你的拓展>.dll` 存在 → 专属参数生效（如 `--with-editor` 预选组件）