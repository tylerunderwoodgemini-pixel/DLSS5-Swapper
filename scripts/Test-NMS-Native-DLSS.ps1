param(
    [string]$GameDir = "D:\GOG Galaxy\Games\No Man's Sky\Binaries",
    [switch]$Apply,
    [string]$RestoreFrom = ''
)
$ErrorActionPreference = 'Stop'
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'NMS.exe') -PathType Leaf)) { throw 'Select the Binaries folder containing NMS.exe.' }
if (Get-Process -Name NMS -ErrorAction SilentlyContinue) { throw 'Close No Man''s Sky before changing its configuration.' }
$names = @('dlss5-feed.cfg','dlss5-bridge.cfg')
if ($RestoreFrom) {
    $record = Get-Content -LiteralPath (Join-Path $RestoreFrom 'native-test.json') -Raw | ConvertFrom-Json
    if ($record.gameDir -ne $GameDir) { throw 'The backup belongs to a different game folder.' }
    foreach ($name in $names) {
        if (-not (Test-Path -LiteralPath (Join-Path $RestoreFrom $name) -PathType Leaf)) { throw "Backup missing: $name" }
    }
    foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $RestoreFrom $name) -Destination (Join-Path $GameDir $name) -Force }
    Write-Host 'Original Feeder and bridge settings restored.'
    return
}
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'dlss5-bridge.addon64') -PathType Leaf)) { throw 'This test requires the existing native Vulkan mirror bridge; it does not install one.' }
function Set-Key([string]$Text,[string]$Key,[string]$Value) {
    $pattern = '(?im)^\s*' + [regex]::Escape($Key) + '\s*=.*(?:\r?\n|$)'
    # Remove duplicate keys too: only one authoritative value remains.
    $clean = [regex]::Replace($Text,$pattern,'')
    return $clean.TrimEnd("`r","`n") + "`r`n$Key=$Value`r`n"
}
$changes = @{}
foreach ($name in $names) {
    $path = Join-Path $GameDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required existing config missing: $name" }
    $changes[$name] = [IO.File]::ReadAllText($path)
}
$changes['dlss5-feed.cfg'] = Set-Key $changes['dlss5-feed.cfg'] 'enabled' '0'
foreach ($pair in @(@('vk_mirror','1'),@('synth','0'),@('source','mirror'),@('stage','3'),@('mode','2'),@('unwrap','0'))) {
    $changes['dlss5-bridge.cfg'] = Set-Key $changes['dlss5-bridge.cfg'] $pair[0] $pair[1]
}
Write-Host 'Native-only diagnostic: Feeder processing disabled; Vulkan native mirror enabled; bridge uses wrapped D3D12 device (unwrap=0).'
Write-Host 'The game''s own DLSS/DLAA setting, DFC controls and addon binaries are preserved. This is a diagnostic, not a proven crash fix.'
if (-not $Apply) { Write-Host 'Preview only. Run again with -Apply after reinstalling from the local swapper.'; return }
$backup = Join-Path $GameDir ('_NMS_Native_Test_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $GameDir $name) -Destination (Join-Path $backup $name) }
@{gameDir=$GameDir;date=[DateTime]::UtcNow.ToString('o');files=$names} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'native-test.json') -Encoding utf8
try {
    foreach ($name in $names) { [IO.File]::WriteAllText((Join-Path $GameDir $name),$changes[$name],(New-Object Text.UTF8Encoding($false))) }
} catch {
    foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (Join-Path $GameDir $name) -Force }
    throw
}
Write-Host "Applied. Backup: $backup"
Write-Host 'Launch your usual OpenVR shortcut. Then close the game and inspect dlss5-bridge.log and deep-fried-chicken.log.'
Write-Host 'Success requires the native mirror feature to build and evaluate; native Vulkan calls alone do not prove DFC has run.'
