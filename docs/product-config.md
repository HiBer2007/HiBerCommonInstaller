# product.json — 产品配置完整参考

安装器的一切产品属性（名称、组件、路径、快捷方式、配置模板、流程、payload、卸载项、提权、拓展配置）由一份 UTF-8 JSON 定义。壳启动时经 `hci::ProductConfig::loadFile(path)` / `loadString(text)` 解析（解析失败抛 `std::runtime_error`）。

## 完整结构（字段逐字对应 `hci::ProductConfig`，见 `core/include/hci/product.h`）

```json
{
  "productName": "MyApp",                 // ProductConfig::productName — 窗口/欢迎页/横幅
  "company": "HiBer2007",                 // ProductConfig::company
  "orgName": "HiBer2007",                 // ProductConfig::orgName — GUI QApplication 组织名
  "version": "1.0.0",                     // ProductConfig::version（默认 "1.0.0"）→ 变量 {productVersion}
  "icon": "",                             // ProductConfig::icon（暂未启用）
  "defaultMode": "gui",                   // 默认壳："gui" | "tui" | "cli"（缺省 "gui"）
  "defaultLanguage": "zh",                // GUI/壳默认语言 "en"|"zh"（缺省 "en"）；language 步骤 default 优先，--lang 再优先
  "backEnabled": true,                    // GUI「上一步」按钮全局开关（缺省 true）
  "welcomeTitle": "My App",               // GUI 欢迎页大标题（缺省 productName）
  "defaultInstallPath": "%APPDATA%\\MyApp", // 默认安装目录；支持 %VAR% 环境变量占位（流程 seed 时经 port::expandEnv 展开）；可空 → 由流程 path 页决定
  "banner": { "font": "slant" },          // bannerFont（缺省 "slant"；"neo" = 主程序 CLI 同款拼接字；未知字体回退内置表）
  "branding": {                           // 品牌行自定义（缺省保留铁律原文）
    "poweredBy": "Powered by HiBer Common Installer Module"
  },

  "components": [                         // 组件列表（核心功能，三壳统一交互）
    { "id": "editor",                     // ProductComponent::id — 变量键 components.<id>
      "label": "Editor Component",        // 组件展示名
      "description": "说明文字",           // 组件描述（GUI components 页 label 下方灰色小字；可空）
      "exe": "Editor.exe",                // 安装根下可执行文件（可为空）
      "shortcutName": "MyApp Editor",     // 选中时的快捷方式名（可为空）
      "required": false,                  // 必选（GUI 禁用勾选并强制选中，仍显示在列表中）
      "defaultChecked": false }           // 默认勾选
  ],

  "shortcuts": [                          // 快捷方式定义（注意：实际创建由流程里 shortcut 步骤执行）
    { "kind": "desktop",                  // "desktop" | "startmenu"
      "name": "MyApp.lnk",                // 可含 '/' 子目录，如 "MyApp/MyApp.lnk"
      "target": "{installDir}/MyApp.exe", // 目标（支持 {var} 模板）
      "args": "" }
  ],

  "installConf": {
    "fileName": "install.conf",           // 产物文件名（缺省 "install.conf"）
    "header": "# MyApp Install Configuration",  // 首行注释（可选）
    "template": {                         // key=value 逐行写出，value 支持 {var}
      "install_path": "{installDir}",
      "install_editor": "{components.editor}",
      "app_version": "{productVersion}"
    }
  },

  "flows": {
    "install": "install.json",            // 安装流程（文件路径或 "qrc:/..."）
    "uninstall": "uninstall.json",        // 卸载流程
    "repair": "repair.json",              // 可空：修复/重装流程（--flow repair 触发）
    "upgrade": "upgrade.json"             // 可空：升级流程（--flow upgrade 触发）
  },

  "payload": {
    "source": "qrc:/deploy",              // "dir:<path>" | "zip:<path>" | "qrc:/prefix"
    "skip": ["*.dmp", "config/custom/*"]  // 释放时跳过（通配 * ?；相对于 payload 根）
  },

  "uninstall": {
    "registryKey": "Software/Company/MyApp",  // 卸载注册表键（流程里可引用；HKCU）
    "displayName": "MyApp"                    // 卸载显示名
  },

  "elevation": {                          // 提权（UAC）策略，可空
    "request": false,                     // 流程启动时即申请管理员（缺省 false）
    "autoRestart": true,                  // 申请时自动以管理员重启（缺省 true）
    "reason": "需要管理员权限"             // 重启前向用户展示的说明（可空）
  },

  "extensions": {                         // 拓展功能配置段（产品元数据 → 拓展）
    "hci.git": { "gitDir": "tools/git" }  // 键 = 拓展 id；值 = 该拓展自定义键
  }
}
```

结构体对应关系（逐字）：

| JSON 路径 | C++ 类型 | 说明 |
|---|---|---|
| 顶层 | `hci::ProductConfig` | 全部标量字段同名（`productName/company/orgName/version/icon/defaultMode/defaultLanguage/backEnabled/welcomeTitle/poweredBy/defaultInstallPath/bannerFont`）；`loadFile/loadString` 静态工厂 |
| `components[]` | `hci::ProductComponent` | `id/label/description/exe/shortcutName/required/defaultChecked` |
| `shortcuts[]` | `hci::ShortcutSpec` | `kind/name/target/args`（kind: "desktop"\|"startmenu"） |
| `installConf` | `hci::InstallConfSpec` | `fileName/header/template_`（JSON 键 `template`） |
| `flows` | `hci::ProductFlows` | `install/uninstall/repair/upgrade`（后两者可空） |
| `payload` | `hci::DeploySpec` | `source/skip`（见 payload.h） |
| `uninstall` | `hci::UninstallSpec` | `registryKey/displayName` |
| `elevation` | `hci::ElevationSpec` | `request/autoRestart/reason`（缺省 request=false, autoRestart=true） |
| `extensions` | `nlohmann::json` → `ProductConfig::extensionConfig` | 按拓展 id 分段的配置对象；拓展经 `HostApi::extensionConfig(id)` 读取 |

## 字段语义与变量注入

解析后，壳启动流程时自动注入同名变量（`hci::FlowRunner::run`）：

- `{productName}` `{company}` `{productVersion}` — product 标量
- `{installDir}` — 默认取 `defaultInstallPath`（**先经 `port::expandEnv` 展开 `%VAR%`**；`--path` / 流程 path 页优先）
- `{tempDir}` `{exeDir}` — 运行时环境
- `{components.<id>}` — 组件勾选结果（"true"/"false"）

这些变量可在 `installConf.template`、步骤参数、`when` 条件中直接使用。

## 示例速查

- 最小 demo（目录 payload）：`examples/demo/product.json`
- 单文件内嵌 demo（qrc payload）：`examples/demo_qrc/product.json`

## 注意事项

- **路径模板**：`ShortcutSpec.target` 与步骤参数支持 `{var}` 插值（`${name}` 与 `{name}` 均可用；未知占位插值为空串）
- **默认路径环境变量**：`defaultInstallPath` 可写 `%APPDATA%\App` 等占位（`%NAME%` → `port::getEnv`），在流程 seed 时展开；也可用于步骤参数（一切 `{var}` 插值后的相对/绝对路径均可含 `%VAR%`）
- **JSON 编码**：必须 UTF-8（无 BOM 亦可）；中文 label 直接书写
- **`defaultMode`**：入口判定顺序 = 显式 `--gui/--tui/--cli` > `defaultMode`；GUI 未编译时给出明确错误
- **components 预置**：拓展参数处理器（如 CLI `--with-editor`）可在 components 页**之前**设好 `components.<id>` 变量；流程的 components 页会以"已有变量优先"初始化勾选（交互选择仍以用户为准）
- **语言优先级**：`--lang <code>` 参数（预置 `vars.language`）> `language` 步骤 `default` > `defaultLanguage` > `"en"`