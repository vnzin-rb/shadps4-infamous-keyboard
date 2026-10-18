param([string]$GamePath, [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$taskExecutable = Join-Path $PSScriptRoot 'shadPS4.exe'
if (-not (Test-Path -LiteralPath $taskExecutable -PathType Leaf)) {
    throw 'shadPS4.exe nao encontrado. Extraia a pasta inteira do ZIP.'
}
Push-Location -LiteralPath $PSScriptRoot
try {
    if ($VerifyOnly) {
        & $taskExecutable --help
        exit $LASTEXITCODE
    }
    if (-not $GamePath) {
        Add-Type -AssemblyName System.Windows.Forms
        $taskDialog = New-Object System.Windows.Forms.OpenFileDialog
        $taskDialog.Title = 'Selecione o eboot.bin do seu jogo'
        $taskDialog.Filter = 'Executavel do jogo (eboot.bin)|eboot.bin|Todos os arquivos (*.*)|*.*'
        $taskDialog.CheckFileExists = $true
        if ($taskDialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
        $GamePath = $taskDialog.FileName
        $taskDialog.Dispose()
    }
    $taskGame = Get-Item -LiteralPath $GamePath
    if ($taskGame.PSIsContainer) { throw 'Selecione o arquivo eboot.bin, nao a pasta.' }
    & $taskExecutable -g $taskGame.FullName
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
