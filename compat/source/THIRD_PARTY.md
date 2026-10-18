# 第三方来源与许可

模块的播放规则、UI 和语音索引承接克萝赛随机语音 v1.2.0 的反编译移植版。原 DLL SHA256 为 `c0d344f13742234bb448749baa1eed262e1fee6aad289ed3a9c8c2d7eacb5c92`；原 DLL 和设置程序均未由构建或打包工具加载、执行。当前已 fork [作者公开源码](https://github.com/kumopult/sora1st-klose-voice-randomizer)，对照基线 `d4b8a96c9399db68c600a11fa84b7bdd105e25d8`。用户已告知取得作者授权；保留原作者权利，不把改编部分自行声明为 MIT，音频和其他素材按实际授权范围处理。本轮仅交付本地候选。

原始安装包中的 `voice_randomizer.ini`、824 条 WAV、4 张 PNG 和“克萝赛MOD设置.exe”均保留原字节。完整源码 ZIP 包含 fork 的跟踪源码，作者编辑器源码、资源脚本与配置模板位于 `voice_randomizer/`；当前保留的编辑器 EXE 未重新编译，不声称其字节与公开源码构建严格一致。工具从传入的只读原包校验并复制原说明到源码 ZIP 的 `compat/source/third_party/original-notice.txt`，不进入安装 ZIP。原包根目录的 `xinput1_4.dll` 代理及二进制不进入任何安装/源码 ZIP；源码 ZIP 仍保留作者代理工程的源代码。

新模块使用 Detours，依赖快照来自上游 commit `9764cebcb1a75940e68fa83d6730ffaf0f669401`。源码包保留其固定源码、`LICENSE`、`LICENSE.md` 与 `CREDITS.md`。安装 DLL 的 PE 资源中也包含 Detours 完整 MIT 许可文本；安装包不依赖额外许可旁文件。原作者说明内列出的 MinHook 许可属于原发布内容；新模块未使用 MinHook。

`source/build.py` 中 MSVC/SDK 环境发现部分改编自现有加载器的 MIT 构建辅助代码。该辅助代码适用的 MIT 版权和许可文本保留在 `source/src/PROVENANCE.md`；不得把该许可扩展解释为原作者模块、素材或设置程序的许可。
