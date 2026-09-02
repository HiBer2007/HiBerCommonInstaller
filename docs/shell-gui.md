# GUI 壳（hci_gui）参考

Qt6 Widgets + HiBerGUILibrary（submodule）的图形安装壳。控制台子系统（入口模型）：终端启动保留终端输出日志，双击释放控制台。

## 用法

```
hci_gui [options]
  --product <file.json>     product（默认：内嵌构建 qrc:/product.json；否则 ./product.json）
  --flow <install|uninstall|repair|upgrade|file.json>
  --path <dir>              install path override
  --lang <code>             language: en | zh（预置 vars.language；语言步骤直接采纳）
  --silent, -s              headless run（无窗口；错误走 stderr，不弹框）
  --gui / --help / --version
  其它未知参数 → 拓展 cliArgs 处理器（如 --with-editor）
```

## 入口与终端生命周期

1. `hasConsole()`：终端启动 → `setUtf8Console` + ConsoleSink（Debug 级）+ banner 输出；双击（无控制台）→ FileSink（%TEMP%/hci_gui.log，Debug 级）
2. `holdOrReleaseConsole()`：从终端启动保留；双击独占控制台 → FreeConsole
3. QApplication → 产品/流程加载（qrc 资源需在 QApplication 之后）→ `GuiShell::run()`（**语言步骤期间主窗口不显示**，选择完成/进入欢迎页才显示）

## 语言选择（欢迎页之前）

- 流程首个可选步骤 `{ "id": "lang", "ui": "language", "default": "zh" }`：GUI 弹独立小窗（列表 + OK/Cancel），**此时主窗口隐藏、欢迎页不渲染**；选择后应用语言并显示窗口
- 优先级：`--lang` 参数预置 > 步骤 `default` > product `defaultLanguage` > "en"
- 壳文本（按钮/标签/提示）按语言渲染（`hci::lang::tr`，内置 en/zh）；产品侧文本（产品名/流程 prompt/message）由产品配置负责

## 标题头与欢迎页

- **标题头**（欢迎页之外的所有页）：`<productName> 安装程序`（多语言「安装程序」）+ 下方**分割线**
- **欢迎页布局**（紧凑、内容右移，margins `48,16,16,8`）：大字 `welcomeTitle`（缺省 productName，22pt 粗体）→ 中等副标题「安装程序」（15pt）→ 描述「此向导将引导您完成安装。」（13pt，wordWrap）→ 右下角灰色小字 `product.branding.poweredBy`（缺省 `Powered by HiBer Common Installer Module`，11pt）

## 窗口尺寸（内容驱动 + 动效）

每次切页**按页面内容解算**：宽度 = `max(sizeHint.w, 560)`；高度优先 `layout()->heightForWidth(w)`（文本换行页精确），叠加标题头/分割线与页脚按钮的高度 → 屏幕可用区 clamp → **`QPropertyAnimation(geometry)` 200ms `OutCubic` 动效调节**（与主程序向导同款，结束后应用最终尺寸）。

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

内置 9 页（`ui` id ↔ 流程步骤 `ui` 字段）：

| id | 内容 | 结果（QVariant） | Next 门控 |
|---|---|---|---|
| `language` | 语言列表 + OK/Cancel（小窗，主窗口隐藏时弹出） | string（语言码） | — |
| `welcome` | 大字标题 + 副标 + 描述 + 右下 Powered（无标题头） | — | — |
| `license` | 协议文本（只读）+ 接受勾选 | bool | 需勾选 |
| `path` | 路径输入 + 浏览 + 目录清空警告 | string | 非空 |
| `components` | 组件勾选（required 锁定不可取消；**含必选组件**）+ 每项 description 描述 | QVariantList<bool> | 显式放行 |
| `git` | Git 检测结果（系统路径/未检测到）+ 系统/内置单选（系统缺失且 `installSystemOption` 时显示第三项「下载并安装系统 Git（需管理员）」） | string（"system"\|"bundled"\|"install-system"） | — |
| `option` | 单选（choices/default） | int | — |
| `confirm` | 提示 + "Yes, continue" 勾选 | bool | — |
| `input` | 文本输入 | string | 必填时非空 |
| `finish` | 成败大标题 + 消息 + 启动项（单 launch 时勾选框；**`launchOptions` 多启动项时列表多选**） | 单: bool；多: QVariantList<"name=path"> | — |

- Next/Back/Cancel 页脚按钮；Back（「上一步」）在非首步默认显示（product `backEnabled` 可全局关闭；步骤 `buttons.back:false` 临时禁用；`buttons.next/cancel:false` 同理），由 FlowRunner 历史回退重跑上一步；Cancel → ctx.cancel() + 终止；finish 无上一步
- 交互页经**嵌套事件循环**阻塞（`GuiShell::blockOnPage`），用户点击 Next/Back（或取消）后返回；切换伴随 160ms 淡入（主程序向导同款）
- 页面被 `deleteLater` 清理（勿在页面内持有跨页指针）
- 未注册的 ui id → 兜底页（"Unknown page"）

## 进度页（固定布局）

- 位于标题头分割线下：**步骤小字（左）+ 百分比（右）一行 → 进度条（恒定）→ 日志区（填满）**
- `onProgress(step, percent, detail)` 由流程控制器给整体进度（步骤粒度）+ 执行器细粒度；百分比右侧实时
- 日志区接收 `GuiLogSink` 镜像的框架日志（**Debug 级，极详**）；`onMessage` 追加（silent 时走 stderr）

## 静默 / 自动化

- `--silent`：GuiFlowUi 全部交互步骤取默认值（同 CLI 静默语义），不建窗口；**错误与流程失败经 stderr 输出**（不弹模态框，避免无头挂死）
- `HCI_GUI_AUTOPILOT=1`（环境变量）：等价 silent（测试钩子，供冒烟/CI）
- 静默 finish **不自动启动**应用（交互时才按勾选启动）

## qrc 单文件分发（HCI_PRODUCT_FILES）

产品配置、流程、payload、LICENSE 全部内嵌进 EXE（单文件分发，静态 Qt 时自包含）：

```cmake
# gui/CMakeLists.txt
if(HCI_PRODUCT_FILES)
    # 条目语法：alias=文件路径（alias 缺省 = 文件名）
    # 例：product.json=<p>;install.json=<p>;deploy/...=<f>;...
```

- **qrc 目标以内容哈希命名**（`hci_product_<hash>.qrc`）：内嵌文件（json/product/deploy）变更自动触发新 rcc 目标——不再需要手动重跑 configure 也能拿到最新内容
- **资源注册**：`Q_INIT_RESOURCE` 经宏间接展开（`HCI_PRODUCT_QRC_NAME` 编译定义指向哈希命名单元）
- **资源读取统一走 `QResource::uncompressedData()` 直读**（`gui::readResource`，`resource_utils.{h,cpp}`）——Qt6 的 `data()` 返回压缩字节，必须解压（曾引发内嵌 product/flow 全部解析失败的终极根因）
- 运行：`hci_gui --product qrc:/product.json --flow install --silent --path D:\App`
- 外部绝对流程路径（调试/测试）与 qrc 产品可混用

## 布局失配守则

改动 `gui_shell.h`/页面共享头类成员后，确认引用它们的每个 TU 重编（Ninja 头依赖可能漏检），部署后必做 GUI 冒烟（启动 → 3-4s → 关闭，无 HEAP CORRUPTION / crash-report 新增）。

## 冒烟清单

1. 静默全流程：`hci_gui ... --silent` → exit 0 + 产物核对（含下载链/提权日志 in stderr）
2. 窗口：启动 → 4s 存活 + 有窗口句柄 → CloseMainWindow → exit 0/1 且无崩溃（取消语义 exit 1 正常）
3. 语言窗先于主窗口；逐页尺寸动画与标题头/分割线正常
4. 拓展：`extensions/<你的拓展>.dll` 存在 → 专属参数生效（如 `--with-editor` 预选组件）