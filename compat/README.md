# 通用加载器语音兼容候选

此目录维护克萝赛随机语音的通用加载器适配。模块通过 sora1looseload v0.2.0 加载，并兼容缺少新增字段的旧 INI：缺项按默认值读取。ReplacementSet 在游戏启动时确定；其他配置保存后约 1 秒重读，并对后续播放生效。仅支持 sora_1st.exe SHA256 为 9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac 的版本。

## 本地候选

本轮候选目标目录为 compat/dist/20261020-native-options/，压缩包文件名沿用 VoiceRandomizer-options-candidate：

- module-update.zip：3 个文件，模块 DLL、sora_mod.ini 和新编译的设置编辑器；保留用户的 voice_randomizer.ini。
- install.zip：832 个文件，包括模块文件、新默认 INI、设置编辑器、原有 824 个 WAV 和 4 张 PNG。首次安装前备份个人 INI。
- source.zip：完整 fork 源码。

候选只留本地供手动验收，不创建 release，也不上传音频。候选实际内容以同目录清单为准。

## 随机规则

random 按基础概率抽取新旧语音，并可应用硬保底、捕获明光和软保底。新、旧和逐语音交替模式保持原有行为。

软保底默认关闭。连续未命中目标 X 次后，下次抽取的目标概率增加 Step 个百分点；之后每再未中一次，下一次继续增加同样的百分点，最高 100%。例如目标基础概率为 50%、Start=2、Step=10：两次未中后，下次为 60%；再未中，下次为 70%。命中目标后软计数清零；硬保底或捕获明光提前命中目标，也会清零。

改变软保底开关、阈值或步长只清空软保底计数。改变 GuaranteeTarget、硬保底开关或捕获明光开关会清空全部规则计数。非 random 模式暂停随机规则计数；双声道开启时优先播放新旧两路语音，并暂停随机、逐语音交替和所有保底计数。

新增设置如下。旧 INI 缺少这些字段时使用表中默认值；数值会限制在有效范围内。

| 配置键 | 默认值 | 有效值与作用 |
| --- | --- | --- |
| [Randomizer] SoftPityEnabled | 0 | 0 或 1；软保底开关。 |
| [Randomizer] SoftPityStart | 10 | 1–1000；连续未中阈值 X。 |
| [Randomizer] SoftPityStep | 5 | 0–100；每次增加的目标概率百分点。 |
| [Overlay] MaxEntries | 10 | 1–50；控制显示条数，不改变头像大小或间距。 |
| [Overlay] TransparencyPercent | 0 | 0–100；0 完全不透明，100 完全隐藏。 |
| [Overlay] WaiScalePercent | 100 | 0–200；只缩放歪标识，锚定其左上角。 |
| [Overlay] HideOnOtherVoice | 0 | 0 或 1；其他角色语音时隐藏记录，克萝赛语音后恢复。 |
| [Stereo] Enabled | 0 | 0 或 1；同时播放新旧两路单声道语音。 |
| [Stereo] SwapChannels | 0 | 0 或 1；交换新旧语音所在的左右声道。 |
| [Stereo] LeftVolume | 100 | 0–100；左侧语音音量百分比。 |
| [Stereo] RightVolume | 100 | 0–100；右侧语音音量百分比。 |

## Overlay

记录保留最近 50 条。MaxEntries 默认显示 10 条，范围为 1–50；它只控制显示数量。图标大小和行间距始终按十条基准计算，条数变化不会缩放头像或改变间距。超过十条时列表从底部向上扩展，超出屏幕的上部会被裁剪。

配置键 HeightPercent 对应设置界面的“UI 区域高度 (%)”，用于控制 Overlay 区域大小；CenterYPercent 对应“垂直中心 (%)”，仍控制垂直位置。TransparencyPercent 的 0 表示完全不透明，100 表示完全隐藏。WaiScalePercent 只缩放歪标识并固定左上角。HideOnOtherVoice=1 时，成功播放 kind=2 的其他角色语音会隐藏 Overlay；之后播放克萝赛语音会恢复显示，历史记录不清空。

## 双声道

双声道由同一个播放任务句柄中的两路原生单声道语音组成，分别来自游戏新版语音和现有旧版 WAV。内部双声道子混音将两路分别路由到左、右声道，并为每路设置独立音量；默认左边新版、右边旧版，SwapChannels 可交换两边。现有 824 个旧版 WAV 保持原样，不需要预合成双声道 WAV 包。

左右音量设置保存并约 1 秒重读后，在之后新建的 SourceVoice 上应用；已经播放的语音不变。模块保留游戏原有的高层播放请求和引擎播放路径。源码静态调用显示它会设置 SourceVoice 的左右矩阵和音量。若旧语音未能缓存或第二路未启动，播放会回退为普通新版语音；第二路失败时会把已启动的新版语音恢复为双侧输出和原音量。实际设备左右输出、冷缓存播放、跳过、淡出及结束等待行为仍待游戏内验收。播放记录的双头像只表示双声道模式已选择，须结合听测及 NATIVE_STEREO_PAIR both=1 日志确认两路均已启动。

## 设置编辑器、构建与验收

设置编辑器从已修改的作者公开源码重新构建以支持新增字段；安装文件名仍为“克萝赛MOD设置.exe”，构建目录中的产物名为 KloseVoiceSettings.exe。不能将这个新版编辑器描述为原二进制字节保持。

构建和手动验收步骤见[验收与回退](验收与回退.md)及 [source/BUILDING.md](source/BUILDING.md)。普通规则、软保底和 Overlay 先验收，再检查新版设置界面，最后验收双声道。源码来源和权利边界见 [source/THIRD_PARTY.md](source/THIRD_PARTY.md)。游戏内效果尚未验收通过。
