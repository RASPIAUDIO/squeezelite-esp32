param([string]$BuildDir = 'build-s31')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
if (-not $env:IDF_PATH) { throw 'Activate the pinned ESP-IDF environment first (see docs/BUILD.md).' }
if ($env:SPOTIFY_SECRET) { throw 'Unset SPOTIFY_SECRET: release builds must not embed personal credentials.' }
Push-Location $ProjectRoot
try {
    python "$PSScriptRoot/prepare_sdk.py"
    if ($LASTEXITCODE -ne 0) { throw 'SDK preparation failed' }
    python -m grpc_tools.protoc --version
    if ($LASTEXITCODE -ne 0) { throw 'Install scripts/requirements-build.txt in the IDF Python environment.' }
    $env:PROJECT_VER = (Get-Content "$ProjectRoot/version.txt" -Raw).Trim()
    python "$env:IDF_PATH/tools/idf.py" -B $BuildDir -D SDKCONFIG=sdkconfig.sendspin -D IDF_TARGET=esp32s31 -D DEPTH=32 "-DPROTOBUF_PROTOC_EXECUTABLE=$PSScriptRoot/protoc.bat" build
    if ($LASTEXITCODE -ne 0) { throw "Firmware build failed: $LASTEXITCODE" }
} finally { Pop-Location }

