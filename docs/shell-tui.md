# TUI 壳（hci_tui）参考

C++ ANSI 终端壳（零 Qt）：内嵌 wcwidth 宽字符表（CJK/emoji 感知布局）、行式交互组件、流程驱动渲染。

## 用法

```
hci_tui [options]
  --product <file.json> / --flow <install|uninstall|repair|upgrade|file.json>
  --path <dir> / --lang <en|zh> / --tui / --help / --version
```

**前提**：必须存在交互终端（`hasConsole()`）；无终端（CreateProcessA 场景）→ 明确报错并提示改用 hci_cli（autopilot 测试钩子可绕过）。

**语言**：`--lang <code>` 预置（TuiShell 构造语言参数，`ctx.vars().language` 同步）；未指定时取 product `defaultLanguage`（缺省 en）。壳文本（交互标签/提示）经 `hci::lang::tr(lang_, en)` 渲染（zh 时中文）；language 步骤（如有）默认实现采纳既定值不弹窗。

## 渲染架构（`tui/src/tui_shell.{h,cpp}`）

- 终端初始化：`CP_UTF8` + `ENABLE_VIRTUAL_TERMINAL_PROCESSING` + `_O_BINARY`（主程序同款铁律）
- **内嵌 wcwidth**：`displayWidth(utf8)` 按 Unicode 范围判定宽度（CJK 统一表意/假名/谚文/全角/emoji = 2 格；组合字符/变体选择符 = 0 格；表见 `tui_shell.cpp` 的 `isWide/isZeroWidth`）
- 布局：`wrapText(text, width)` 按显示宽度折行（菜单/协议文本）；`COLUMNS` 环境变量或默认 80
- 颜色：`ansi(code, text)` → `\x1b[<code>m...\x1b[0m`

## 公共 API（逐字）

```cpp
size_t displayWidth(const std::string& utf8);
std::vector<std::string> wrapText(const std::string& text, size_t width);
std::string ansi(const char* code, const std::string& text);

class TuiShell {
    TuiShell(const hci::ProductConfig& product, const std::string& flowFile,
             const std::string& installPath, const std::string& language = "");
    int run();
    // Rendering
    void clear();                       // ESC[2J ESC[H
    void print(const std::string& line);
    void printTitle(const std::string& line);
    void printError(const std::string& line);
    void printProgress(const std::string& label, int percent);   // [████░░] 45% label（\r 原地刷新）
    // Input
    std::string prompt(const std::string& question, const std::string& def = "");
    bool confirm(const std::string& question, bool defaultYes = true);
    int select(const std::string& prompt, const std::vector<std::string>& choices,
               int defaultIndex = 0);
    bool toggleList(const std::string& title, const std::vector<std::string>& labels,
                    std::vector<bool>& checked);
    hci::ProductConfig& product();
    hci::InstallContext& context();
    const std::string& flowFile() const;
    bool autopilot() const;
    size_t width() const;
    const std::string& language() const;
    void setLanguage(const std::string& l);
    std::string tr(const std::string& en) const;  // hci::lang::tr(lang_, en)
};
```

交互步骤由 `TuiFlowUi : hci::IFlowUi` 驱动（壳文本随语言渲染）：

| 流程 ui | TUI 呈现 |
|---|---|
| welcome | 清屏 + banner + 产品标题 + 「按回车继续」 |
| license | 标题 + 协议文本（40 行截断 + 折行）+ 「接受许可协议？[Y/n]」 |
| path | `安装目录 [default]: ` |
| components | `toggleList`（`[x]` 标记 + `toggle id, blank = done`） |
| git | 默认实现（`onGit`）直接采纳默认模式（参数预置时亦然）；交互选择留给实机 |
| option | 编号列表 `<n>` |
| confirm | `[Y/n]` |
| input | `prompt: `（必填重试循环） |
| finish | 成功/失败大字 + launch 提示 + 回车退出 |

## 自动化钩子

`HCI_TUI_AUTOPILOT=1`：全部交互取默认值（license 接受、default 路径、默认勾选……），用于字节级验证/CI。同时绕过终端检查（无终端也可跑全流程，便于管道验证）。

## 字节级验证法（铁律）

UTF-8 字节流在 GBK 测试终端显示 `����` 是**显示层假象**。判据：

```powershell
# cmd 重定向拿原始字节 → UTF-8 解码 → 数 U+FFFD + 关键子串
$text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($raw))
# 期望：FFFD count = 0，且 Contains 目标子串（如 U+2588 方块、Powered by）
```

## 已知实现细节

- 进度条方块 = `"\xE2\x96\x88"`（U+2588）/ `"\xE2\x96\x91"`（U+2591）**字节串拼接**——写多字符字面量 `'\xE2\x96\x88'` 会被 MSVC 截断成单字节（实测量产乱码）
- 宽字符对齐：进度条/表格按 `displayWidth` 计算，中文 2 格
- 无滚屏回看（页面式渲染 + 日志行内输出）；长流程建议 GUI/CLI