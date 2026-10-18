# Source and license boundary

This compatibility source was migrated without a functional rewrite from
`VoiceRandomizerCompat/source`, commit
`17264de3b91c6e744544ab9660d224b2d4cf9747`, into this fork's canonical
`compat/source` entry. The author's original `voice_randomizer/` project is
retained separately. The migration retains the existing Detours integration,
`SoraVoiceInitialize` entry point, configuration, selection rules and overlay.
Build discovery is updated for the installed MSVC rather than fixed to 14.43.

`module.cpp`, `rules.cpp`, `overlay.cpp`, `VoiceRandomizer.h`, and `voice_index.inc`
reconstruct behavior and tables from Sora1st-克萝赛随机语音 v1.2.0, original DLL
SHA256 `c0d344f13742234bb448749baa1eed262e1fee6aad289ed3a9c8c2d7eacb5c92`.
The original DLL was analyzed statically and was never loaded or executed.
The original author retains rights in their work. This local compatibility
reconstruction makes no independent MIT claim or public redistribution grant
over those derived portions. The user reports permission from the original
author; that statement does not create a new license for these derived portions
or expand the scope of the author's permission.
Original resources and their notices must be retained unchanged.

Detours sources are copied unchanged from the verified local loader dependency,
upstream commit `9764cebcb1a75940e68fa83d6730ffaf0f669401`, without its nested
`.git`. Its `LICENSE`, `LICENSE.md`, and `CREDITS.md` are preserved alongside
`external/detours/src`. No test or sample project is used by this build.

The MSVC/SDK environment discovery in `build.py` is adapted from the existing
loader's MIT build helper. The following notice applies to that adaptation only:

Copyright (c) 2025 Hinkiii

Build and integration additions Copyright (c) 2026 DialogueOutline contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
