# HiBerCommonInstaller 使用文档

## 快速开始（独立构建）

```powershell
# 前置：vcpkg 工具链 + Qt6（动态 H:/Qt/6.11.1/msvc2022_64 或静态 H:/Qt-static）
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=H:/vcpkg/scripts/buildsystems/vcpkg.cmake `
      -DCMAKE_PREFIX_PATH=H:/Qt/6.11.1/msvc2022_64 -G Ninja
cmake --build build
```

核心链零 Qt（hci_core/hci_flow/hci_ext/hci_cli）；GUI 需 `-DHCI_BUILD_GUI=ON`；TUI 需 `-DHCI_BUILD_TUI=ON`。

### demo 产品

```powershell
build/examples/hci_demo_run                      # CMake 自定义目标：静默装到 demo_out
build/cli/hci_cli.exe --product build/examples/demo/product.json --flow install --silent --path <dir>
build/cli/hci_cli.exe --product ... --flow uninstall --silent --path <dir>
```

### 单文件内嵌分发（product、flows、payload 全进 exe）

```powershell
cmake -S . -B build_qrc -DHCI_BUILD_GUI=ON -DHCI_BUILD_CLI=OFF -DHCI_BUILD_EXAMPLES=OFF `
      "-DHCI_PRODUCT_FILES=product.json=...;install.json=...;deploy/*=...;..."  # alias=path 语法
cmake --build build_qrc --target hci_gui
build_qrc/gui/hci_gui.exe --product qrc:/product.json --flow install --silent --path <dir>
```

产品/流程放 qrc 后：`--product qrc:/product.json`、flow 相对名自动解析为 `qrc:/` 基准。

## 壳用法

| 壳 | 参数 | 说明 |
|---|---|---|
| hci_cli | `--product` `--flow install\|uninstall\|file` `--path` `--silent` `--json` `--extensions <dir>` | 退出码 0/1/2；`--json` 输出 `=====JSON-BEGIN=====` 块；未知参数交拓展 cliArgs 处理器 |
| hci_gui | 同 CLI（另有 `--gui`），支持 qrc 内嵌 | 控制台子系统：终端启动保终端+UTF-8，双击释放；`--silent` 无头；`HCI_GUI_AUTOPILOT=1` 测试钩子 |
| hci_tui | 同 CLI+`--tui` | 要求真实终端；`HCI_TUI_AUTOPILOT=1` 测试钩子；内置 wcwidth 宽字符布局 |

## 产品配置 product.json

```json
{ "productName", "company", "orgName", "version", "defaultMode": "gui|cli|tui",
  "defaultInstallPath", "banner": {"font":"slant"},
  "components": [{"id","label","exe","shortcutName","required","defaultChecked"}],
  "shortcuts": [{"kind":"desktop|startmenu","name","target","args"}],
  "installConf": {"fileName","header","template":{key:"{var}"}},
  "flows": {"install":"install.json","uninstall":"uninstall.json"},
  "payload": {"source":"dir:...|zip:...|qrc:/deploy","skip":["*.dmp"]},
  "uninstall": {"registryKey","displayName"} }
```

## 流程脚本（JSON 步骤机 + Lua）

步骤字段：`id/type|ui/next/when/onFail/params`。
- ui 步骤：`welcome/license(source)/path/components/option(choices)/confirm/input/finish(message,launch)`
- type 步骤：`extract(source,target,skip) / copy / clean(target) / download(asset|url,variant,dest) / run(program,args,waitMs) / shortcut(kind,name,target) / template(file,template) / registry(key,action) / script(script)`
- `when`：Lua 表达式（**`==`/`~=`，非 `!=`**；带点键名用 `vars['components.editor']`）；false=跳过本步
- 变量：`${var}` 模板插值；预置 `productName/company/productVersion/installDir/tempDir/exeDir`
- 拓展自定义步骤：TYPE 未命中内置时查 ExtensionRegistry

## 拓展（三种加载）

```cpp
class MyExt : public hci::IHciExtension {
    const char* id() const override;  const char* version() const override;
    hci::HciCapabilities capabilities() const override;   // providesSteps/CliArgs/...
    bool init(hci::HostApi& api) override {
        api.registry().registerStep("my_type", handler);   // 自定义步骤
        api.registry().registerCliArg("--my-arg", handler); // 产品专属参数
        return true;
    }
    void shutdown() override {}
};
extern "C" __declspec(dllexport) hci::IHciExtension* HciGetExtension(); // 必须 dllexport
```

1. **静态链接**：`HCI_REGISTER_EXTENSION(MyExt)` + 链接进壳
2. **DLL 放置**：`extensions/*.dll`，宿主自动扫描 exeDir/extensions
3. **.hci 包**：ZIP 容器（`meta.json`{id,version,dll,sha256} + dll + assets），解压到 `%LOCALAPPDATA%/hci/ext-cache/<id>/<version>/` 校验 sha256 后加载

## 注意事项

- **静态库跨 DLL 状态**：ExtensionRegistry 必须宿主注入（`ExtensionLoader(..., &registry)` + `FlowRunner::setRegistry(&registry)`），勿依赖其内部静态实例（各 DLL 一份）
- **退出勿 FreeLibrary**：加载的拓展 DLL 保持到进程退出（卸载会引起退出期访问违例）
- **qrc 资源**：走 `QResource` 直读（QFile 打开 qrc 失败）；AHQUTORCC 嵌入的资源需 `Q_INIT_RESOURCE(hci_product)`
- **TUI 宽字符**：进度条方块用字节串 `"\xE2\x96\x88"`（勿写多字符字面量）
- **Lua 脚本**：vcpkg lua port 现为 5.5（API 兼容）；`print` 直接输出（CLI/TUI 可见）

## 相关文档

- [README.md](README.md) — 架构总览与里程碑
- [DESIGN.md](DESIGN.md) — 权威设计文档
- [docs/](docs/README.md) — 详细文档库（产品配置/流程脚本/API/拓展/三壳/构建/踩坑）