param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Lumetra executable was not found: $Executable"
}

$programs = [Environment]::GetFolderPath('Programs')
$shortcutPath = Join-Path $programs 'Lumetra.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = [IO.Path]::GetFullPath($Executable)
$shortcut.WorkingDirectory = Split-Path -Parent $Executable
$shortcut.Description = 'Lumetra'
$shortcut.Save()
