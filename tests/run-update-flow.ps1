param(
    [Parameter(Mandatory)][string]$TestExe,
    [Parameter(Mandatory)][string]$PayloadExe,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$TestExe = (Resolve-Path -LiteralPath $TestExe).Path
$PayloadExe = (Resolve-Path -LiteralPath $PayloadExe).Path
$root = Join-Path $OutputDirectory ('update-qa-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$compat = $env:__COMPAT_LAYER
function FileDigest([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $file = [IO.File]::OpenRead($Path)
    try { return [Convert]::ToBase64String($hash.ComputeHash($file)) }
    finally { $file.Dispose(); $hash.Dispose() }
}
try {
    $env:__COMPAT_LAYER = 'RunAsInvoker'
    foreach ($mode in @('flow', 'install')) {
        $dir = Join-Path $root $mode
        New-Item -ItemType Directory -Path $dir | Out-Null
        $exe = Join-Path $dir 'update-flow.exe'
        $payload = Join-Path $dir 'next.bin'
        Copy-Item -LiteralPath $TestExe -Destination $exe
        Copy-Item -LiteralPath $PayloadExe -Destination $payload
        # PE overlays are valid; padding makes intermediate progress observable.
        $stream = [IO.File]::OpenWrite($payload)
        try { if ($stream.Length -lt 524288) { $stream.SetLength(524288) } } finally { $stream.Dispose() }
        $ini = Join-Path $dir 'color-picker.ini'
        Set-Content -LiteralPath $ini -Encoding unicode "[sentinel]`r`nkeep=portable-settings"
        New-Item -ItemType Directory -Path (Join-Path $dir '_au_tmp') | Out-Null
        Set-Content -LiteralPath (Join-Path $dir '_au_tmp/unrelated.txt') 'keep'
        if ($mode -eq 'install') { & $exe $dir --install } else { & $exe $dir }
        if ($LASTEXITCODE -ne 0) { throw "$mode integration checks failed: $LASTEXITCODE" }
        if ($mode -eq 'install') {
            $deadline = [DateTime]::UtcNow.AddSeconds(10)
            $marker = Join-Path $dir 'restarted.txt'
            while (!(Test-Path -LiteralPath $marker) -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 100
            }
            if (!(Test-Path -LiteralPath $marker)) { throw 'Updated EXE did not restart' }
            if ((FileDigest $exe) -ne (FileDigest $payload)) {
                throw 'Installed EXE differs from the verified download'
            }
            if ((Test-Path -LiteralPath ($exe + '.old')) -or (Test-Path -LiteralPath ($exe + '.autmp'))) {
                throw 'Update left temporary files'
            }
            Write-Output 'PASS real EXE replacement, restart, predecessor wait, and cleanup'
        }
        if ((Get-Content -Raw -LiteralPath $ini) -notmatch 'keep=portable-settings') { throw 'INI was not preserved' }
        if (!(Test-Path -LiteralPath (Join-Path $dir '_au_tmp/unrelated.txt'))) { throw 'Unrelated folder changed' }
        Write-Output 'PASS portable INI and unrelated files preserved'
    }
    Write-Output "Screenshots and logs: $root"
} finally {
    $env:__COMPAT_LAYER = $compat
}
