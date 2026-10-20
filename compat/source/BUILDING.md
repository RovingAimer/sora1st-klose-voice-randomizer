# 构建模块

本 fork 的兼容模块入口是 compat/source/；作者的 voice_randomizer/ 工程独立保留。源码 ZIP 解压后也在包含 build.py 的目录运行。Python 只使用标准库。构建需要 Windows x64、Visual Studio x64 Build Tools 和 Windows SDK；工具通过 Visual Studio 自带的 vswhere 与 vcvars64.bat 发现实际工具集并记录版本。本机使用 Visual Studio 18 Build Tools 中的 MSVC 14.51.36231。默认从已安装的 Visual Studio 环境发现 SDK；若 SDK 不在默认路径，可显式提供 SDK 根目录：

~~~powershell
python build.py
python build.py --sdk-root "X:\path\to\WindowsSDK"
python build.py --sdk-root "G:\WorkSpace\ModDevelop\SoraMod\toolchain\sdk\c"
python build.py --sdk-root "G:\WorkSpace\ModDevelop\SoraMod\toolchain\sdk\c" --editor
~~~

SDK 可采用标准 Include/10.x 与 Lib/10.x/{um,ucrt}/x64 布局，也可采用工作区现有的 Include/10.x 与根目录 {um,ucrt}/x64 扁平库布局。脚本检查所需头文件和库。

--editor 在构建模块后额外编译作者原生设置窗口，输出 source/build/KloseVoiceSettings.exe。要保留完整 fork 中的 voice_randomizer/src/ 与 voice_randomizer/release/config_template.ini；完整源码 ZIP 解压后无需 Git 也可使用。设置器从已修改的作者源码构建，以支持新增配置字段。打包时将该 EXE 复制进设置目录并命名为“克萝赛MOD设置.exe”；这个文件名不表示它与原编辑器二进制相同。

设置器复用同一 MSVC 和 SDK 头/库，另使用已安装 Windows Kits 的 x64 rc.exe、mt.exe，不安装新工具。构建把 UTF-8 模板转为 UTF-16 LE BOM，生成 build/editor_template.ini；生成的 RC 副本仅改 RCDATA 输入路径，保留原 VERSIONINFO，并链接 DPI、asInvoker 与 Common-Controls manifest。不会写入作者目录中的生成 INI，也不会运行设置器或作者测试程序。

build/report.json 的 editor 项记录设置 EXE 路径、SHA256、大小、编译器/SDK/资源工具、源码输入哈希、生成资源哈希和系统依赖。构建静态检查 PE 架构、GUI 子系统、内嵌模板、版本资源和 manifest；这些检查不启动设置程序。

工具只在 source/build/ 写入 VoiceRandomizer.dll、PE 导入/导出检查、命令记录、输入 SHA256 和诊断日志。构建只编译和检查 PE，不加载 DLL、不启动游戏，也不写入游戏目录。模块仅支持 sora_1st.exe SHA256 为 9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac 的版本。

打包器位于 source/tools/package_candidate.py；从 compat/ 运行，并显式传入只读原包目录。它依赖 Python 标准库和 Git 命令行，不从本机配置猜测原包路径。输出必须位于项目工作区，可显式指定本轮候选目录：

~~~powershell
$originalPackage = Read-Host "Path to the original package directory"
python source/tools/package_candidate.py --original $originalPackage --release-version 0.1.0 --no-audio --out dist/v0.1.0
~~~

若需要从此前兼容安装包读取未变更的旧资源，可传入 --assets-archive，并提供完整旧素材候选安装 ZIP。该选项只提供原有资源输入并按清单核对；--no-audio 仅打包程序、配置和四张 UI PNG，旧 WAV 不进入公开附件。模块更新包只含 DLL、sora_mod.ini 和新版设置编辑器，不包含或覆盖用户 INI。--no-audio 发布程序包有 8 个文件，含新默认 INI、编辑器、模块和四张 UI PNG；不附带旧 WAV。源码包包含完整 fork 源码和作者源码包中公开的 4 张 UI PNG，不包含 824 个旧版 WAV、程序二进制或游戏文件。

打包前需要提交被打包的源码，脚本会检查模块构建报告、设置编辑器构建报告、源码输入哈希、原包清单和 ZIP 成员。ZIP 静态核验不能证明游戏运行结果。v0.1.0 不读取双声道配置，实验实现停用；旧 Stereo 节在编辑器保存时移除。发布页使用 compat/RELEASE_NOTES.md 中的玩家说明。
