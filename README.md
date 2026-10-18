# 克萝赛随机语音 MOD

《空之轨迹 the 1st》克萝赛（科洛丝 / Klose）新旧语音随机 MOD 的源码，包含游戏内播放记录 UI 与原生 Windows 图形设置程序。当前功能对应 v1.2.0（语音 DLL v1.1.0）。

**本仓库只提供源码、构建工具、配置模板及四张 UI PNG，不含任何新旧语音、测试音频、游戏文件或编译后的运行包。** 旧语音资源来源于 B 站 UP 主 **Mini_Lom233**；运行时需要自行准备对应素材。没有旧素材的台词使用游戏配音。

## 功能

- 对有旧资源的 824 条克萝赛语音，每次播放选择新版或旧版，覆盖剧情、战斗、原野与主动语音；其他角色保持游戏配音。
- 基础旧版概率可调，默认 50%。保底与捕获明光可独立开关，目标可切换为新版或旧版，两层机制及【歪】标识同步反转。
- 显示最近 10 条播放记录，新记录从底部进入；头像表示实际版本，图片标识表示【歪】或【捕获明光】。
- 原生 Win32 设置程序支持中英文、位置预览、恢复默认与保留注释的配置保存；INI 同时保留中英文配置说明。

## 构建

需要 Windows x64、Visual Studio 2022 或 Build Tools（“使用 C++ 的桌面开发”及 Windows SDK）。MinHook v1.3.4 源码已包含，无需下载依赖，也不需要音频或游戏才能编译。

在仓库根目录打开 PowerShell：

```powershell
.\voice_randomizer\build.cmd
```

脚本通过 `vswhere` 查找编译器，或使用已有的 x64 开发者命令行环境。使用 C++17 和静态 CRT；Windows 自带 PowerShell 会将 UTF-8 配置模板转换为带 BOM 的 UTF-16 LE INI，并嵌入设置程序。

构建产物在 `voice_randomizer/bin/`：

| 文件 | 用途 |
| --- | --- |
| `xinput1_4.dll` | 游戏加载的语音与 UI 插件 |
| `KloseVoiceSettings.exe` | 图形设置程序，可重命名为 `克萝赛MOD设置.exe` |
| `vr_tests.exe` | 随机规则及游戏素材集成测试 |
| `overlay_tests.exe` | D3D11 离屏渲染测试 |
| `editor_config_tests.exe` | INI 解析、保存与冲突处理测试 |

只构建设置程序可运行 `voice_randomizer/build_editor.cmd`。

## 测试（无需游戏和音频）

先完成构建，另安装 64 位 Python 3.10 或更新版本。以下命令从仓库根目录运行：

```powershell
.\voice_randomizer\bin\vr_tests.exe --rules-only
python -m unittest discover -s voice_randomizer -p test_check_log.py -v
python .\voice_randomizer\make_ui_placeholders.py --output .\voice_randomizer\build\ui_test_fixture
.\voice_randomizer\bin\overlay_tests.exe .
.\voice_randomizer\bin\editor_config_tests.exe .
python .\voice_randomizer\proxy_test.py
```

规则测试涵盖两个目标版本、四种开关组合、计数与保底优先级、配置切换和并发。UI 测试使用 Windows D3D11 WARP 软件设备与独立纯色测试图片，另验证实际 UI PNG；不会覆盖仓库图片。完整的 `vr_tests.exe <游戏目录>` 还依赖匹配的游戏 EXE、配置模板及两套本地音频，详见[本地素材工具](docs/local-assets.md)。

## 本地运行

退出游戏后，将构建产物和自行准备的资源按以下结构放置：

```text
<sora_1st.exe 所在目录>/
  xinput1_4.dll
  voice_randomizer/
    克萝赛MOD设置.exe
    voice_randomizer.ini
    assets/
      ui/                     # 本仓库提供的四张 PNG
      old/                    # 自行准备的 824 个同名 WAV
```

INI 使用构建生成的 `voice_randomizer/release/voice_randomizer.ini`。双击设置程序调整并保存；大部分选项在游戏内约一秒后重载，素材集切换需重启游戏。已有同名 XInput 代理 DLL 时需要先处理兼容，不能直接叠加安装。

插件仅支持已核验的 Windows x64 / DirectX 11 游戏版本，启动时检查 `sora_1st.exe` 的 SHA-256：

```text
9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac
```

不匹配时禁用 hook，保持 XInput 转发。旧语音保留原时长，剧情同步可能与新版不同。日志位于 `voice_randomizer/voice_randomizer.log`。

## 源码结构

- `voice_randomizer/src/`：选择规则、XInput 代理、语音 hook、D3D11 UI、Win32 设置程序及 C++ 测试。
- `voice_randomizer/src/assets.generated.h`：824 条语音的文件名与 ID 对照，只有元数据，不含音频。
- `voice_randomizer/assets/ui/`：新旧头像及两个标识，四张 64×64 PNG。
- `voice_randomizer/release/`：本地运行包的配置与说明模板；这里描述的完整运行包需要另行准备音频。
- `voice_randomizer/third_party/minhook-1.3.4/`：MinHook 源码及原许可证。
- Python / PowerShell 脚本：本地素材整理、测试提示音生成、日志检查与运行包构建。生成的音频、日志和 ZIP 均被 Git 忽略。

## 致谢

旧语音来源：B 站 UP 主 **Mini_Lom233**。加载方式与文件入口研究参考 [sora1looseload](https://github.com/Hinkiii/sora1looseload)。Hook 使用 [MinHook v1.3.4](https://github.com/TsudaKageyu/minhook)，原版权与许可证保留于 [LICENSE.txt](voice_randomizer/third_party/minhook-1.3.4/LICENSE.txt)。详见[第三方说明](THIRD_PARTY_NOTICES.md)。

## English

Source for the Klose voice randomizer for *Trails in the Sky 1st Chapter*, including its Direct3D 11 history overlay and bilingual native Windows settings editor. The current feature set is v1.2.0 with runtime DLL v1.1.0.

**Only source, build tools, configuration templates and four UI PNGs are included. No audio, game files, compiled binaries or runtime ZIPs are published here.** Legacy voice resources were sourced from Bilibili creator **Mini_Lom233** and must be supplied separately. Lines without legacy assets keep the game's audio.

Build on Windows x64 with Visual Studio 2022 / Build Tools, the Desktop development with C++ workload and a Windows SDK. Run `voice_randomizer\build.cmd` from the repository root. MinHook is vendored; no game, audio or third-party downloads are needed to compile. Binaries appear in `voice_randomizer/bin/`; the UTF-16 LE INI is generated from the UTF-8 template.

The test commands above exercise selection rules, the log auditor, offscreen overlay rendering, settings persistence and XInput forwarding without starting the game. Python tests require 64-bit Python 3.10+. Full integration and packaging require local game/audio data; see [local asset tools](docs/local-assets.md).

Install the compiled DLL beside `sora_1st.exe`; put the settings editor, generated INI, UI PNGs and separately obtained legacy WAVs inside `voice_randomizer/` as shown above. The editor must be beside the INI. Most settings reload within about one second; changing the asset set requires restarting the game. Only the executable hash listed above is supported. An unsupported executable disables hooks while retaining XInput forwarding.

MinHook retains its original license. Loading and file-entry research references sora1looseload. See [third-party notices](THIRD_PARTY_NOTICES.md).
