<#
.SYNOPSIS
    Assembles the distribution archive from the binary that was tested.

.DESCRIPTION
    Produces dist\FreeDistortPlus-<version>.zip containing the plugin, INSTALL.txt,
    the README, the license texts, and the release notes, and leaves the symbol
    file beside it in dist\symbols rather than inside the archive.

    It does not build. The order for a release is: tools\probe-build.ps1 from a
    clean tree, tools\install.ps1, the host suite (tools\run-suite.ps1) and the
    checks made by hand, then this. Packing a fresh build would ship a binary
    nobody tested: MSVC stamps a link time, so the same source built twice is
    two different files.

    Nothing is published.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $repo 'dist'
$binary = Join-Path $repo 'build\Release\FreeDistortPlus.aip'
$symbols = Join-Path $repo 'build\Release\FreeDistortPlus.pdb'
$evidence = Join-Path $repo 'docs\evidence'
if (-not (Test-Path $binary)) { throw 'No Release binary. Run tools\probe-build.ps1 first.' }

# --- identity ------------------------------------------------------------
$info = (Get-Item $binary).VersionInfo
$version = $info.FileVersion
$hash = (Get-FileHash $binary -Algorithm SHA256).Hash
Write-Output ("Version:   {0}" -f $version)
Write-Output ("Product:   {0} ({1})" -f $info.ProductName, $info.CompanyName)
Write-Output ("SHA-256:   {0}" -f $hash)
if ($info.CompanyName -match 'Adobe') { throw 'The binary claims Adobe as its publisher: it carries the SDK sample version resource.' }
if ($version -notmatch '^\d+\.\d+\.\d+(-rc\.\d+)?$') { throw "The binary's version is not a release version: $version" }

# --- this is the binary the build record and the host describe -----------
$buildRecord = Join-Path $evidence 'build.txt'
if (-not (Test-Path $buildRecord)) { throw 'No docs\evidence\build.txt. Run tools\probe-build.ps1 first.' }
if (-not ([IO.File]::ReadAllText($buildRecord) -match [regex]::Escape($hash))) {
    throw 'The binary is not the one docs\evidence\build.txt records. It was rebuilt after the build probe; run the probe and the suite again.'
}
$installed = $null
$folder = Get-AiAdditionalPluginFolder
if ($folder) { $installed = Join-Path $folder 'FreeDistortPlus.aip' }
if ($installed -and (Test-Path $installed) -and (Get-FileHash $installed -Algorithm SHA256).Hash -ne $hash) {
    throw 'The installed FreeDistortPlus.aip is not this binary, so the host evidence describes a different one.'
}

# --- no developer paths ---------------------------------------------------
$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary))
$patterns = @('[A-Za-z]:\\Users[ -~]{0,120}', '[A-Za-z]:\\Documents and Settings[ -~]{0,120}')
foreach ($secret in @($repo, $env:USERPROFILE, (Split-Path -Parent $repo))) {
    if ($secret) { $patterns += [regex]::Escape($secret) + '[ -~]{0,120}' }
}
# The account name only as a whole word: a short one can be the start of an
# ordinary English word.
if ($env:USERNAME -and $env:USERNAME.Length -gt 2) {
    $patterns += '(?<![A-Za-z0-9])' + [regex]::Escape($env:USERNAME) + '(?![A-Za-z0-9])'
}
if ([regex]::Matches($ascii, ($patterns -join '|')).Count -gt 0) { throw 'The binary contains an absolute path from the build machine.' }
Write-Output 'No build-machine paths in the binary.'

# --- the evidence describes this binary -----------------------------------
#
# Every result, transcript, and screenshot has to be newer than the build
# record, which is written when the binary is built. A probe that dies partway
# leaves its previous results behind, and the matrix would then quote rows
# measured against another binary as though they described this one.
#
# A file is exempt only if it does not describe this plugin at all. Being slow
# or made by hand is a reason to run it, not a reason to excuse it.
# docs\evidence\history\ is not examined: its files carry their date in their
# name and nothing cites them as current.
$exempt = @(
    'build.txt', 'build.tsv',           # the build record itself
    'distort-strings.txt',              # strings in Adobe's own Distort.aip
    'free-transform-points.tsv'         # what Adobe's Free Transform tool did, recorded by hand
)
$builtAt = (Get-Item $buildRecord).LastWriteTimeUtc
$stale = @(Get-ChildItem $evidence -File | Where-Object {
    @('.tsv', '.txt', '.png') -contains $_.Extension.ToLower() -and $_.LastWriteTimeUtc -lt $builtAt -and $exempt -notcontains $_.Name
})
if ($stale.Count -gt 0) {
    $stale | ForEach-Object { Write-Output ("  stale: {0}  written {1}" -f $_.Name, $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm')) }
    throw ("{0} evidence file(s) predate the build record, so they describe a binary this archive does not contain. Run those probes again." -f $stale.Count)
}
$failed = @()
foreach ($file in Get-ChildItem (Join-Path $evidence '*.tsv')) {
    $rows = @(Import-Csv -Path $file.FullName -Delimiter "`t")
    if ($rows.Count -and ($rows[0].PSObject.Properties.Name -contains 'status')) {
        $failed += @($rows | Where-Object { $_.status -eq 'FAIL' } | ForEach-Object { '{0}: {1}' -f $file.BaseName, $_.case })
    }
}
if ($failed.Count -gt 0) {
    $failed | Select-Object -First 10 | ForEach-Object { Write-Output ("  FAIL " + $_) }
    throw ("{0} check(s) failed against this binary." -f $failed.Count)
}
Write-Output 'Evidence checked: every result postdates the build, and none failed.'

# --- assemble -------------------------------------------------------------
$name = 'FreeDistortPlus-' + $version
$stage = Join-Path $dist $name
if (Test-Path $stage) { [IO.Directory]::Delete($stage, $true) }
$null = New-Item -ItemType Directory -Force -Path $stage
$null = New-Item -ItemType Directory -Force -Path (Join-Path $dist 'symbols')

[IO.File]::Copy($binary, (Join-Path $stage 'FreeDistortPlus.aip'))
# LICENSE-EXCEPTION travels with the binary: it is the permission that lets a
# GPL binary built partly from Adobe's sample framework be distributed at all.
foreach ($doc in @('README.md', 'LICENSE', 'LICENSE-EXCEPTION', 'RELEASE_NOTES.md')) {
    [IO.File]::Copy((Join-Path $repo $doc), (Join-Path $stage $doc))
}
# INSTALL.txt is the file a downloader reads first, and it says not to
# double-click the .aip before it says anything else.
$installText = [IO.File]::ReadAllText((Join-Path $repo 'packaging\INSTALL.txt')).Replace('{VERSION}', $version)
[IO.File]::WriteAllText((Join-Path $stage 'INSTALL.txt'), $installText, (New-Object Text.UTF8Encoding($false)))

$archive = Join-Path $dist ($name + '.zip')
if (Test-Path $archive) { [IO.File]::Delete($archive) }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
# The symbol file records the absolute paths of the machine that built it,
# which is what makes it useful for a crash dump and why it stays out.
if (Test-Path $symbols) { [IO.File]::Copy($symbols, (Join-Path $dist ("symbols\" + $name + '.pdb')), $true) }

Write-Output ''
Write-Output ("Archive:  dist\{0}.zip ({1:N0} bytes)" -f $name, (Get-Item $archive).Length)
Get-ChildItem $stage | ForEach-Object { Write-Output ("  {0,-24} {1,10:N0} bytes" -f $_.Name, $_.Length) }
Write-Output 'Nothing has been published.'
