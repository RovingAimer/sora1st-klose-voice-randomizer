# 本地素材与运行包工具

这些工具用于已有游戏和素材的本地开发，不是从 GitHub 下载音频的工具。编译和源码测试不需要运行本页步骤。

## 素材准备

`prepare_assets.py` 读取模块上级目录内的 `pac/steam/voice.pac`、`pac/steam/table_sc.pac` 和 `空之轨迹1st语音替换/**/*.7z`；跳过父文件夹名称以 `-` 开头的工具目录。它需要 PATH 中有 `7z`，不会修改游戏 PAC。

为了使用既有目录约定，可将源码中的 `voice_randomizer` 模块放到自己的游戏目录下开发。已有 MOD 时先保留个人配置和资源，勿直接覆盖。运行：

```powershell
python .\voice_randomizer\prepare_assets.py
```

脚本提取旧 WAV 到 `assets/old/`，验证游戏文件名、语音 ID、PCM 格式及哈希，生成本地 `assets/manifest.json` 并更新 `src/assets.generated.h`。仓库已经包含当前 824 条映射，因此普通编译无需重新生成。音频和资源 manifest 不提交到 Git。

## 开发用占位音

先完成上述素材准备，需要 Windows PowerShell 5.1 的 System.Speech、`Microsoft Huihui Desktop` 中文语音和 PATH 中的 FFmpeg：

```powershell
powershell.exe -NoProfile -File .\voice_randomizer\synthesize_placeholders.ps1
python .\voice_randomizer\build_placeholders.py
python .\voice_randomizer\verify_assets.py
```

结果是双提示音加中文报号的 WAV，与游戏原语音采样数相同，保存在本地 `assets/placeholder/`。这些文件和 TTS 中间文件被 Git 忽略。测试时在图形设置中选择占位音素材集并重启游戏。

完整集成测试需已核验的游戏 EXE、生成的 INI 及旧版/占位音两套资源：

```powershell
.\voice_randomizer\bin\vr_tests.exe .
```

该测试读取游戏文件并在 `voice_randomizer/build/` 创建测试文件，不启动游戏。实际剧情/战斗听感和覆盖仍需实机验证。

## 本地打包

`package_release.py` 按明确文件清单打包 v1.2.0，需要已构建的 DLL/设置程序、生成的 INI、四张 UI 图片、manifest 及全部 824 条旧语音：

```powershell
python .\voice_randomizer\package_release.py
python .\voice_randomizer\test_release_package.py
```

ZIP 生成于 `voice_randomizer/dist/v1.2.0/`，根目录只有 DLL、资源目录与简明中英文 TXT；TXT 末尾附完整 MinHook 许可证。运行包包含音频，因此不应提交到本源码仓库。脚本拒绝与已有 staging 内容混合；再次打包前自行归档上次结果。

## English

These optional tools work with locally supplied game files and voice archives; they do not download audio. The module's parent directory must contain the matching game files (`pac/steam/voice.pac` and `table_sc.pac`) and the supplied `空之轨迹1st语音替换` archive tree. `prepare_assets.py` requires 7-Zip on PATH, extracts legacy WAVs, validates their mapping and format, and generates the local manifest plus the C++ name/ID table. It leaves game PACs unchanged.

Placeholder generation additionally requires Windows PowerShell 5.1, System.Speech with Microsoft Huihui Desktop, and FFmpeg. Run the commands above to generate, verify and test these local assets. The full native integration test reads the game executable and both audio sets without launching the game.

Packaging requires all 824 legacy WAVs, their generated manifest, the compiled DLL/editor, the generated INI and the four UI PNGs. The resulting ZIP includes audio and stays local under the ignored `dist/` directory. Audio, manifests, archives, game files and build products are excluded from Git.
