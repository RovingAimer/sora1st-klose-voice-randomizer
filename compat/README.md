# 克萝赛随机语音 · 通用加载器版

适用于《空之轨迹 the 1st》，需要先安装 [sora1looseload 通用加载器 v0.2.0](https://github.com/RovingAimer/sora1looseload/releases/tag/v0.2.0)。

## 新增功能

- 软保底：连续未抽中目标后，逐次提高目标概率，可自行调整触发次数和提升幅度。
- 默认语音音量：可单独调整克萝赛的新旧语音音量。
- 播放记录可调整显示条数、透明度和“歪”标识大小。
- 可在其他角色说话时自动隐藏记录，克萝赛说话时恢复。
- 调整记录条数时，头像和图标不会随之变大或缩小。

保留新版、旧版、随机、逐句交替，以及硬保底和捕获明光。上述功能已由用户实机确认；双声道功能不包含在本次发布中。

## 下载与安装

[下载 v0.1.0](https://github.com/RovingAimer/sora1st-klose-voice-randomizer/releases/tag/v0.1.0)。

- `VoiceRandomizer-v0.1.0-main.zip`：首次安装程序包，含默认配置和界面图片。
- `VoiceRandomizer-v0.1.0-source.zip`：完整源码，供查看和自行编译。

退出游戏并备份现有语音模块、设置程序和配置，再将程序包按原目录解压到游戏根目录。不替换根目录加载器，其他 Mod 保持原样。

程序包不含旧语音素材。首次安装时，将原 Mod 的 `assets/old/` 放入 `voice/wav/voice_randomizer/assets/old/`。使用 `voice/wav/voice_randomizer/克萝赛MOD设置.exe` 修改设置。

## 与原版的目录差异

以下路径均相对于游戏根目录：

| 内容 | 原版位置 | 本版位置 |
| --- | --- | --- |
| 语音模块 | `xinput1_4.dll` | `mods/VoiceRandomizer/VoiceRandomizer.dll`，同目录附 `sora_mod.ini` |
| 旧语音 WAV | `voice_randomizer/assets/old/` | `voice/wav/voice_randomizer/assets/old/` |
| 界面 PNG | `voice_randomizer/assets/ui/` | `voice/wav/voice_randomizer/assets/ui/` |
| 个人配置 | `voice_randomizer/voice_randomizer.ini` | `voice/wav/voice_randomizer/voice_randomizer.ini` |
| 设置程序 | `voice_randomizer/克萝赛MOD设置.exe` | `voice/wav/voice_randomizer/克萝赛MOD设置.exe` |

根目录的 `xinput1_4.dll` 使用通用加载器提供的版本，语音模块放入 `mods/VoiceRandomizer/`。

从原版迁移：先备份，再将旧语音 WAV 复制到新版的 `assets/old/`，保持原文件名；想保留设置，可将原 INI 复制到表中的新位置。界面图片和设置程序使用本安装包提供的新版。旧语音素材不包含在程序包内。

## 配置与维护

设置保存后约一秒读取，对后续播放生效；切换素材需要重启游戏。旧配置缺少新增字段时使用默认值。默认音量为 100%，软保底默认关闭；音量改变不清空规则计数。

`[Randomizer]` 新增 `VolumePercent`（0–100）、`SoftPityEnabled`、`SoftPityStart`（1–1000）和 `SoftPityStep`（0–100）。连续未中目标 X 次后，下次增加 Step 个百分点，再未中继续增加，最高 100%；命中目标清零。硬保底优先。软参数变化只清软计数；目标、硬保底或明光开关变化清全部规则计数。

`[Overlay]` 新增 `MaxEntries`（1–50）、`TransparencyPercent`（0–100）、`WaiScalePercent`（0–200）和 `HideOnOtherVoice`。图标按固定十条基准计算尺寸，条数只影响数量；超过画面的部分裁剪。`HeightPercent` 对应“UI 区域高度”，控制大小；`CenterYPercent` 对应“垂直中心”，控制位置。歪标识以自身左上角缩放；透明度 100 为完全隐藏。

旧 `[Stereo]` 配置被模块忽略，编辑器保存时移除该节，不影响其他配置。双声道实验实现留在源码中停用，不在此版本修复。

构建见 [source/BUILDING.md](source/BUILDING.md)，检查和回退见[验收与回退](验收与回退.md)，来源与许可见 [source/THIRD_PARTY.md](source/THIRD_PARTY.md)。当前只支持 1st 的指定游戏版本，不宣称 2nd 兼容。
