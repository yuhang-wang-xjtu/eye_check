param([string]$Version, [string]$WindowsBuildDirectory)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$versionHeader=Get-Content -LiteralPath (Join-Path $repoRoot 'src\app_version.h') -Raw
if(-not $Version) { $Version=[regex]::Match($versionHeader,'EYEBREAK_VERSION_TEXT "([0-9]+\.[0-9]+\.[0-9]+)"').Groups[1].Value }
if($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version' }
if(-not $WindowsBuildDirectory) { $WindowsBuildDirectory=Join-Path $repoRoot "out\build-v$Version\Release" }
$exe=Join-Path $WindowsBuildDirectory 'EyeBreak.exe'
if([Diagnostics.FileVersionInfo]::GetVersionInfo($exe).ProductVersion -ne $Version) { throw 'Build the matching Windows version before packaging' }
$apk=Join-Path $repoRoot 'android\app\build\outputs\apk\debug\app-debug.apk'
$metadata=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $apk) 'output-metadata.json') -Raw | ConvertFrom-Json
if($metadata.elements[0].versionName -ne $Version) { throw 'Build the matching Android version before packaging' }
$releaseRoot=Join-Path $repoRoot "out\releases\v$Version"
$windowsRoot=Join-Path $releaseRoot 'windows'
New-Item -ItemType Directory -Force -Path $windowsRoot | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $windowsRoot 'EyeBreak.exe')
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination (Join-Path $windowsRoot 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\nlohmann\LICENSE.MIT') -Destination (Join-Path $windowsRoot 'nlohmann-json-LICENSE.txt')
Compress-Archive -LiteralPath (Join-Path $windowsRoot 'EyeBreak.exe'),(Join-Path $windowsRoot 'README.md'),(Join-Path $windowsRoot 'nlohmann-json-LICENSE.txt') -DestinationPath (Join-Path $releaseRoot "EyeBreak-Windows-x64-v$Version.zip") -Force
Copy-Item -LiteralPath $apk -Destination (Join-Path $releaseRoot "EyeBreak-Android-v$Version.apk")
Compress-Archive -LiteralPath (Join-Path $repoRoot 'sync\server.py'),(Join-Path $repoRoot 'sync\test_server.py'),(Join-Path $repoRoot 'sync\README.md'),(Join-Path $repoRoot 'sync\deploy') -DestinationPath (Join-Path $releaseRoot "EyeBreak-Sync-Prototype-v$Version.zip") -Force
$assets=Get-ChildItem -LiteralPath $releaseRoot -File | Where-Object {$_.Extension -in '.apk','.zip'}
$lines=$assets | ForEach-Object {(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLower()+'  '+$_.Name}
[IO.File]::WriteAllLines((Join-Path $releaseRoot 'SHA256SUMS.txt'),$lines,[Text.UTF8Encoding]::new($false))
Write-Output "Release packaged: $releaseRoot"
