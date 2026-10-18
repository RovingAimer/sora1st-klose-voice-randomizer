# 通用加载器语音兼容候选

此目录承接此前已完成的反编译移植版，保留作者随机语音、设置程序与播放记录功能，通过 sora1looseload v0.2.0 的模块接口加载。作者原工程保留在上层 `voice_randomizer/`，本次以其公开源码核对现有实现。本候选需要用户实机验收。

## 安装内容

安装包在 `dist/`，只包含安装文件；完整源码与说明另包提供。结构为：

```text
mods/VoiceRandomizer/VoiceRandomizer.dll
mods/VoiceRandomizer/sora_mod.ini
voice/wav/voice_randomizer/voice_randomizer.ini
voice/wav/voice_randomizer/克萝赛MOD设置.exe
voice/wav/voice_randomizer/assets/old/（824 WAV）
voice/wav/voice_randomizer/assets/ui/（4 PNG）
```

保留原素材、配置和编辑器字节，不替换根目录 `xinput1_4.dll`，也不覆盖字幕或相机文件。旧安装已有配置时，先备份用户配置；升级候选优先用仅模块更新包，避免完整包中的默认 INI 覆盖自己的设置。步骤见[验收与回退](验收与回退.md)。

## 保留功能

- `random`、`new`、`old`、每个语音名独立的新旧交替；旧版基础概率 0–100%。
- 保底、捕获明光及目标版本切换，规则状态重置时机保持原语义。
- 原生设置编辑器，UTF-16 LE BOM 配置；除启动时决定的 `ReplacementSet` 外约一秒重载。
- 最近十条播放记录、新旧头像、歪与明光标识、位置和滚动动画，隐藏时仍保留记录。
- 独立缓存身份，避免游戏移除目录后让新旧语音共享缓存。

核心入口 `DWORD WINAPI SoraVoiceInitialize()`。仅支持 `sora_1st.exe` SHA256 `9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac`，2nd 不支持。

## 继续构建

在 `compat/source/` 运行 `python build.py`；需要指定工作区 SDK 时使用 `--sdk-root`。采用已安装的 x64 MSVC、C++17、静态 CRT，详见 [BUILDING](source/BUILDING.md)。不安装新环境，不自动运行候选。作者编辑器源码和原构建方法在上层 `voice_randomizer/`，当前安装包保留已有编辑器二进制，不声称已用当前编译器重现其字节。

## 已知差异

替换后的声音由游戏与现有 loose loader 打开。启动和每次播放前检查旧素材，失败则回退新版；同步排队失败也会尝试回退。旧 WAV 在排队后、异步打开前被移除的窗口，没有原代理的 open-hook 回退。当前方案保留已审查的目录别名和独立缓存键，不为该罕见窗口新增全局文件打开 hook。

原作者、素材来源与第三方许可见 [THIRD_PARTY](source/THIRD_PARTY.md) 和上层 [第三方说明](../THIRD_PARTY_NOTICES.md)。本地完整素材包仅用于用户验收，不因 fork 而获得额外音频发布授权。
