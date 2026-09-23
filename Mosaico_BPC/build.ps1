param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$MosaicoPath = $env:MOSAICO_ROOT,
    [string]$BspPath = $env:MOSAICO_BSP_DIR,
    [string]$PythonExe = ''
)
$ErrorActionPreference = 'Stop'
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py'))) {
    throw 'Enter an ESP-IDF 6.1 environment, or provide -IdfPath.'
}
if (-not $PythonExe) {
    if ($env:IDF_PYTHON_ENV_PATH) {
        $PythonExe = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
    } else {
        $PythonExe = (Get-Command python -ErrorAction Stop).Source
    }
}
$env:IDF_PATH = $IdfPath
if ($MosaicoPath) { $env:MOSAICO_ROOT = $MosaicoPath }
if ($BspPath) { $env:MOSAICO_BSP_DIR = $BspPath }
$runner = if ($MosaicoPath) {
    Join-Path $MosaicoPath 'submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py'
} else { '' }
if ($runner -and (Test-Path -LiteralPath $runner)) {
    & $PythonExe $runner --project $PSScriptRoot --idf-path $IdfPath build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    $idfArgs = @('-C', $PSScriptRoot)
    if ($BspPath) { $idfArgs += "-DMOSAICO_BSP_DIR=$BspPath" }
    elseif ($MosaicoPath) { $idfArgs += "-DMOSAICO_ROOT=$MosaicoPath" }
    $idfArgs += 'build'
    & $PythonExe (Join-Path $IdfPath 'tools/idf.py') @idfArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& (Join-Path $PSScriptRoot 'package.ps1') -PythonExe $PythonExe
exit $LASTEXITCODE

