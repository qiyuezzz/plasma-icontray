# IconTray 图标托盘

独立的 KDE Plasma 系统托盘组件，基于原生托盘增加持久排序、手动分组、统一尺寸和逐图标外观调整。
组件 ID：`org.kde.plasma.icontray`。不覆盖原生托盘，不自动修改面板布局。

## 安装与卸载快捷命令

```sh
# 编译、测试并安装到当前用户；刷新面板后立即可搜索组件
./install.sh --restart

# 仅查看卸载范围，不删除文件
./uninstall.sh --dry-run

# 从面板移除 IconTray 后，卸载并刷新桌面
./uninstall.sh --restart
```

首次在其他设备安装前，请先安装下文列出的构建依赖。`--restart` 会让面板短暂重载，应用窗口保持运行；不加此选项则在重新登录后生效。

安装后还会生成独立卸载命令，即使删除项目源码或 `build` 目录仍可使用：

```sh
~/.local/bin/icontray-uninstall --dry-run
~/.local/bin/icontray-uninstall --restart
```

## 功能

- 在“排序与外观”设置页拖动手柄或使用上下按钮排序；点击上下按钮后列表跟随该项目，方便在同一鼠标位置连续点击；到达列表边缘后不再额外留白。
- 应用图标、系统图标各有一个设置列表，通过顶部标签切换；可用分组下拉框更改归属，也可关闭面板中的分组排列。
- 应用退出后保留排序及外观设置，重新启动时按稳定的托盘 ID 恢复。
- 固定图标尺寸（12–64 逻辑像素），默认保持单行/单列；支持水平和垂直面板。
- 逐项目缩放（50%–150%），补偿图标内部留白。统一方形图标区域和原生间距设置。
- 为第三方应用托盘指定图标主题名称或 `file:///绝对路径.svg`。
- 复用原生折叠区、提示、菜单、点击、滚轮及网络/音量/蓝牙等小部件的接入逻辑。
- 新增设置页提供简体中文和英文。原生设置使用系统已安装的 KDE 翻译。

## 兼容范围

本版基于 Plasma **6.8** 分支，已在 **Plasma 6.8.1 / Qt 6.12 / KDE Frameworks 6.31** 上编译及进行隔离加载测试。
最低构建依赖为 **Plasma 6.8、Qt 6.11、KDE Frameworks 6.30、CMake 3.24、支持 C++23 的编译器、Python 3.9+**。
小部件 API 元数据使用与原生托盘一致的 `6.0`；实际构建依赖仍由 CMake 检查为 Plasma 6.8。
不支持 Plasma 5、6.6、6.7；更新版本尚需验证。跨设备分发使用源码包，在目标设备本地编译，不能保证一个 `.so` 在不同发行版或 Qt 版本间通用。

组件包含原生 C++ 后端，不能通过“获取新组件”或 `kpackagetool6` 直接当作纯 QML 包安装。
源码和 DBus 菜单实现已包含在仓库中，构建不联网下载代码。

## 安装依赖

Arch Linux / 满足上述版本的衍生发行版：

```sh
sudo pacman -S --needed base-devel cmake extra-cmake-modules gettext \
  qt6-base qt6-declarative libplasma plasma-workspace \
  kconfig kcoreaddons kiconthemes ki18n kio kjobwidgets kservice \
  kitemmodels kwindowsystem kxmlgui kstatusnotifieritem \
  kirigami kcmutils kdeclarative ksvg
```

其他发行版安装对应开发包：Qt Core/DBus/Gui/Quick/Widgets/Test 及 Qt Gui 私有头文件、Extra CMake Modules、libplasma/PlasmaQuick，以及上面的 KDE Frameworks 库。
运行时还需要 Plasma、Kirigami、KCMUtils、KQuickControls、KItemModels、KSvg 的 QML 模块。
若发行版尚未提供所需版本，本版会在 CMake 检查时停止；需要针对旧版本单独移植。

## 当前用户安装（推荐）

在源码目录执行：

```sh
./install.sh
```

脚本会编译、运行测试，然后安装到 `~/.local`。重复执行可以更新已有安装。整个过程不需要管理员权限。
用户插件目录为 `~/.local/lib/qt6/plugins`，并生成：

```text
~/.config/plasma-workspace/env/icontray.sh
```

该文件为下次 Plasma 会话加入插件与翻译数据搜索路径（使用自定义 `XDG_CONFIG_HOME` 时跟随该路径）。
不加 `--restart` 时，**安装后注销并重新登录**；也可使用 `./install.sh --restart` 立即刷新。然后右键面板 → 进入编辑模式 → 添加组件 → 搜索 **IconTray**。
在 IconTray 的设置中打开“排序与外观”，按需要调整分组、顺序和缩放。
点击“应用”后生效；图标间距、自动随面板缩放和显示/隐藏规则在“常规”页中设置。

确认 IconTray 可正常使用后，可在面板编辑模式中移除原生系统托盘。
并排测试时两者会显示相同应用图标；通知等系统小部件也可能同时加载，因此建议最终只保留一个托盘。
安装脚本不会替用户移除或替换原生托盘。

## 系统级安装

```sh
./install.sh --system --restart
```

编译和测试仍以当前用户运行，只在写入 `/usr` 时调用 `sudo`。
插件路径由 KDE/Qt 根据发行版确定，支持发行版使用的多架构目录。
不加 `--restart` 时，安装后重新登录并添加 IconTray。无需生成用户会话环境文件。系统级独立卸载入口为 `/usr/bin/icontray-uninstall`，删除文件时会调用 `sudo`。
请勿同时保留用户级和系统级的不同版本。

## 在其他设备上安装

生成便于复制的源码包：

```sh
./package.sh
```

输出 `dist/icontray-0.1.0.tar.gz` 及 SHA256 校验文件。包内包含所有组件源代码、菜单代码、中文翻译、测试和安装脚本，不包含本机编译产物。

在另一台满足依赖的设备上：

```sh
sha256sum -c icontray-0.1.0.tar.gz.sha256
tar -xzf icontray-0.1.0.tar.gz
cd icontray-0.1.0
./install.sh
```

各设备的排序独立保存。自定义图标若使用本地路径，需要在目标设备上准备相应文件；使用主题图标名更便于迁移。

## 预览与开发

```sh
./tools/build.sh
./tools/preview.sh
```

预览使用 `plasmawindowed`，从构建目录加载，不安装，也不更改面板。独立窗口用于检查加载和基本交互，最终效果请在面板中检查。

常用选项：

```sh
./install.sh --prefix "$HOME/.local" --jobs 4
./install.sh --cmake /path/to/cmake
./tools/build.sh --skip-tests
```

`--skip-tests` 适合目标系统未安装 Qt Test 时；默认会运行排序策略、图标几何、QML 配置、安装卸载流程及隔离会话中的组件/设置页加载测试。CMake 优先从命令行、`CMAKE` 环境变量和 `PATH` 查找，也可复用现有构建缓存中仍可用的路径。
`--no-session-env` 可禁止生成环境文件，但用户安装时需自行保证 Qt 插件搜索路径包含安装目录。
自定义前缀推荐用于用户级安装；系统级安装使用默认 `/usr`，以匹配系统插件搜索路径。
若切换安装前缀/模式，请先按原来的模式卸载，或使用独立的 `--build-dir`。

直接使用 CMake：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

## 卸载

先从面板移除 IconTray，再执行与安装方式对应的命令：

```sh
./uninstall.sh
# 系统级安装：
./uninstall.sh --system
```

脚本安装时会在 `<安装前缀>/share/icontray/install.json` 保存文件清单和环境文件位置。卸载优先读取这份记录，因此不依赖 `build` 目录。
源码目录中的 `./uninstall.sh` 默认卸载 `~/.local` 版本；自定义前缀需传入同样的 `--prefix`，或直接使用 `<安装前缀>/bin/icontray-uninstall`。

```sh
./uninstall.sh --dry-run                  # 预览将删除的文件
./uninstall.sh --restart                  # 当前用户卸载并刷新
./uninstall.sh --system --restart         # 系统级卸载并刷新
/path/to/prefix/bin/icontray-uninstall    # 无需项目源码和构建目录
```

旧版安装或直接执行 `cmake --install` 的安装没有独立记录，仍可传入原来的 `--build-dir`，从 `install_manifest.txt` 卸载。使用上面的脚本更新安装后即可生成新记录。
卸载只移除组件、翻译、卸载工具和对应安装记录，以及由它生成的用户环境文件；保留托盘设置和面板布局。请先从面板移除 IconTray。不加 `--restart` 时，注销后重新登录生效。

## 行为说明与限制

- 分组可以手动调整；旧版的线条图标组自动并入应用图标组，原有排序设置保留。
- 开启分组时，先按组排列，再按用户顺序排列；跨组拖动/上下移动会同时更改所属组。整体方向可以在“常规”中反转。
- 排序设置页包括可用但未启用的系统项目；是否显示仍由“常规”的显示规则决定。未运行过的第三方应用需先启动一次，才能在列表中设置。
- 大部分应用的托盘 ID 稳定；若应用每次启动自行生成不同 ID，它的规则可能无法自动恢复。多个相同 ID 的实例共享规则。
- 图标按最终正方形尺寸重新绘制，尺寸与位置按物理像素对齐，并受可用空间限制。缩放不会自动裁掉透明边缘，低分辨率位图放大仍可能模糊；建议使用 SVG 或主题图标。
- 替代图标仅作用于第三方 SNI 应用图标；网络/电量等系统小部件保留自身动态状态图标。应用请求注意时也优先保留原提醒图标。
- “常规”中的随面板缩放优先于固定尺寸。实际尺寸受面板厚度限制；尺寸数值使用逻辑像素，适配 HiDPI。
- 继承 KDE 对 Wayland/X11 与传统托盘桥接的支持；所有第三方应用交互、各主题、多屏和不同缩放组合尚需在实际桌面中验证。
- Plasma 使用了私有接口，大版本/次版本升级后可能需要重新编译或适配。

## 源码与许可证

`src/` 是基于 KDE 原生系统托盘的分支，`vendor/libdbusmenuqt/` 保留 DBus 菜单代码。
来源与固定提交见 [UPSTREAM.md](UPSTREAM.md)。原有作者和 SPDX 声明均保留，许可证文本位于 `LICENSES/`。
IconTray 新增代码采用 GPL-2.0-or-later。
