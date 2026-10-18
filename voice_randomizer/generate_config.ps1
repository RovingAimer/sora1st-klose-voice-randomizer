$ErrorActionPreference = 'Stop'
# The editor and Windows Profile API consume UTF-16 LE with BOM.
# Keep only the readable UTF-8 template in Git.
$configSource = Join-Path $PSScriptRoot 'release\config_template.ini'
$configOutput = Join-Path $PSScriptRoot 'release\voice_randomizer.ini'
$configText = [IO.File]::ReadAllText($configSource, [Text.Encoding]::UTF8)
$configText = ($configText -replace "`r`n", "`n").TrimEnd() + "`n"
$configText = $configText -replace "`n", "`r`n"
[IO.File]::WriteAllText($configOutput, $configText, [Text.Encoding]::Unicode)
