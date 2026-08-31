# 构建指南（Build Guide）

## 依赖

| 组件 | 来源 | 说明 |
|---|---|---|
| CMake ≥ 3.20 + Ninja | VS18 BuildTools 自带（本机无 PATH cmake） | 路径：`...\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| vcpkg | H:/vcpkg | manifest 模式；`vcpkg.json` = nlohmann-json, lua, libzippp, cpr（→ libzip/zlib/bzip2/curl/openssl+wil） |
| Qt6 | 动态 `H:/Qt/6.11.1/msvc2022_64` / 静态 `H:/Qt-static/6.11.1/msvc2022_64` | 仅 GUI 壳需要 |
| HiBerGUILibCPP | git submodule（嵌套） | GUI 壳复用 HiBerGUILibrary；`if(NOT TARGET HiBerGUILibrary)` 守卫 |

## 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `HCI_BUILD_CLI` | ON | hci_cli（零 Qt） |
| `HCI_BUILD_GUI` | OFF | hci_gui（需 Qt6 + HiBerGUILibCPP） |
| `HCI_BUILD_TUI` | OFF | hci_tui（零 Qt） |
| `HCI_BUILD_EXAMPLES` | ON | demo 产品 + ext_demo 拓展 |
| `HCI_CORE_SHARED` | OFF | hci_core 共享库（WINDOWS_EXPORT_ALL_SYMBOLS） |
| `HCI_QT_STATIC` | OFF | 静态 Qt 模式：`qt_import_plugins(QWindowsIntegrationPlugin)`，跳过 windeployqt |
| `HCI_PRODUCT_FILES` | 空 | GUI qrc 内嵌清单（`alias=path`，见下） |
| `HIBERGUI_BUILD_EXAMPLES` | ON* | 子模块示例（宿主集成时关） |

## 动态 Qt 构建（开发）

```powershell
$cmake = "C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cmd /c "`"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`" && `"$cmake`" -S K:/HiBerCommonInstaller -B build -DCMAKE_TOOLCHAIN_FILE=H:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=H:/Qt/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Debug -G Ninja -DCMAKE_MAKE_PROGRAM=<VS ninja> -DHCI_BUILD_GUI=ON -DHCI_BUILD_TUI=ON -DHIBERGUI_BUILD_EXAMPLES=OFF && `"$cmake`" --build build"
```

产物：`build/cli/hci_cli.exe`、`build/gui/hci_gui.exe`（windeployqt 自动部署 Qt DLL）、`build/tui/hci_tui.exe`；vcpkg 运行时 DLL 自动拷到 exe 旁。

## 静态 Qt 构建（发布单文件）

静态 Qt 需源码构建（vcpkg 已移除 qt6 port）：

```bat
:: qtbase 静态构建（本机已就绪 H:/Qt-static/6.11.1/msvc2022_64；命令留档）
call vcvars64.bat
cd /d H:\Qt\6.11.1\Src\qtbase && mkdir build-static && cd build-static
..\configure.bat -static -release -opensource -confirm-license ^
  -prefix H:/Qt-static/6.11.1/msvc2022_64 -platform win32-msvc ^
  -nomake examples -nomake tests -no-opengl -schannel
cmake --build . --parallel && cmake --install .
```

要点：`-schannel` 规避 OpenSSL/perl 依赖；`-no-opengl` 提速；install 后 `lib/Qt6Config.cmake` 即 `CMAKE_PREFIX_PATH`。

应用侧：`-DHCI_BUILD_GUI=ON -DHCI_QT_STATIC=ON -DCMAKE_PREFIX_PATH=H:/Qt-static/6.11.1/msvc2022_64 -DCMAKE_BUILD_TYPE=Release`；产物单一 `hci_gui.exe`（实测约 21.8MB，无 Qt*.dll、无 platforms/）。

## 单文件内嵌分发（HCI_PRODUCT_FILES）

```powershell
$files = "product.json=K:/.../product.json;install.json=...;uninstall.json=...;LICENSE.txt=...;deploy/bin/x.txt=K:/.../payload/bin/x.txt"
cmake -S . -B build_qrc ... -DHCI_PRODUCT_FILES="$files"
```

- 条目 `alias=绝对路径`；alias 为 qrc 内路径（`deploy/...` 前缀即 payload 的 `qrc:/deploy`）
- qrc 资源注册在 main.cpp（`Q_INIT_RESOURCE(hci_product)`，宏 `HCI_EMBED_PRODUCT`）
- **改内嵌内容后必须重跑 configure**（qrc 在 configure 期生成）
- 产品内嵌时运行：`hci_gui --product qrc:/product.json --flow install [--silent] [--path <dir>]`

## 主仓库集成（installer-static 预设）

主仓库 `CMakePresets.json` 的 `installer-static`：`INSTALLER_ONLY_BUILD=ON + HCI_QT_STATIC=ON + CMAKE_PREFIX_PATH=H:/Qt-static + DEPLOY_SOURCE=build/deploy`；根 CMakeLists INSTALLER_ONLY 分支自动构建 `HCI_PRODUCT_FILES`（deploy 全量 + nsum_installer 文件）。详见 [nsum-integration.md](nsum-integration.md)。

```powershell
cmd /c "vcvars64.bat && cmake --preset installer-static && cmake --build build_installer"
# 产物：build_installer/modules/HiBerCommonInstaller/gui/hci_gui.exe（+ extensions/nsum_args_ext.dll）
```

## 故障排查

| 症状 | 原因 / 修法 |
|---|---|
| `qt6 does not exist`（vcpkg） | vcpkg 已移除 Qt ports；静态 Qt 走源码构建（上文） |
| SSL error 35 下载失败 | 本机 vcpkg/curl 直连 github 握手失败；用 `Invoke-WebRequest` 预下载到 `H:/vcpkg/downloads/<期望文件名>`（SHA512 自动校验） |
| `Lua::Lua` target 不存在 | CMake 4.x FindLua 只给变量；用 `${LUA_INCLUDE_DIR}` + `${LUA_LIBRARIES}`（core/CMakeLists.txt 已如此） |
| GUI 构建提示 AUTOMOC 无 Qt | core 链不设全局 AUTOMOC（按 target 开） |
| qrc 内容改了不生效 | 未重新 configure —— qrc 生成于 configure 期 |
| 静态 Qt 缺平台插件 | 未设 `HCI_QT_STATIC`/`HCI_QT_STATIC` 时需 `qt_import_plugins`；检查 exe 目录无 platforms/ 即静态成功 |
| build_installer 旧缓存路径错误 | 工作区迁移后删 `build_installer/` 整目录重配（CMakeCache 绑旧机器路径） |
| 主仓库改了 HCI 代码没生效 | 主仓库构建的是 submodule checkout：HCI 提交 push 后 `git submodule update --remote modules/HiBerCommonInstaller` 再构建 |