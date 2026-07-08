$ErrorActionPreference = "Stop"

Set-Location -LiteralPath $PSScriptRoot

$node = "C:\Users\ecydm\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe"
if (-not (Test-Path -LiteralPath $node)) {
    Write-Host "Node runtime not found: $node" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

$ak = Read-Host "Huawei Cloud Access Key Id"
$secureSk = Read-Host "Huawei Cloud Secret Access Key" -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureSk)
try {
    $env:HUAWEI_AK = $ak
    $env:HUAWEI_SK = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    $env:HUAWEI_DERIVED_AUTH_SERVICE_NAME = "iotda"

    Write-Host ""
    Write-Host "Starting Huawei Cloud proxy on http://127.0.0.1:8790" -ForegroundColor Cyan
    Write-Host "Keep this window open while the frontend is running." -ForegroundColor Yellow
    & $node server.js
} finally {
    if ($bstr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
    Remove-Item Env:HUAWEI_AK -ErrorAction SilentlyContinue
    Remove-Item Env:HUAWEI_SK -ErrorAction SilentlyContinue
    Remove-Item Env:HUAWEI_DERIVED_AUTH_SERVICE_NAME -ErrorAction SilentlyContinue
}
