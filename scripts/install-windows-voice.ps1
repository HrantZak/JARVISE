# Windows PowerShell 5.1 compatible, ASCII source.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$logFolder = Join-Path $root 'build\windows-voice'
New-Item -ItemType Directory -Path $logFolder -Force | Out-Null
$statusPath = Join-Path $logFolder 'install-status.json'
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script as administrator to install the Russian Windows voice.'
    }
    @{ state = 'installing' } | ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
    Start-Transcript -Path (Join-Path $logFolder 'install.log') -Force | Out-Null
    foreach ($name in @('Language.Basic~~~ru-RU~0.0.1.0', 'Language.TextToSpeech~~~ru-RU~0.0.1.0')) {
        $capability = Get-WindowsCapability -Online -Name $name
        if ($capability.State -ne 'Installed') {
            Add-WindowsCapability -Online -Name $name | Format-List
        }
        if ((Get-WindowsCapability -Online -Name $name).State -ne 'Installed') {
            throw "Windows did not finish installing $name"
        }
    }
    @{ state = 'installed'; message = 'Restart JARVIS to use the Russian Windows voice.' } |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
    Stop-Transcript | Out-Null
} catch {
    @{ state = 'failed'; message = $_.Exception.Message } |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
    Write-Error $_
    exit 1
}
