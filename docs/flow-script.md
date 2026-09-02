# 流程脚本（Flow Script）参考

流程 = JSON 声明式步骤列表 + Lua 内嵌脚本。由 `hci::FlowRunner` 执行（`flow/include/hci/flow.h`），步骤要么是**交互页**（`ui` 字段，壳渲染），要么是**执行动作**（`type` 字段，核心执行器），二选一。

## 顶层结构

```json
{
  "id": "install",                          // FlowSpec::id（默认 "flow"）
  "vars": { "k": "v", ... },                // 种子变量（宿主/CLI 已设的变量不覆盖）
  "steps": [ { 步骤... }, ... ]
}
```

## 步骤字段（`hci::FlowStep`，逐字）

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | string | 步骤唯一 ID（必填；`next` 跳转目标） |
| `type` | string | 执行步骤类型（见下表）；与 `ui` 互斥 |
| `ui` | string | 交互页 id（见下表）；与 `type` 互斥 |
| `next` | string | 显式跳转：下一跳步骤 id，`"__end"` 提前结束 |
| `when` | string | Lua 表达式；**false = 跳过本步（非错误）**；空 = 恒执行 |
| `onFail` | string | 失败策略：`"abort"`（默认，终止运行）/ `"ignore"`（记日志继续） |
| `buttons` | object | 页脚按钮临时禁用（GUI）：`{"back": false, "next": false, "cancel": false}`；缺失键保持默认（back 另受 product `backEnabled` 与是否有上一步影响） |
| 其余字段 | — | 进入 `params`（步骤参数，见下表） |

**执行顺序**：when 判定 → （跳过则按 `next` 跳转）→ ui/type 执行 → `next` 跳转。

## 交互页（ui 步骤）

| ui | param | 交互 | 变量写入 |
|---|---|---|---|
| `welcome` | — | 欢迎页（产品名 + Powered by banner） | — |
| `language` | `default`（如 "zh"，缺省取 product `defaultLanguage`/“en”） | 语言选择小窗（GUI；CLI/TUI 直接采纳默认） | `language` |
| `license` | `source`（文本文件或 `qrc:/...`，GUI 需 ResourceReader） | 阅读 + 勾选接受 | —（拒绝 = 流程失败） |
| `path` | — | 安装路径输入/选择 | `installDir` |
| `components` | — | 组件勾选（来自 product.components） | `components.<id>` |
| `option` | `prompt/choices[]/default` | 单选 | `name`（默认=步骤 id）→ 选中序号 |
| `confirm` | `prompt/defaultYes/abortIfNo/name` | 确认 | `name`（默认=步骤 id）；`abortIfNo=true` 且否 → 流程失败 |
| `input` | `prompt/name/required` | 文本输入 | `name`；必填输入为空 → 失败 |
| `finish` | `message/launch`（安装根相对 exe） | 完成页（成功/失败 + 启动勾选） | `launchNow` |

交互页返回 false（用户取消）→ 流程中止。

## 执行步骤（type 步骤）

| type | 关键 param | 行为 |
|---|---|---|
| `script` | `script`（Lua 代码串） | 运行脚本块（`print` 直出终端） |
| `extract` | `source`（dir:/zip:/qrc:/）、`target`、`skip[]`、`7za`（.7z 解压器路径） | payload 释放到 target（目录优先、稳定排序、进度回调） |
| `copy` | `source/target/skip[]` | 目录树复制 |
| `clean` | `target` | 清空目录内容（保留目录本身） |
| `download` | `url` 或 `asset`（GitHub repo）+ `variant`、`dest`、`7za` | 下载；`asset` 模式走 GitHub Releases API（404/限流返回错误） |
| `run` | `program/args[]/waitMs`（默认 60000） | 执行外部进程；超时/非零退出 = 失败；输出进 `run.output` |
| `shortcut` | `kind`（desktop/startmenu）、`name`（可含 `/` 子目录）、`target`、`workDir`、`args` | IShellLink 创建（POSIX v1 返回失败） |
| `template` | `target`（目录）、`file`（默认 install.conf）、`header`、`template{key:value}` | 渲染 `key=value` 文本文件（value 支持 {var}） |
| `registry` | `key/name/value` 或 `action=delete` | HKCU 注册表写/删（WIN32） |
| 其它 | — | 交拓展注册表（`ExtensionRegistry::runStep`）；未注册 → `unknown step type` 失败 |

## 变量与模板

- 写入：ui 步骤写 `installDir/components.*/launchNow` 等；执行步骤写 `run.output`；拓展可写任意键
- 预置：`productName/company/productVersion/installDir/tempDir/exeDir`（见 [product-config.md](product-config.md)）
- 插值：`{name}` 与 `${name}` 均可；递归展开（最多 8 轮，环安全）；未知键 → 空串
- **Lua 访问**：全局表 `vars`，键就是变量名（**带点号的键必须方括号** `vars['components.editor']`；`vars.components.editor` 会索引 nil）

## Lua 参考（when / script）

引擎 = 内嵌 Lua（vcpkg `lua` port，当前 5.5，API 兼容 5.4；`hci::LuaEngine`，`IScriptEngine` 可替换）。

- `when` 语义：`eval("return (" + expr + ")")` → 结果真值（`"true"/"1"`/非空非 `"false"` 均真）；**等于判断用 `==`，不等用 `~=`（Lua 无 `!=`）**
- 脚本内可读写 `vars` 表（写操作在进程内生效但**不回写**宿主 Vars——如需持久化请用拓展步骤）
- 常用：`vars['components.editor'] == 'true'`、`string.lower(vars.gitMode)`、`os.getenv('X')`

## 卸载流程

同引擎第二条流程（`flows.uninstall`）。典型结构：`confirm（abortIfNo）→ registry delete → clean → finish`。卸载入口（开始菜单卸载项等）由产品自行注册（v1 未内置注册表卸载入口条目写入——`UninstallSpec` 预留）。

## 示例

```json
{
  "id": "install",
  "steps": [
    { "id": "welcome", "ui": "welcome" },
    { "id": "license", "ui": "license", "source": "qrc:/LICENSE.txt" },
    { "id": "components", "ui": "components" },
    { "id": "customPlan", "type": "acme_git_plan",      // 拓展步骤示例
      "when": "vars.gitPlanned ~= 'true'" },
    { "id": "gitDownload", "type": "download", "asset": "git-for-windows/git",
      "variant": "{gitVariant}", "dest": "{tempDir}/neo_git.zip",
      "when": "vars.gitDownload == 'true'" },
    { "id": "extractApp", "type": "extract", "source": "qrc:/deploy",
      "target": "{installDir}", "skip": ["*.dmp"] },
    { "id": "writeconf", "type": "template", "target": "{installDir}",
      "file": "install.conf", "header": "# MyApp",
      "template": { "install_path": "{installDir}" } },
    { "id": "shortcut1", "type": "shortcut", "kind": "desktop",
      "name": "MyApp.lnk", "target": "{installDir}/MyApp.exe" },
    { "id": "finish", "ui": "finish", "message": "安装完成", "launch": "MyApp.exe" }
  ]
}
```

## 路径解析规则

- 相对路径以**流程文件所在目录**为基准（`FlowRunner::setBaseDir`）
- `qrc:` / `http(s):` / 绝对路径（盘符、`/`、`\` 起头）原样使用
- product 来自 qrc 时，流程相对名自动解析为 `qrc:/<name>`