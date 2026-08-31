# 踩坑清单（Pitfalls）

开发/集成 HiBerCommonInstaller 过程中实证并修复过的坑。现象 → 根因 → 修法。

## 编译期

### 嵌套 namespace 同名阴影（`hci::hci`）
- 现象：`hci::ProductConfig` 报 `不是 "hci::hci" 的成员`
- 根因：命名空间 hci 内再声明 `namespace hci { ... }`（想放个自由函数）→ 内部 `hci` 遮蔽外层，`hci::X` 解析成 `hci::hci::X`
- 修法：把函数声明放到外层 hci 块**开头**，勿在尾部嵌套同名命名空间

### 多字符字面量截断（TUI 进度条乱码）
- 现象：TUI 进度条方块变 `?`（UTF-8 解码出 168 个 U+FFFD）
- 根因：`bar.append(fill, '\xE2\x96\x88')` —— `'\xE2\x96\x88'` 是多字符字面量，MSVC 截断为单字节 0x88
- 修法：字节串常量 `static const std::string kFull = "\xE2\x96\x88";` 循环拼接

### `Lua::Lua` target 不存在
- 现象：`target_link_libraries(... Lua::Lua)` 报 target not found（CMake 4.x）
- 修法：用 `find_package(Lua)` 的 `${LUA_INCLUDE_DIR}` + `${LUA_LIBRARIES}`（core/CMakeLists.txt 已如此）

### 全局 AUTOMOC 报警
- 现象：零 Qt 的 core/flow/ext 编译时 CMake 报 `AUTOGEN: No valid Qt version found ... AUTOMOC disabled`
- 修法：不设全局 `CMAKE_AUTOMOC ON`；GUI 壳按 target 开

### 头文件成员未声明（QProgressBar* 等）
- 现象：头文件里 `QProgressBar*`/`QTextEdit*` 成员报语法错误
- 修法：成员指针类型所在头必须 include（或前置声明）——依赖 QDialog 传递包含不可靠

### libzippp API 随版本大改
- 现象：`libzippp::ZipArchive*`/`readContent(char*,int)` 编译错误（vcpkg 当前 7.x）
- 当前 API：`ZipArchive(path).open(ReadOnly)`、`getEntries()`、`getEntry(name)` 返回**按值** `ZipEntry`（`isNull()` 判空）、`readContent(std::ostream&)`、`readAsText()`
- 修法：写代码前先看 `build/vcpkg_installed/.../include/libzippp/libzippp.h` 实测（见 core/src/payload.cpp、ext/src/extension_loader.cpp）

## 运行期（加载/流程）

### 静态库跨 DLL 状态重复（ExtensionRegistry）
- 现象：CLI/GUI 里 `--with-editor` 报 unknown option，尽管 DLL 已加载并注册
- 根因：`ExtensionRegistry::instance()` 的函数级静态在 exe 与拓展 DLL（各链一份 hci_core 静态库）中各自独立
- 修法：宿主显式注入——`ExtensionLoader(..., &registry)` + `FlowRunner::setRegistry(&registry)`；拓展经 `HostApi::registry()` 注册到注入实例

### 退出期访问违例（FreeLibrary）
- 现象：流程跑完 exit 却 0xC0000005（`-1073741819`）
- 根因：shutdown 时 FreeLibrary 卸载拓展 DLL，注册表/CRT 状态仍在引用其代码段
- 修法：DLL 加载后**不卸载**（`shared_ptr` deleter 只 delete 对象），进程退出回收

### `when` 条件 false 被当失败
- 现象：`[ERROR] step 'X' failed: `（空错误）——条件不满足却中止
- 根因：v1 把 `evalWhen` 返回值直接当成败
- 修法：`evalWhen` 语义 = 条件错误才 false；`shouldRun=false` 为跳过（发 `hci/step` skipped 事件）

### Lua 语法差异
- `!=` 不存在：用 `~=`（`vars.gitPlanned ~= 'true'`）
- 带点键名：`vars['components.editor']`（`vars.components.editor` 索引 nil）
- 现象若为 `condition eval failed: <lua 错误串>`，先查表达式语法

### runProcess 整行多余引号
- 现象：所有 `run`/`git --version` 类步骤"启动失败"
- 根因：`CreateProcessW` 的 lpCommandLine 被外层 `L"\"" + cmdLine + L"\""` 再包一层 → 整行被当可执行名
- 修法：每个参数单独按需加引号，整行不再包装（见 core/src/exec.cpp）

## qrc / 单文件分发

### QFile 打开 qrc 资源失败（诡异：QDir 能列、QFile 不行）
- 现象：`QFile(":/product.json").open` 报系统找不到文件，而同进程 `QDir(":/").entryList()` 可见
- 修法：统一 `QResource` 直读（`gui::readResource`，见 `resource_utils.{h,cpp}`）；流程/许可读取走 `FlowRunner::setResourceReader`

### AUTORCC 资源未注册
- 现象：qrc 编译进二进制（字符串表在 exe 里）但运行时访问失败
- 根因：AUTORCC 只负责编译链接；**资源注册要 `Q_INIT_RESOURCE(hci_product)`**（main.cpp，`HCI_EMBED_PRODUCT` 宏控制）

### 改了 json 不生效
- 根因：`HCI_PRODUCT_FILES` 内容在 **configure 期**写入 qrc
- 修法：改 product.json/流程 json 后重跑 configure（重新生成 qrc；宿主侧即重跑其 configure 命令）

### 路径形式
- `qrc:/x` 与 `:/x`：资源引擎统一按 `:/x` 处理（内部自行归一化）；外部 API 两种都接受
- `qrc:` 流程相对名解析：`qrc:/<name>`（见 gui main）

## 构建/集成

### 主仓库改了 HCI 代码不生效
- 根因：主仓库 `build_nsum_installer` 编译的是 **submodule checkout**（`modules/HiBerCommonInstaller`），不是 HCI 工作树
- 修法：HCI 仓库提交 push → `git submodule update --remote modules/HiBerCommonInstaller` → 重建

### 静态 Qt 缺失平台插件
- 检查：exe 目录无 `platforms/`、无 `Qt*.dll` 即静态成功；21.8MB 量级合理
- 未设 `HCI_QT_STATIC` 时 windeployqt 会部署动态 Qt —— 发布链必须显式 `-DHCI_QT_STATIC=ON`

### vcpkg 静态 Qt port 已移除
- `vcpkg install qt6[...]` 报 `qt6 does not exist`（registry 已删除 Qt ports）
- 静态 Qt 一律源码构建（`-static -release -schannel -no-opengl`，见 build-guide.md）；`-schannel` 免 OpenSSL/perl

### build_nsum_installer 旧缓存路径
- 工作区迁移后 configure 报 `CMakeCache.txt ... different than the directory ...`
- 修法：删 `build_nsum_installer/` 整目录重配

### cmd 里塞 PowerShell 管道
- 现象：`cmd /c "... && ... | Select-Object"` 报 `'Select-Object' 不是内部或外部命令`
- 修法：cmd 串内不带 pwsh 语法；管道放在 pwsh 侧对 `cmd /c` 结果处理

## 冒烟纪律

- GUI 冒烟：启动 → 3-4s 确认存活（窗口句柄非 0）→ CloseMainWindow → 期待 exit 0（取消语义 1 也正常）；关闭崩溃 = 布局失配/资源问题的典型信号
- TUI/CLI 判据：原始字节 UTF-8 解码 0 个 U+FFFD + 关键子串命中（GBK 终端显示 `����` 是显示层假象）
- 快捷方式冒烟会真实写桌面/开始菜单 —— 验后清理