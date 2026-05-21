param(
    [string]$Version = "0.0.0-local"
)
$ErrorActionPreference = 'Stop'

$repo = Resolve-Path "$PSScriptRoot\.."
Set-Location $repo

$dist = "dist\OneKeyInput"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -Force -ItemType Directory $dist | Out-Null

Copy-Item build\release\bin\onekey-core.exe                          $dist
Copy-Item build\release\bin\Microsoft.CognitiveServices.Speech.*.dll $dist
Copy-Item settings\src-tauri\target\release\onekey-settings.exe      $dist
Copy-Item config.example.json                                        $dist

if (Test-Path dist-readme.txt) {
    Copy-Item dist-readme.txt "$dist\README.txt"
} else {
    "See https://github.com/zwcih/one-key-input for usage." | Out-File "$dist\README.txt" -Encoding utf8
}

$zip = "dist\OneKeyInput-$Version-portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$dist\*" -DestinationPath $zip -Force

Get-Item $zip | Select-Object Length, FullName
Get-ChildItem $dist | Select-Object Name, Length
