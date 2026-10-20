# 构建模块

本 fork 的兼容模块规范入口是 `compat/source/`；作者的 `voice_randomizer/` 工程独立保留。源码 ZIP 解压后也在包含 `build.py` 的目录运行。Python 只使用标准库。构建需要 Windows x64、已安装的 MSVC x64 Build Tools 和 Windows SDK；不锁定某个 MSVC 小版本，通过 Visual Studio 自带的 `vswhere` 和 `vcvars64.bat` 发现工具集，报告记录实际路径和版本。本机使用 `C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools` 中的 MSVC 14.51.36231。默认从已安装的 Visual Studio 环境发现 SDK；若 SDK 不在默认发现路径，可显式提供 SDK 根目录：

```powershell
python build.py
python build.py --sdk-root "X:\path\to\WindowsSDK"
python build.py --sdk-root "G:\WorkSpace\ModDevelop\SoraMod\toolchain\sdk\c"
python build.py --sdk-root "G:\WorkSpace\ModDevelop\SoraMod\toolchain\sdk\c" --editor
```

SDK 可采用标准 `Include/10.x` 与 `Lib/10.x/{um,ucrt}/x64` 布局，也可采用工作区已有的 `Include/10.x` 与根目录 `{um,ucrt}/x64` 扁平库布局。不会因为缺少 `Lib/` 而否定后一种布局；脚本检查实际必需头文件和库。

`--editor` 在模块构建后额外编译作者原生设置窗口，输出同一 `build/` 下的 `KloseVoiceSettings.exe`。需保留完整 fork 布局中的 `voice_randomizer/src/` 与 `voice_randomizer/release/config_template.ini`；完整源码 ZIP 解压后无需 Git 也可使用。设置器复用同一 MSVC、SDK 头与扁平库，另使用本机已安装 Windows Kits 的 x64 `rc.exe`、`mt.exe`（优先匹配SDK版本，然后查找已安装版本或PATH），不安装新工具。模板从 UTF-8 转为 UTF-16 LE BOM 写入 `build/editor_template.ini`；生成的RC副本只改RCDATA输入路径，保留原VERSIONINFO，链接保留DPI、asInvoker与Common-Controls manifest。不会写作者目录中的生成INI，不运行设置器或作者测试程序。

`report.json` 的可选 `editor` 项记录设置EXE路径、SHA256、大小、编译器/SDK/资源工具、相对fork根的源码输入哈希、生成资源哈希与系统依赖。构建直接读取PE核对AMD64/GUI子系统、内嵌模板逐字节一致、版本资源与manifest，并记录 `executed=false`、`upstream_tests_run=false`；这些检查不启动设置程序。

工具只在本源码目录的 `build/` 写入 `VoiceRandomizer.dll`、PE 导入/导出检查、命令记录、输入 SHA256 和诊断日志，构建子进程的 `TEMP` / `TMP` 也指向这里。这里的产物是本地构建材料，不属于源码 ZIP。构建只编译和检查 PE，不加载 DLL、不启动游戏，也不写入游戏目录。模块拒绝不匹配的游戏版本；支持版本的 `sora_1st.exe` SHA256 为 `9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac`。

`build/report.json` 记录唯一导出 `SoraVoiceInitialize`、静态依赖、内嵌 Detours 许可的逐字节检查及实际源码输入哈希。在 fork 内构建时，`source_git` 记录 fork HEAD 和本源码目录是否有未提交修改，并仅对该 Git 命令设置 `safe.directory`；独立提取的源码没有 Git 元数据时，这两个值为 `null`，源码哈希仍完整记录。移植基线与原 DLL 来源见 `src/PROVENANCE.md`。

打包器迁入 `compat/source/tools/package_candidate.py`，由 fork 的兼容流程维护；在 `compat/` 下运行，并显式传入只读原包目录。它依赖 Python 标准库和 Git 命令行，不会从本机配置猜路径：

```powershell
$originalPackage = Read-Host "Path to the original package directory"
python source\tools\package_candidate.py --original $originalPackage
```

迁移资料只保留原 DLL/说明而完整素材在上次安装 ZIP 时，可加 `--assets-archive "上次兼容候选-install.zip"`。工具按原始清单逐一核对 ZIP 内 INI、编辑器、824 WAV 和 4 PNG，直接流式压入新包，不展开额外素材副本，也不复制上次 DLL。产出完整安装包、保留用户 INI 的三文件更新包（DLL、模块声明、新设置程序），以及包含作者编辑器工程的完整 fork 源码包。素材与私人清单保持本地，不提交 Git。

源码 ZIP 不含反编译分析目录和原始语音素材，因此独立源码构建不需要 `analysis/`。打包器是本地项目工作流的一部分，使用工作区的原始输入清单、完整原包哈希清单与语音索引核对传入的原包目录；这些分析清单不随源码 ZIP 交付。
