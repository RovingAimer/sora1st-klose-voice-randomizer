$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$sourceDir = Join-Path $PSScriptRoot 'assets/tts_source'
New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'assets/manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $speaker.SelectVoice('Microsoft Huihui Desktop')
    $speaker.Rate = 2
    $speaker.Volume = 85
    $format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
    if (!(Test-Path -LiteralPath (Join-Path $sourceDir 'test.wav'))) {
        $speaker.SetOutputToWaveFile((Join-Path $sourceDir 'test.wav'),$format)
        $speaker.Speak([string]::Concat([char]0x6d4b,[char]0x8bd5))
        $speaker.SetOutputToNull()
    }
    $generated = 0
    foreach ($entry in $manifest) {
        if ($entry.new_seconds -lt 1.35) { continue }
        $phrase = $entry.placeholder_phrase
        if (!$phrase) { throw "Missing placeholder phrase for $($entry.name)" }
        $wavPath = Join-Path $sourceDir ($entry.name + '.wav')
        $textPath = Join-Path $sourceDir ($entry.name + '.txt')
        if (Test-Path -LiteralPath $wavPath) {
            $matchingText = (Test-Path -LiteralPath $textPath) -and ((Get-Content -LiteralPath $textPath -Raw -Encoding UTF8).Trim() -eq $phrase)
            if ($matchingText -or (!(Test-Path -LiteralPath $textPath) -and $entry.name -match '^v006_b\d{4}b?$')) { continue }
        }
        $speaker.SetOutputToWaveFile($wavPath,$format)
        $speaker.Speak($phrase)
        $speaker.SetOutputToNull()
        [IO.File]::WriteAllText($textPath,$phrase,[Text.UTF8Encoding]::new($false))
        $generated++
        if ($generated % 100 -eq 0) { Write-Output "Synthesized $generated additional voice markers..." }
    }
    Write-Output "Synthesized $generated additional Chinese placeholder phrases to local WAV files; nothing played through speakers."
} finally { $speaker.Dispose() }
