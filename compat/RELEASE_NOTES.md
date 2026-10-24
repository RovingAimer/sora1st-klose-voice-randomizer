新增软保底：连续未抽中目标后可逐次提高概率，自行调整触发次数和提升幅度。

v0.1.1 移除额外 EXE hash 白名单，九个原生函数改为特征扫描定位。本次按用户授权仅静态验证后发布。

还可以调整默认语音音量、播放记录条数与透明度，以及“歪”标识大小；支持其他角色说话时隐藏记录。改变记录条数不会改变图标大小。

下载：

- 安装使用：选择 `VoiceRandomizer-v0.1.1-main.zip` 程序包，旧语音素材需自行准备。
- 查看代码：选择 `VoiceRandomizer-v0.1.1-source.zip` 源码包。

需先安装[通用加载器](https://github.com/RovingAimer/sora1looseload/releases/tag/v0.2.0)。退出游戏并备份后，按原目录解压到游戏根目录；[首次安装和回退说明](https://github.com/RovingAimer/sora1st-klose-voice-randomizer/blob/compat/generic-loader/compat/README.md)。

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
