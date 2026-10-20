# 第三方来源与权利边界

兼容模块承接克萝赛随机语音 v1.2.0 的既有移植实现，并对照作者公开仓库 [kumopult/sora1st-klose-voice-randomizer](https://github.com/kumopult/sora1st-klose-voice-randomizer) 核对和维护。公开源码基线为 `d4b8a96c9399db68c600a11fa84b7bdd105e25d8`。用户已告知取得作者授权。作者及其他权利人的权利仍保留；本 fork 不将改编模块、编辑器或素材自行声明为 MIT，也不因本地兼容工作产生额外的素材再分发许可。

旧安装包中的原 DLL SHA256 为 `c0d344f13742234bb448749baa1eed262e1fee6aad289ed3a9c8c2d7eacb5c92`。构建和打包流程不加载或运行原 DLL、原设置程序或候选模块。完整 fork 源码 ZIP 包含作者公开源码、适配模块源码、编辑器源码、构建和打包工具及其附带说明；不包含原 DLL、设置编辑器二进制、游戏文件或音频素材。依赖 Detours 的固定源码和许可一并保留；加载器构建辅助代码对应的许可见 `src/PROVENANCE.md`。这些依赖许可不扩展为对作者作品或素材的许可。

## 配置编辑器

安装包中的设置程序仍命名为 `克萝赛MOD设置.exe`，但其二进制是从 fork 中已修改的作者编辑器源码重新构建的。修改增加软保底、Overlay 和双声道字段；因此不能再说编辑器二进制与原程序字节一致。`python build.py --editor` 的本地构建产物名为 `KloseVoiceSettings.exe`；打包时重命名为安装文件名。从公开编辑器源码与更新后的 `voice_randomizer/release/config_template.ini` 生成新版程序和 UTF-16 LE BOM 默认配置。

旧版 `voice_randomizer.ini` 缺少新增键时，模块按模板默认值读取；软保底与双声道均默认关闭。仅模块更新包含 DLL、`sora_mod.ini` 和新版编辑器，不含或覆盖用户的 `voice_randomizer.ini`。完整安装包包含新版默认 INI、新编辑器、824 个旧版 WAV 和 4 张 PNG；它以新默认配置替换包内原配置，故用户应在安装前备份自己的 INI。两个包中的旧版 WAV 和 PNG 按原字节复制；源代码包不含这些文件。

## 本地双声道素材

双声道音频由本地游戏语音 PAC 与已有 824 个旧版 WAV 配对生成。ZIP 内有 `voice/wav/voice_randomizer/assets/stereo_lr/` 和 `stereo_rl/` 两组，每个语音各一份；WAV 为 48 kHz、16 位、PCM 2 声道。`stereo_lr` 左新版、右旧版；`stereo_rl` 左旧版、右新版。当前 `dist/20261020-stereo/VoiceRandomizer-stereo-assets.zip` 有 1,648 个 WAV，大小 703,123,359 字节，解压后 WAV 数据共 1,054,706,656 字节。清单记录 824 对旧音频索引匹配及 ZIP CRC 回读核验。

该素材包只供用户个人本地验收，游戏语音与旧版音频的权利各自保留，不随源码包或公开发布上传。打包/生成工具只读取显式提供的来源，不写入游戏或输入目录。生成工具当前 `--help` 接受必填 `--voice-pac`，以及二选一的 `--old-archive` / `--old-root`；可选 `--out` 与 `--reference-index`。默认输出目录为 `compat/dist/20261020-stereo`。不要假设或使用其他参数。

## 验收状态

options 源码已完成，最终提交、构建和打包尚待完成；DLL、INI、设置编辑器及完整源码包的目标输出目录为 `compat/dist/20261020-options/`。本地构建或 ZIP 核对不能证明游戏运行结果。双声道素材 ZIP 已生成，清单记录 824 对音频索引匹配、1,648 个成员和 CRC 回读核验；实际游戏中的声道左右、音量与硬件/场景路由尚待手动验收。没有完成这些实机检查前，不将候选描述为已通过。
