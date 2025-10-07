# Loads Visual Studio Build Tools developer variables into the current PowerShell session
# Just run: .\Enter-VSDev.ps1

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Install Visual Studio Build Tools or Visual Studio Installer."
    exit 1
}

$installPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $installPath) {
    Write-Error "Visual Studio Build Tools not found."
    exit 1
}

$vsDevCmd = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) {
    Write-Error "VsDevCmd.bat not found at $vsDevCmd."
    exit 1
}

$tmp = [System.IO.Path]::GetTempFileName()
try {
    # Run VsDevCmd.bat and dump environment variables into temp file
    & cmd.exe /c "`"$vsDevCmd`" -no_logo -arch=x64 && set > `"$tmp`""

    # Import all environment variables into current PowerShell session
    Get-Content -LiteralPath $tmp | ForEach-Object {
        if ($_ -match '^(?<name>[^=]+)=(?<value>.*)$') {
            Set-Item -Path "Env:$($Matches['name'])" -Value $Matches['value'] -Force
        }
    }

    Write-Host "✅ Visual Studio Build Tools environment loaded for x64."
    Write-Host "You can now run 'cl', 'msbuild', 'nmake', etc."
}
finally {
    Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
}
