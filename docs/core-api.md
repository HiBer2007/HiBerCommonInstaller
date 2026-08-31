# hci_core — 公共 API 逐字参考

零 Qt 核心库（C++17 + nlohmann-json + Lua + libzippp + cpr）。以下签名**逐字摘自** `core/include/hci/*.h`。

## Vars（`hci/vars.h`）

```cpp
class Vars {
public:
    void set(const std::string& name, const std::string& value);
    void setBool(const std::string& name, bool value);
    void setInt(const std::string& name, long long value);

    std::string get(const std::string& name) const;   // 未知键 → 空串
    bool has(const std::string& name) const;
    bool getBool(const std::string& name, bool defaultValue = false) const; // "true/1/yes/on"

    void remove(const std::string& name);
    void clear();

    // 递归插值 {name}（最多 8 轮，环安全；未知键 → 空串）
    std::string interpolate(const std::string& text) const;

    const std::map<std::string, std::string>& all() const;
};
```

## 总线与服务（`hci/bus.h`）

```cpp
using EventHandler = std::function<void(const nlohmann::json& payload)>;
using HandlerId = unsigned long long;

class EventBus {          // 不可拷贝
    HandlerId subscribe(const std::string& topic, EventHandler handler);
    void unsubscribe(HandlerId id);
    void unsubscribeAll(const std::string& topic);
    void publish(const std::string& topic, const nlohmann::json& payload = nullptr); // 同步、按注册序
    void post(const std::string& topic, const nlohmann::json& payload = nullptr);    // 排队
    void drain();                                                                     // 投递积压
};

class ServiceRegistry {
    void registerService(const std::string& ifaceName, void* service);
    void unregisterService(const std::string& ifaceName);
    void* findService(const std::string& ifaceName) const;
    template <typename T> void registerService(T* service);   // 键 = typeid(T).name()
    template <typename T> T* service() const;                 // 按类型取
    std::vector<std::string> serviceNames() const;
};
```

内置事件主题：`hci/step`（payload `{"id","state"}`，state = `start|done|skipped`）。

## 安装上下文（`hci/context.h`）

```cpp
enum class StepFailure { Abort, Ignore, Continue };  // Continue 为 Ignore 别名(v1)

class InstallContext {
    Vars& vars();  const Vars& vars() const;
    void cancel();                                 // 置取消标记（流程逐步骤检查）
    bool cancelled() const;
    void resetCancelled();
    StepFailure failurePolicy() const;
    void setFailurePolicy(StepFailure p);
    nlohmann::json& state();                       // 自由状态（拓展/步骤共享）
    const nlohmann::json& state() const;
};
```

## 执行库（`hci/exec.h`，命名空间 `hci::exec`）

```cpp
// 文件系统
bool mkdirs(const std::string& path);
bool copyFile(const std::string& src, const std::string& dst, std::string* error = nullptr);
bool copyTree(const std::string& srcRoot, const std::string& dstRoot,
              const std::vector<std::string>& skipPatterns = {},
              std::function<bool(const std::string& rel)> onFile = nullptr,
              std::string* error = nullptr);
bool cleanDir(const std::string& path, std::string* error = nullptr);   // 清内容，保留目录

// 解压（libzippp 主后端；7za 用于 .7z/.7z.exe）
bool extractZip(const std::string& zipPath, const std::string& destDir,
                const std::string& sevenZipExe = "", std::string* error = nullptr);

// 进程 / 下载
struct ProcessResult {
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;   // stdout+stderr 合并（UTF-8 尽力）
};
bool runProcess(const std::vector<std::string>& command, int waitMs,
                ProcessResult& result);   // waitMs<=0 = 无限；超时杀进程

using DownloadProgress = std::function<void(long long received, long long total)>;
bool downloadFile(const std::string& url, const std::string& destPath,
                  DownloadProgress progress = nullptr, std::string* error = nullptr); // cpr/libcurl

// 文本模板（{name} 插值）
std::string renderTemplate(const std::string& text, const Vars& vars);

// Windows 专属（POSIX v1 返回 false）
enum class ShortcutKind { Desktop, StartMenu };
bool createShortcut(ShortcutKind kind, const std::string& name,     // name 可含 '/' 子目录
                    const std::string& targetPath, const std::string& workDir,
                    const std::string& args = "", std::string* error = nullptr);
bool registryWriteString(const std::string& key, const std::string& valueName,
                         const std::string& value);                  // HKCU
bool registryDeleteKey(const std::string& key);
```

## payload 源（`hci/payload.h`）

```cpp
struct DeploySpec {
    std::string source;               // "dir:<path>" | "zip:<path>" | "qrc:/prefix"
    std::vector<std::string> skip;    // 通配 * ?
};
struct DeployEntry {
    std::string relPath;              // '/' 分隔
    bool isDir = false;
    long long size = 0;
};

class IDeploySource {
    virtual bool open(std::string& error) = 0;
    virtual bool enumerate(std::vector<DeployEntry>& entries, std::string& error) = 0;
    virtual bool readFile(const std::string& relPath, std::vector<char>& out,
                          std::string& error) = 0;
    const std::string& sourceKind() const;
};

std::shared_ptr<IDeploySource> makeDirSource(const std::string& rootPath);
std::shared_ptr<IDeploySource> makeZipSource(const std::string& zipPath);
std::shared_ptr<IDeploySource> makeDeploySource(const DeploySpec& spec, std::string& error);

// 扩展点：注册自定义源 kind（Q t 桥注册 "qrc"）
using DeploySourceFactory = std::function<std::shared_ptr<IDeploySource>(
    const std::string& rest, std::string& error)>;
void registerDeploySourceFactory(const std::string& kind, DeploySourceFactory factory);

bool wildcardMatch(const std::string& pattern, const std::string& text); // * ?
```

## 脚本引擎（`hci/script.h`）

```cpp
class IScriptEngine {
    virtual bool eval(const std::string& expression, const Vars& vars,
                      std::string& out) = 0;   // 表达式求值（渲染为字符串）
    virtual bool run(const std::string& code, const Vars& vars,
                     std::string& err) = 0;    // 脚本块执行
    virtual const char* name() const = 0;
};

class LuaEngine : public IScriptEngine { ... };   // 内嵌 Lua（vcpkg lua port）
std::shared_ptr<IScriptEngine> createLuaEngine();
```

## 日志（`hci/log.h`）

```cpp
enum class LogLevel { Trace, Debug, Info, Warn, Error };

class ILogSink {
    virtual void write(LogLevel level, const std::string& message) = 0;
    virtual const char* sinkName() const = 0;
};
class NullSink : public ILogSink { ... };
class ConsoleSink : public ILogSink { explicit ConsoleSink(LogLevel minLevel = LogLevel::Info); };
class FileSink   : public ILogSink { FileSink(const std::string& path, LogLevel minLevel = LogLevel::Info); };

class Log {                                     // 门面：多 sink、线程安全
    static Log& instance();
    void addSink(std::shared_ptr<ILogSink> sink);
    void removeSink(ILogSink* sink);
    void setLevel(LogLevel level);
    LogLevel level() const;
    void write(LogLevel level, const std::string& message);
    static void Trace/Debug/Info/Warn/Error(const std::string& m);
};

// 变参格式化（C++17）：hci::fmt("a={} b={}", 1, "x")；参数耗尽后原样保留 "{}"
template <typename... Args>
std::string fmt(const std::string& pattern, Args&&... args);
```

## 可移植层（`hci/port.h`，命名空间 `hci::port`）

```cpp
bool hasConsole();                                  // 交互终端判定（Win: GetConsoleProcessList>0）
void holdOrReleaseConsole();                        // 终端启动→保留+UTF-8；双击→FreeConsole
void setUtf8Console(bool virtualTerminal = true);   // CP_UTF8 + VT + _O_BINARY + 无缓冲

std::string exeDir(); std::string tempDir(); std::string localAppData(); std::string currentDir();
std::string joinPath(const std::string& a, const std::string& b);
std::string joinPath(const std::vector<std::string>& parts);
std::string normalizeSlashes(const std::string& p);

std::string getEnv(const std::string& name, const std::string& fallback = "");
void setEnv(const std::string& name, const std::string& value);
```

## 入口层（`hci/entry.h`，命名空间 `hci::entry`）

```cpp
struct EntryOptions {
    std::string mode;            // "gui" | "tui" | "cli" | ""
    std::string productJson;
    std::string flow;            // "install" | "uninstall" | 文件路径
    bool silent = false;
    bool jsonOut = false;
    std::string installPath;
    std::vector<std::string> extensionArgs;   // 交拓展 cliArgs 处理器
    std::string resolveMode(const ProductConfig& product) const;  // 显式 > product.defaultMode
};

// 品牌横幅：<产品名> ASCII 拼接字（font: slant|standard，超长自动降档）+ 强制 Powered by 行
std::string renderBanner(const std::string& productName, const std::string& font = "slant");

// 库模式安装（实现位于 hci_flow）：返回进程退出码（0 成功 / 1 失败 / 2 用法错误）
int runInstall(const ProductConfig& product, const std::string& flowPath,
               const EntryOptions& options);
```

## 拓展注册表（`hci/extension_registry.h`）

```cpp
using StepHandler = std::function<bool(const nlohmann::json& params,
                                       InstallContext& ctx, std::string& error)>;
using CliArgHandler = std::function<bool(const std::string& arg, InstallContext& ctx)>;

class ExtensionRegistry {
    static ExtensionRegistry& instance();          // 兜底（见坑：宿主应注入实例）
    void registerStep(const std::string& type, StepHandler handler);
    bool runStep(const std::string& type, const nlohmann::json& params,
                 InstallContext& ctx, std::string& error) const;   // 未注册返回 false
    void registerCliArg(const std::string& arg, CliArgHandler handler);
    bool handleCliArg(const std::string& arg, InstallContext& ctx) const;
    bool hasCliArg(const std::string& arg) const;
    std::vector<std::string> cliArgs() const;
    void clear();
};
```

## 产品配置（`hci/product.h`）

见 [product-config.md](product-config.md)（字段逐字）。

## 常用拼装示例（库模式）

```cpp
hci::ProductConfig product = hci::ProductConfig::loadFile("product.json");
hci::entry::EntryOptions opts;
opts.silent = true; opts.installPath = "D:/App";
int rc = hci::entry::runInstall(product, "install.json", opts);   // 0/1/2
```