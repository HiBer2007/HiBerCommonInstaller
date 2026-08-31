# CLI 壳（hci_cli）参考

控制台子系统、零 Qt 的安装壳。核心参数 + 拓展参数处理器；JSON 协议沿 NSUM CLI 公约（`=====JSON-BEGIN=====` 块、日志走 stderr）。

## 用法

```
hci_cli [options]
  --gui | --tui | --cli     shell mode（默认: product defaultMode；gui/tui 未编译时明确报错）
  --silent, -s              non-interactive（默认答案）
  --path <dir>              install path override
  --product <file.json>     product config（默认 ./product.json）
  --flow <id|file.json>     flow: install | uninstall | 文件路径
  --json                    JSON protocol blocks on stdout
  --verbose                 debug logging
  --extensions <dir>        load extensions from directory（默认 <exe>/extensions）
  --help, -h / --version, -v
```

未知参数 → 收集为 extensionArgs → 启动后路由给拓展 `cliArgs` 处理器；**无处理器认领 → "Error: unknown option" + exit 2**。

## 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 失败 / 用户取消（流程 abort） |
| 2 | 用法错误（未知参数、产品/流程缺失、gui/tui 壳未构建、拓展拒绝参数） |

## JSON 协议（--json）

- stdout 只输出标记块；人类日志走 stderr；banner 走 stderr
- 块格式：

```
=====JSON-BEGIN=====
{...}
=====JSON-END=====
```

- 事件（`category: "hci"`）：

| event | payload |
|---|---|
| `progress` | `{step, percent, detail}` |
| `message` | `{error: bool, text}` |
| `finish` | `{success: bool, message, launch}` |

## 交互语义（CliUi）

- **静默（--silent）**：所有交互步骤取默认值——license 接受、path 用 `--path`/默认、components 用已预置/默认勾选、option=0、confirm=defaultYes、input 必填无默认 → 失败、finish 直接输出
- **交互**：行式提示（stdin UTF-8）：
  - license：展示文本（前 40 行）→ `Accept the license? [y/N]`
  - path：`Install directory [default]: `（回车默认）
  - components：逐项 `[x] label [y/N]`
  - option：编号列表 `<n>`
  - confirm：`[Y/n]`
  - input：`prompt: `（必填时会重试）
  - 进度：`[step <id>] <pct>% <detail>` 行
- EOF：confirm/select 返回默认；必填 input EOF → 取消（false → 流程中止 exit 1）

## 启动流程（main 顺序）

1. 参数解析（help/version 短路）→ 模式校验
2. 日志：有终端 → ConsoleSink（verbose 升 Debug 级）；无终端（CreateProcessA 场景）→ `%TEMP%/hci_cli.log` FileSink
3. `port::setUtf8Console(true)`（有终端时）
4. 产品加载 → **banner（stdout；--json 时 stderr）**
5. 流程解析（product.flows 相对名按产品目录解析；`--flow` 具体路径优先）
6. 上下文（`--path` 预置 installDir）+ Lua 引擎 + EventBus + ServiceRegistry + **宿主注入 ExtensionRegistry**
7. `loadStatic()` + `loadDirectory(exeDir/extensions 或 --extensions)`
8. **extensionArgs 路由**（未认领 → exit 2）
9. FlowRunner（注入 bus/registry/baseDir=流程目录）→ `run` → 退出码

## 与主仓库 NSUM CLI 的关系

hci_cli 是独立网关（安装器 CLI），与 NeoServerUpdateModpack 主程序的 `info/flow/exec` CLI 无关；两者共享 `=====JSON-BEGIN=====` 标记块惯例（脚本解析同一套习惯）。

## 典型脚本

```powershell
hci_cli --product product.json --flow install --silent --path "D:\App" --with-editor
hci_cli --product product.json --flow uninstall --silent --path "D:\App" --json
hci_cli --product product.json --flow install --extensions .\ext
```