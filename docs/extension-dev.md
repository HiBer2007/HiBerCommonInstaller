# 拓展开发指南（Extension Development）

拓展 = 给安装器添加**自定义步骤类型**、**产品专属 CLI/GUI 参数**、**自定义页面/面板**（v1 已实现步骤与参数；页面/面板能力位预留）及**服务**的独立模块。接口契约逐字见 `ext/include/hci/extension.h` 与 `core/include/hci/extension_registry.h`。

## 1. 接口契约

```cpp
// 能力声明位（可选组合）
struct HciCapabilities {
    bool providesSteps = false;      // 自定义步骤类型
    bool providesPages = false;      // GUI 页面工厂（预留）
    bool providesTuiPanels = false;  // TUI 面板工厂（预留）
    bool providesCliCommands = false;
    bool providesCliArgs = false;    // 产品专属参数（如 --with-editor）
    bool providesServices = false;
};

class IHciExtension {
public:
    virtual const char* id() const = 0;
    virtual const char* version() const = 0;
    virtual HciCapabilities capabilities() const = 0;
    virtual bool init(HostApi& api) = 0;    // 加载后调用；在此注册步骤/参数/服务
    virtual void shutdown() = 0;            // 卸载前调用
};
```

`id()` 建议 `"厂商.产品"` 风格（如 `acme.product-args`、`hci.demo`），version 建议 semver。

## 2. 宿主 API（`HostApi`）

```cpp
EventBus& bus();                       // 事件总线（发布 hci/step 等主题）
ServiceRegistry& services();           // 服务注册表（拓展间调用）
InstallContext& context();             // 当前运行上下文（vars/state/cancel）
const ProductConfig& product() const;  // 产品配置（只读）
// 拓展功能配置：product.json "extensions": { "<本拓展 id>": { ... } }；
// 未配置时返回空对象（见 product-config.md 的 extensions 段）
const nlohmann::json& extensionConfig(const std::string& id) const;
void log(LogLevel lv, const std::string& msg) const;
ExtensionRegistry& registry();         // 步骤/参数/下载后端注册表（宿主注入实例；兜底静态）
bool hasRegistry() const;              // 宿主是否注入了注册表实例
```

**铁律**：注册表必须用宿主注入的实例（`ExtensionLoader(..., &registry)`）。静态兜底 `ExtensionRegistry::instance()` 在各 DLL 各持一份（静态库状态重复坑），仅库模式可用。

## 3. 注册自定义步骤

```cpp
api.registry().registerStep("acme_git_plan",
    [](const nlohmann::json& params, hci::InstallContext& ctx, std::string& error) {
        hci::Vars& v = ctx.vars();
        // ... 读取 params（"editorVariant" 等）、环境，写 ctx.vars() ...
        v.setBool("gitPlanned", true);
        return true;                    // false + error=说明 → 步骤失败
    });
```

流程侧：`{ "id": "gitPlan", "type": "acme_git_plan", "editorVariant": "PortableGit", "when": "..." }`。执行序：内置步骤未命中 → 查注册表 → 仍无 → `unknown step type`。

## 4. 注册参数处理器（产品专属 CLI 参数）

```cpp
api.registry().registerCliArg("--with-editor",
    [](const std::string&, hci::InstallContext& ctx) {
        ctx.vars().setBool("components.editor", true);
        return true;                    // false → 宿主报"extension rejected argument"
    },
    "preselect the editor component");  // 第三参 help：出现在壳 --help 的插件段
```

语义：核心只解析核心参数；未知参数收集后路由给处理器（CLI/GUI 均支持）。处理器在流程运行**前**执行 → 预置变量（如组件勾选），流程的 components 页以"已有变量优先"初始化。

## 4b. 注册下载后端（拓展下载链）

```cpp
api.registry().registerDownloadBackend(
    "myfs",
    []() -> std::shared_ptr<hci::download::IDownloadBackend> {
        return std::make_shared<MyFsBackend>();   // 实现 hci::download::IDownloadBackend
    });
```

流程侧即可链式使用：`{ "type": "download", "url": "...", "chain": ["myfs", "curl", "direct"] }`。内置后端见 [core-api.md](core-api.md) 下载后端节；链语义：首个 `supports()` 且执行成功即止。

## 5. 三种加载方式

`ExtensionLoader` 提供 `modules()`（`std::vector<std::pair<std::string,std::string>>`，已加载拓展的 id/version）供帮助/元数据展示。

### 5a. 静态链接（编译进壳）

```cpp
// 在任一带 HCI 头链接进壳的 TU 中：
HCI_REGISTER_EXTENSION(MyExtension);   // 展开为静态注册（__COUNTER__ 去重）
// 壳启动调用 loader.loadStatic() 加载全部注册项
```

### 5b. DLL 放置（drop-in）

- DLL 放到 `<壳 exe>/extensions/`，宿主启动自动扫描
- **必须导出**（插件导出教训：漏 dllexport = 零导出，GetProcAddress 失败）：

```cpp
extern "C" __declspec(dllexport) hci::IHciExtension* HciGetExtension()
{
    return new MyExtension();
}
```

- 可选同目录 `<名字>.meta.json`（当前未读取字段；为后续扩展预留）
- 目录里的非拓展 DLL 会被探测并跳过（Debug 日志可见）

### 5c. `.hci` 单文件包（ZIP 容器）

包结构（ZIP，libzippp 读取）：

```
<id>-<version>.hci
├── meta.json   { "id": "hci.demo.pkg", "version": "1.0.0",
│                 "dll": "hci_ext_demo.dll", "sha256": "<dll 的 sha256 十六进制>" }
└── <dll>       （可含 assets/ 等附加资源）
```

加载链：扫描 `extensions/*.hci` → 校验 meta.json → 解压到 `%LOCALAPPDATA%/hci/ext-cache/<id>/<version>/`（版本变更换目录）→ **sha256 校验不匹配拒绝加载** → LoadLibrary + `HciGetExtension`。

打包示例（PowerShell；sha256 用 .NET 计算避免 Get-FileHash 在旧宿主缺失）：

```powershell
Compress-Archive -Path "stage/*" -DestinationPath "demo.hci" -Force
```

参考实现：`examples/ext_demo/`（CMake 自定义目标 `hci_ext_demo_pkg` 自动打包）。

## 6. 完整示例（hci_ext_demo，逐字可编译）

```cpp
#include "hci/extension.h"
#include <string>

#ifdef _WIN32
#define HCI_EXT_EXPORT __declspec(dllexport)
#else
#define HCI_EXT_EXPORT __attribute__((visibility("default")))
#endif

namespace {
class DemoExtension : public hci::IHciExtension {
public:
    const char* id() const override { return "hci.demo"; }
    const char* version() const override { return "1.0.0"; }
    hci::HciCapabilities capabilities() const override {
        hci::HciCapabilities c;
        c.providesSteps = true;
        c.providesCliArgs = true;
        return c;
    }
    bool init(hci::HostApi& api) override {
        api.registry().registerStep("demo_ping",
            [](const nlohmann::json& params, hci::InstallContext& ctx, std::string&) {
                ctx.vars().set("demoPing", "true");
                return true;
            });
        api.registry().registerCliArg("--with-editor",
            [](const std::string&, hci::InstallContext& ctx) {
                ctx.vars().setBool("components.editor", true);
                return true;
            });
        return true;
    }
    void shutdown() override {}
};
} // namespace

extern "C" HCI_EXT_EXPORT hci::IHciExtension* HciGetExtension() { return new DemoExtension(); }
```

构建：SHARED 库 + 链 `hci_core hci_ext`（include 经 target 传播）；`set_target_properties(... PREFIX "")` 避免 `lib` 前缀。

## 7. 拓展间通讯

- 总线：`api.bus().subscribe("hci/step", ...)` 监听步骤生命周期；`publish`/`post` 自定义主题
- 服务：`api.services().registerService<T>(impl)` / `api.services().service<T>()`（键 = typeid 名）；另一拓展 `init` 中取用（注意加载顺序——延迟到步骤执行期取更稳）

## 8. 随仓库附带的官方拓展

### hci_git（`ext/git/`，id `hci.git`）— Git 策略通用拓展

任何需要 Git 的安装流程可静态链入或 DLL 托管：

- **CLI 参数**：`--use-system-git` / `--use-bundled-git` / `--install-system-git`（均带 help 文本）
- **步骤 `git_plan`**：系统 vs 内置 vs 安装系统的决策（自动探测 + 模式），写入 `gitUseSystem/gitDownload/gitInstallKind/gitVariant/gitInstallDir/gitPath/gitPlanned`。参数：
  - `editorComponent`（勾选该组件时变体用 PortableGit）、`editorVariant`（自定义变体名）
  - `gitDir`：**内置 Git 安装的相对位置**（相对 installDir，缺省 `tools/git`；其次取产品元数据 `extensions.hci.git.gitDir`）
  - `installerVariant`（install-system 模式的安装器资产匹配串，缺省 `Git-`）
- **步骤 `git_refresh`**：安装系统 Git 后重新探测并写回 `gitPath/gitUseSystem`
- **产品元数据配置**：`"extensions": { "hci.git": { "gitDir": "tools/git" } }`

### hci_winget（`ext/winget/`，id `hci.winget`）— winget 下载/安装后端

注册下载后端 `winget`（Windows）：`winget install --exact --id <package> --silent --accept-package-agreements --accept-source-agreements`。流程用法：`{ "type": "download", "package": "Git.Git", "backend": "winget" }`。

### hci_apt（`ext/apt/`，id `hci.apt`）— apt 下载/安装后端

注册下载后端 `apt`（POSIX）：`apt-get install -y <package>`（需 root；Windows 自动不可用）。流程用法：`{ "type": "download", "package": "git", "backend": "apt" }`，可与 `chain` 组合成跨平台回退策略。

三个官方拓展均为标准 `HCI_REGISTER_EXTENSION` 注册（静态链接进壳即可生效），可作自定义拓展的参照实现。

## 9. 注意事项

- **退出卸载**：加载的 DLL 在进程退出前**不要 FreeLibrary**（卸载导致退出期访问违例已实证）；模块保持加载、交进程回收
- **日志**：`api.log(...)` 走宿主 sink；拓展内勿自行 Init 日志器
- **路径**：字符串一律 UTF-8；Windows 文件访问用 `hci::port::joinPath` 或 `fs::u8path`
- **版本兼容**：`HciGetExtension` 签名与 `IHciExtension` 为 ABI 契约；改接口须同步全部拓展并升 `apiVersion`（meta 预留）