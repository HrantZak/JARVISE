param([string]$Model = 'deepseek-v4-flash')
$ErrorActionPreference = 'Stop'
$jarvisRoot = Split-Path -Parent $PSScriptRoot
$jarvisExe = Join-Path $jarvisRoot 'build\msvc-release\bin\JARVIS.exe'
if (-not (Test-Path -LiteralPath $jarvisExe)) {
    throw 'Build JARVIS first using scripts\build.bat release.'
}
if (Get-Process -Name JARVIS -ErrorAction SilentlyContinue) {
    throw 'Close the running JARVIS before starting DeepSeek mode.'
}
$previousKey = $env:DEEPSEEK_API_KEY
$previousModel = $env:JARVIS_DEEPSEEK_MODEL
try {
    if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) {
        $secret = Read-Host 'DeepSeek API key (from platform.deepseek.com/api_keys)' -AsSecureString
        $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
        try { $env:DEEPSEEK_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer).Trim() }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer); $secret.Dispose() }
    }
    if ([string]::IsNullOrWhiteSpace($env:DEEPSEEK_API_KEY)) { throw 'API key is required.' }
    $env:JARVIS_DEEPSEEK_MODEL = $Model
    Start-Process -FilePath $jarvisExe -WorkingDirectory $jarvisRoot -WindowStyle Hidden
} finally {
    $env:DEEPSEEK_API_KEY = $previousKey
    $env:JARVIS_DEEPSEEK_MODEL = $previousModel
}
