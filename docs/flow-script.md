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
| `buttons` | object | 页脚按钮**临时禁用**（GUI）：`{"back": false, "next": false, "cancel": false}`；缺失键保持默认（back 另受 product `backEnabled` 与是否存在上一步影响） |
| 其余字段 | — | 进入 `params`（步骤参数，见下表） |

**执行顺序**：when 判定 →（跳过则按 `next` 跳转）→ 步骤前经 `ui_->onStepParam(params, canGoBack)` 通知 UI → ui/type 执行 → `next` 跳转。

## 交互页（ui 步骤）

| ui | param | 交互 | 变量写入 |
|---|---|---|---|
| `language` | `default`（如 "zh"） | 语言选择小窗（GUI；CLI/TUI 采纳默认）。**优先级**：`--lang` 参数预置的 `vars.language` > 本步骤 `default` > product `defaultLanguage` > "en" | `language` |
| `welcome` | — | 欢迎页（产品名 + Powered by banner） | — |
| `license` | `source`（文本文件或 `qrc:/...`，GUI 需 ResourceReader） | 阅读 + 勾选接受 | —（拒绝 = 流程失败） |
| `path` | — | 安装路径输入/选择 | `installDir`；**随后自动写权限探测**（目标目录不可写 → 申请提权重启） |
| `components` | — | 组件勾选（来自 product.components，含 required 必选） | `components.<id>` |
| `git` | `installSystemOption`（bool，系统 Git 缺失时显示第三选项「下载并安装系统 Git」）、`default` | Git 方案选择：自动检测系统 Git（结果显示在页上）+ 系统/内置（/安装系统）三选 | `gitMode`（"system"\|"bundled"\|"install-system"）、`gitSystemAvailable`、`gitUseSystem`、`gitDownload` |
| `option` | `prompt/choices[]/default` | 单选 | `name`（默认=步骤 id）→ 选中序号 |
| `confirm` | `prompt/defaultYes/abortIfNo/name` | 确认 | `name`（默认=步骤 id）；`abortIfNo=true` 且否 → 流程失败 |
| `input` | `prompt/name/required` | 文本输入 | `name`；必填输入为空 → 失败 |
| `elevate` | `reason`（提示文本）、`autoRestart`（默认 true） | **中途提权**：非管理员时申请提权重启（GUI 弹确认；CLI/TUI 自动重启）；已提权进程直接通过；用户拒绝 → 步骤失败（abort/ignore 可选） | — |
| `finish` | `message/launch`（安装根相对 exe）、`launchOptions`（`[{ "name": "...", "path": "{installDir}/x.exe" }, ...]` 多启动项） | 完成页（成功/失败 + 启动勾选；多选项时列表勾选） | `launchNow` |

交互页返回 false（用户取消）→ 流程中止（**上一步**由 `backRequested()` 区分：GUI「上一步」按钮 → FlowRunner 从历史重新执行上一步骤）。

## 执行步骤（type 步骤）

| type | 关键 param | 行为 |
|---|---|---|
| `script` | `script`（Lua 代码串） | 运行脚本块（`print` 直出终端） |
| `extract` | `source`（dir:/zip:/qrc:/）、`target`、`skip[]`、`7za`（.7z 解压器路径） | payload 释放到 target（目录优先、稳定排序、进度回调） |
| `copy` | `source/target/skip[]` | 目录树复制 |
| `clean` | `target` | 清空目录内容（保留目录本身） |
| `download` | 见下方「下载后端链」 | 多后端链下载/安装 |
| `run` | `program/args[]/waitMs`（默认 60000；**<0 = 分离启动不等待**） | 执行外部进程；超时/非零退出 = 失败；输出进 `run.output` |
| `shortcut` | `kind`（desktop/startmenu）、`name`（可含 `/` 子目录）、`target`、`workDir`、`args`、**`enabled`**（`false` 跳过创建，测试/静默用） | IShellLink 创建（POSIX v1 返回失败） |
| `template` | `target`（目录）、`file`（默认 install.conf）、`header`、`template{key:value}` | 渲染 `key=value` 文本文件（value 支持 {var}） |
| `registry` | `key/name/value` 或 `action=delete` | HKCU 注册表写/删（WIN32） |
| `git_plan` / `git_refresh` | 由 hci_git 拓展提供（见 [extension-dev.md](extension-dev.md)） | Git 策略决策 / 系统 Git 再探测 |
| 其它 | — | 交拓展注册表（`ExtensionRegistry::runStep`）；未注册 → `unknown step type` 失败 |

### 下载后端链（`download` 步骤）

参数：`url`（直链）\| `asset`（GitHub repo）+ `variant` + 可选 `ext:"exe"`（匹配安装器 .exe 资产）\| `package`（包管理器包名）；`dest`（输出文件；安装式后端可省略）；**`chain`**（字符串数组，后端按序尝试，首个适用且成功即止）或 **`backend`**（单后端快捷）；缺省链 `["github", "direct"]`。

内置后端：`direct`（直链，cpr）、`github`（GitHub Releases asset）、`powershell`（Invoke-WebRequest，.NET SChannel——本机网络异常时常用回退）、`curl`（curl.exe `-L --fail`）。

拓展后端（经拓展注册表注入）：`winget`（hci_winget，`winget install --exact --id` 静默；Windows）、`apt`（hci_apt，`apt-get install -y`；POSIX）——自定义后端见 [extension-dev.md](extension-dev.md)「下载后端」。

```json
{ "type": "download", "url": "https://…", "dest": "{tempDir}/a.bin", "chain": ["curl", "powershell", "direct"] }
{ "type": "download", "asset": "git-for-windows/git", "variant": "MinGit", "dest": "{tempDir}/g.zip" }
{ "type": "download", "package": "Git.Git", "backend": "winget" }
{ "type": "download", "asset": "git-for-windows/git", "variant": "Git-", "ext": "exe", "dest": "{tempDir}/gi.exe" }
```

## 变量与模板

- 写入：ui 步骤写 `installDir/components.*/launchNow/gitMode/gitUseSystem/gitDownload` 等；hci_git 写 `gitInstallDir/gitPath/gitInstallKind/gitVariant/gitPlanned`；执行步骤写 `run.output`；拓展可写任意键
- 预置：`productName/company/productVersion/installDir（%VAR% 展开）/tempDir/exeDir`（见 [product-config.md](product-config.md)）
- 插值：`{name}` 与 `${name}` 均可；递归展开（最多 8 轮，环安全）；未知键 → 空串
- **Lua 访问**：全局表 `vars`，键就是变量名（**带点号的键必须方括号** `vars['components.editor']`；`vars.components.editor` 会索引 nil）

## Lua 参考（when / script）

引擎 = 内嵌 Lua（vcpkg `lua` port，当前 5.5，API 兼容 5.4；`hci::LuaEngine`，`IScriptEngine` 可替换）。

- `when` 语义：`eval("return (" + expr + ")")` → 结果真值（`"true"/"1"`/非空非 `"false"` 均真）；**等于判断用 `==`，不等用 `~=`（Lua 无 `!=`；逻辑与/或用 `and`/`or`，无 `&&`/`||`）**
- 脚本内可读写 `vars` 表（写操作在进程内生效但**不回写**宿主 Vars——如需持久化请用拓展步骤）
- 常用：`vars['components.editor'] == 'true'`、`vars.gitInstallKind ~= 'system'`、`string.lower(vars.gitMode)`、`os.getenv('X')`

## 回退（上一步）

GUI「上一步」按钮：`IFlowUi::backRequested()` + FlowRunner 维护**已执行步骤历史**（`history_`）——交互返回 false 且 `backRequested()` 为真 → 从历史回退一步重跑（变量重新计算；welcome 无上一步）。

## 卸载 / 修复 / 升级

- **卸载**：`flows.uninstall`（`--flow uninstall`）。典型：`confirm（abortIfNo）→ registry delete → clean → finish`
- **修复**：`flows.repair`（`--flow repair`）——重新释放部署文件 + 刷新快捷方式（保留配置与数据）
- **升级**：`flows.upgrade`（`--flow upgrade`）——更新部署文件到最新版本（保留配置与数据）

## 示例（含 git 选择页 + 多启动）

```json
{
  "id": "install",
  "steps": [
    { "id": "lang", "ui": "language", "default": "zh" },
    { "id": "welcome", "ui": "welcome" },
    { "id": "license", "ui": "license", "source": "qrc:/LICENSE.txt" },
    { "id": "path", "ui": "path" },
    { "id": "components", "ui": "components" },
    { "id": "gitChoice", "ui": "git", "installSystemOption": true },
    { "id": "gitPlan", "type": "git_plan", "editorComponent": "editor",
      "when": "vars.gitPlanned ~= 'true'" },
    { "id": "gitDownload", "type": "download", "asset": "git-for-windows/git",
      "variant": "{gitVariant}",
      "dest": "{tempDir}/neo_git.zip",
      "when": "vars.gitDownload == 'true' and vars.gitInstallKind ~= 'system'" },
    { "id": "gitExtract", "type": "extract", "source": "zip:{tempDir}/neo_git.zip",
      "target": "{gitInstallDir}",
      "when": "vars.gitDownload == 'true' and vars.gitInstallKind ~= 'system'" },
    { "id": "gitInstallerDownload", "type": "download", "asset": "git-for-windows/git",
      "variant": "Git-", "ext": "exe", "dest": "{gitInstallerPath}",
      "when": "vars.gitInstallKind == 'system'" },
    { "id": "gitElevate", "ui": "elevate", "reason": "需要管理员权限安装系统 Git",
      "when": "vars.gitInstallKind == 'system'" },
    { "id": "gitRunInstaller", "type": "run", "program": "{gitInstallerPath}",
      "args": ["/VERYSILENT", "/NORESTART"],
      "when": "vars.gitInstallKind == 'system'" },
    { "id": "gitRefresh", "type": "git_refresh",
      "when": "vars.gitInstallKind == 'system'" },
    { "id": "extractApp", "type": "extract", "source": "qrc:/deploy",
      "target": "{installDir}", "skip": ["*.dmp"] },
    { "id": "writeconf", "type": "template", "target": "{installDir}",
      "file": "install.conf", "header": "# MyApp",
      "template": { "install_path": "{installDir}", "git_path": "{gitPath}" } },
    { "id": "shortcut1", "type": "shortcut", "kind": "desktop",
      "name": "MyApp.lnk", "target": "{installDir}/MyApp.exe" },
    { "id": "finish", "ui": "finish", "message": "安装完成",
      "launchOptions": [
        { "name": "MyApp", "path": "{installDir}/MyApp.exe" },
        { "name": "Editor", "path": "{installDir}/Editor.exe" }
      ] }
  ]
}
```

## 路径解析规则

- 相对路径以**流程文件所在目录**为基准（`FlowRunner::setBaseDir`）
- `qrc:` / `http(s):` / 绝对路径（盘符、`/`、`\` 起头）原样使用
- product 来自 qrc 时，流程相对名自动解析为 `qrc:/<name>`；外部绝对流程路径（调试/测试）原样使用