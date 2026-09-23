param([string]$PythonExe = 'python')
$ErrorActionPreference = 'Stop'
$buildDir = Join-Path $PSScriptRoot 'build'
$manifest = Get-Content -LiteralPath (Join-Path $buildDir 'flasher_args.json') -Raw | ConvertFrom-Json
$mergeArgs = @('-m', 'esptool', '--chip', $manifest.extra_esptool_args.chip,
    'merge-bin', '--output', (Join-Path $buildDir 'bpc_clock_full.bin'), '--target-offset', '0x0')
$mergeArgs += @($manifest.write_flash_args)
foreach ($entry in $manifest.flash_files.PSObject.Properties) {
    $mergeArgs += @($entry.Name, (Join-Path $buildDir $entry.Value))
}
& $PythonExe @mergeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'FLASHING.md') -Destination (Join-Path $buildDir 'FLASHING.md')
$hashes = Get-ChildItem -LiteralPath $buildDir -Filter 'bpc_clock*.bin' | Get-FileHash -Algorithm SHA256
$hashes | ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(), (Split-Path $_.Path -Leaf) } |
    Set-Content -LiteralPath (Join-Path $buildDir 'SHA256SUMS.txt') -Encoding ascii
exit 0
