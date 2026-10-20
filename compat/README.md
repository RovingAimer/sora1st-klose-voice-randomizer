# 通用加载器语音兼容候选

此目录维护克萝赛随机语音的通用加载器适配。模块接入 sora1looseload v0.2.0，兼容旧版 INI；缺少新增键时采用原默认行为。`ReplacementSet` 在游戏启动时读取，其他设置保存后约 1 秒重读、对后续播放生效。仅支持 `sora_1st.exe` SHA256 `9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac`。

## 候选文件

本轮 options 候选的目标目录为 `dist/20261020-options/`，完成最终构建和打包后应包含：`VoiceRandomizer-options-candidate-module-update.zip`（模块 DLL、`sora_mod.ini` 和新设置编辑器，不含或覆盖用户 INI）；`VoiceRandomizer-options-candidate-install.zip`（新默认 INI、新设置编辑器、824 个旧版 WAV 和 4 张 PNG，首次安装时先备份个人配置）；以及 `VoiceRandomizer-options-candidate-source.zip`（完整 fork 源码包）。这些是本地候选，实机表现仍待验收。

`dist/20261020-stereo/VoiceRandomizer-stereo-assets.zip` 是独立双声道素材包，包含 1,648 个 WAV，ZIP 为 703,123,359 字节，解压约 1.055 GB。该包只用于个人本地验收，音频不随公开源码发布。先安装选项更新包，再将双声道素材按目录解压到游戏的 `voice/wav/voice_randomizer/assets/` 下。

## 播放规则

- `random` 按基础概率选择版本，并可叠加硬保底、捕获明光和软保底；`new`、`old` 与逐语音 `alternate` 沿用原行为。
- `SoftPityEnabled=0` 默认关闭。开启后，连续未中目标 `X` 次，下次目标概率增加所设百分点；每次再未中，后续一次再加相同百分点，最高 100%。例如基础目标概率 50%、`Start=2`、`Step=10`，两次未中后的下次是 60%；若再次未中，再下次是 70%。命中目标（包括硬保底或捕获明光）会清零软计数。硬保底优先，可能提早命中并清零。
- 保底目标或硬保底/捕获明光开关变化会清空全部规则计数。只修改软保底开关、阈值或步长时，只清软保底连续未中计数。基础概率和界面设置变化不清计数。
- 非 `random` 模式暂停随机规则计数。双声道开启时优先播放两版语音，并暂停随机、逐句交替及各类保底计数。

新增设置的键名、默认值和有效范围如下；缺失键使用默认值，超出范围的整数限制在范围内。

| 配置键 | 默认值 | 有效值与作用 |
| --- | --- | --- |
| `[Randomizer] SoftPityEnabled` | `0` | `0` 或 `1`；软保底开关。 |
| `[Randomizer] SoftPityStart` | `10` | `1–1000`；连续未中阈值 X。 |
| `[Randomizer] SoftPityStep` | `5` | `0–100`；每次增加的目标概率百分点。 |
| `[Overlay] MaxEntries` | `10` | `1–50`；只控制显示条数，不改变头像大小或间距。 |
| `[Overlay] TransparencyPercent` | `0` | `0–100`；0 完全不透明，100 完全隐藏。 |
| `[Overlay] WaiScalePercent` | `100` | `0–200`；只缩放歪标识，左上角锚点固定。 |
| `[Overlay] HideOnOtherVoice` | `0` | `0` 或 `1`；其他角色语音时隐藏记录，克萝赛语音后恢复。 |
| `[Stereo] Enabled` | `0` | `0` 或 `1`；同时播放新旧语音。 |
| `[Stereo] SwapChannels` | `0` | `0` 或 `1`；交换左右两侧内容。 |
| `[Stereo] LeftVolume` | `100` | `0–100`；左声道音量百分比。 |
| `[Stereo] RightVolume` | `100` | `0–100`；右声道音量百分比。 |

## 设置与播放记录

新版设置编辑器仍名为 `克萝赛MOD设置.exe`，从同目录的 `voice_randomizer.ini` 读取并保存设置。公开的作者编辑器源码现已修改以支持新增字段；不再将这个新编译的编辑器描述为原文件字节保持。

Overlay 默认最多显示 10 条，范围 1–50；记录最多保留最近 50 条。头像大小和间距按十条参考行高固定，改变显示条数不会改变尺寸；超过十条时从底部向上延伸，超出屏幕的上部会裁剪。透明度 `0` 完全不透明、`100` 完全隐藏。歪标识缩放范围 0–200%，只缩放歪标识，并固定其自身左上角。`HideOnOtherVoice=1` 时，成功播放 `kind=2` 的其他角色语音会隐藏记录；下一条克萝赛语音会恢复显示，期间历史不清空。

## 双声道

双声道包为每个语音准备 `stereo_lr`（左新版、右旧版）和 `stereo_rl`（左旧版、右新版）两套 PCM 2 声道 WAV。播放时按交换设置选择其中一套；`LeftVolume` / `RightVolume` 在配置约 1 秒重读后应用于之后创建的声源，当前已播放的语音不变。换位或调音量不需要重写素材。播放路径在原采样器成功处理高层播放请求后，对原生 SourceVoice 调用 `SetChannelVolumes`。源码保留游戏的高层请求与播放引擎路径；设备实际左右声道和不同场景的路由仍待用户在游戏中验证。

## 构建和验收

在 `compat/source/` 运行 `python build.py --editor` 可构建模块与设置编辑器，构建产物名为 `KloseVoiceSettings.exe`；打包时将它放入安装包并命名为 `克萝赛MOD设置.exe`。源码 ZIP 提供独立源码和构建说明。options 最终提交、构建和打包待完成，随后仍需实机验收；安装次序、测试矩阵和回退步骤见[验收与回退](验收与回退.md)。源码来源和第三方权利边界见 [source/THIRD_PARTY.md](source/THIRD_PARTY.md)。
