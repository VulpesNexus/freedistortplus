<#
.SYNOPSIS
    Checks the built plugin itself: both configurations, warnings, identity,
    linkage, and what the binary gives away about the machine that made it.

.DESCRIPTION
    None of this needs Illustrator. It is the part of a release check that is
    about the artifact rather than the behavior: that Release and Debug both
    build clean at the project's warning level, that Release links the retail
    C runtime, that the binary carries this project's identity rather than the
    Adobe SDK sample defaults, that it exports the entry point Illustrator
    looks for, and that it contains no absolute path from the build machine.

    Run it first when preparing a release, from a clean working tree: it
    rebuilds, and every other evidence file has to postdate the record it
    writes (tools/make-release.ps1 checks). Writes docs/evidence/build.txt and
    build.tsv.
#>
[CmdletBinding()]
param([string] $SdkRoot = $env:AI_SDK_ROOT)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')

$repo = Split-Path -Parent $PSScriptRoot
$outPath = Join-Path $repo 'docs\evidence\build.txt'
if (-not $SdkRoot) { throw 'Set AI_SDK_ROOT, or pass -SdkRoot, to point at your copy of the Adobe Illustrator 2026 SDK.' }

$log = New-Object Collections.Generic.List[string]
function Note([string] $line) { $log.Add($line); Write-Output $line }
$script:failed = 0
function Check([string] $name, [bool] $ok, [string] $detail) {
    if (-not $ok) { $script:failed++ }
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    Note ("[{0}] {1}: {2}" -f $status, $name, $detail)
    Add-ProbeResult -Group 'release build' -Case $name -Expected 'the artifact is fit to ship' -Observed $detail -Status $status
}

Start-ProbeResults -Probe 'build'
$toolchain = Get-VcToolchain
Note ('FreeDistort+ -- the built artifact, {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Note ("Toolchain: MSVC {0}; Adobe Illustrator 2026 SDK" -f $toolchain.Version)

# Which source this artifact came from. A binary nobody can trace to a commit
# cannot be rebuilt or explained.
$commit = (& git -C $repo rev-parse HEAD 2>$null)
$dirty = @(& git -C $repo status --porcelain 2>$null)
Note ("Commit:    {0}{1}" -f $commit, $(if ($dirty.Count) { ' (working tree not clean)' } else { '' }))

foreach ($configuration in @('Debug', 'Release')) {
    $output = & $toolchain.MSBuild (Join-Path $repo 'plugin\FreeDistortPlus.vcxproj') "/p:Configuration=$configuration" '/p:Platform=x64' '/v:minimal' '/nologo' '/nodeReuse:false' "/p:AISDKRoot=$SdkRoot" '/t:Rebuild' 2>&1
    $warnings = @($output | Where-Object { $_ -match ': warning ' })
    $errors = @($output | Where-Object { $_ -match ': error ' })
    Check ("{0} builds with no warnings and no errors" -f $configuration) (($LASTEXITCODE -eq 0) -and ($warnings.Count -eq 0) -and ($errors.Count -eq 0)) ("{0} warnings, {1} errors" -f $warnings.Count, $errors.Count)
    foreach ($w in ($warnings + $errors | Select-Object -First 5)) { Note ('    ' + (Hide-Personal ([string] $w))) }
}

# Release last, so the binary on disk is the one inspected below.
$binary = Join-Path $repo 'build\Release\FreeDistortPlus.aip'
Check 'the Release build produced a plugin' (Test-Path $binary) 'build\Release\FreeDistortPlus.aip'
if (-not (Test-Path $binary)) {
    Save-ProbeResults -Path ($outPath -replace '\.txt$', '.tsv')
    Save-ProbeTranscript -Path $outPath -Lines $log
    throw 'No Release binary to inspect.'
}

$info = (Get-Item $binary).VersionInfo
Note ("File version: {0}; product {1}; company {2}" -f $info.FileVersion, $info.ProductName, $info.CompanyName)
Note ("Size:         {0} bytes" -f (Get-Item $binary).Length)
Note ("SHA-256:      {0}" -f (Get-FileHash $binary -Algorithm SHA256).Hash)

Check 'the binary does not claim Adobe as its publisher' ($info.CompanyName -notmatch 'Adobe') ("CompanyName is {0}" -f $info.CompanyName)
Check 'the binary names its own product' ($info.ProductName -eq 'FreeDistort+') ("ProductName is {0}" -f $info.ProductName)
Check 'the binary carries a release version, with no development suffix' ($info.FileVersion -match '^\d+\.\d+\.\d+(-rc\.\d+)?$') ("FileVersion is {0}" -f $info.FileVersion)

$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary))
$runtimes = @([regex]::Matches($ascii, '(?i)MSVCP\d+D?\.dll|VCRUNTIME\d+D?\.dll|ucrtbased?\.dll') | ForEach-Object { $_.Value.ToUpper() } | Sort-Object -Unique)
$debugRuntime = @($runtimes | Where-Object { $_ -match 'D\.DLL$' })
Check 'Release links the retail C runtime, not the debug one' ($debugRuntime.Count -eq 0) ("links {0}" -f ($runtimes -join ', '))

# Derived from where this is rather than from a list of names, so it keeps
# working on another machine.
$patterns = @('[A-Za-z]:\\Users[ -~]{0,100}', '[A-Za-z]:\\Documents and Settings[ -~]{0,100}')
foreach ($secret in @($repo, $env:USERPROFILE, (Split-Path -Parent $repo))) {
    if ($secret) { $patterns += [regex]::Escape($secret) + '[ -~]{0,100}' }
}
if ($env:USERNAME -and $env:USERNAME.Length -gt 2) {
    $patterns += '(?<![A-Za-z0-9])' + [regex]::Escape($env:USERNAME) + '(?![A-Za-z0-9])'
}
$leaks = [regex]::Matches($ascii, ($patterns -join '|'))
Check 'no path from the build machine is embedded' ($leaks.Count -eq 0) $(if ($leaks.Count -eq 0) { 'none found' } else { '{0} found' -f $leaks.Count })

$pdbNames = @([regex]::Matches($ascii, '[ -~]{0,80}\.pdb') | ForEach-Object { $_.Value } | Sort-Object -Unique)
Check 'the symbol reference is a bare file name' (($pdbNames.Count -eq 1) -and ($pdbNames[0] -eq 'FreeDistortPlus.pdb')) ("symbol references: {0}" -f (Hide-Personal ($pdbNames -join ', ')))

Check 'the entry point Illustrator looks for is present' ($ascii -match 'PluginMain') 'PluginMain'
Check 'the effect it edits is named as Illustrator registers it' ($ascii -match 'Adobe Free Distort') 'Adobe Free Distort'
Check 'the tool carries the publisher prefix' ($ascii -match 'VulpesNexus FreeDistort\+ Tool') 'VulpesNexus FreeDistort+ Tool'

$dumpbin = Join-Path (Split-Path -Parent $toolchain.Cl) 'dumpbin.exe'
if (Test-Path $dumpbin) {
    $ours = @((& $dumpbin /dependents $binary) -match '^\s+\S+\.dll\s*$' | ForEach-Object { $_.Trim() })
    Note ("Depends on: {0}" -f ($ours -join ', '))
    $system = '^(KERNEL32|USER32|GDI32|COMCTL32|ADVAPI32|SHELL32|OLE32|OLEAUT32|SHLWAPI|COMDLG32|UxTheme|dwmapi|IMM32)\.dll$'
    $unexpected = @($ours | Where-Object { $_ -notmatch $system -and $_ -notmatch '^api-ms-win-crt-' -and $_ -notmatch '^(MSVCP140|VCRUNTIME140)(_\w+)?\.dll$' })
    Check 'nothing is linked but Windows and the C runtime' ($unexpected.Count -eq 0) $(if ($unexpected.Count -eq 0) { 'no SDK, developer, or test-harness DLL is named' } else { 'unexpected: ' + ($unexpected -join ', ') })

    $hostExe = 'C:\Program Files\Adobe\Adobe Illustrator 2026\Support Files\Contents\Windows\Illustrator.exe'
    if (Test-Path $hostExe) {
        $needed = @($ours | Where-Object { $_ -match '^(MSVCP140|VCRUNTIME140)(_\w+)?\.dll$' })
        $hostNeeds = @((& $dumpbin /dependents $hostExe) -match '^\s+\S+\.dll\s*$' | ForEach-Object { $_.Trim() })
        $unmet = @($needed | Where-Object { $hostNeeds -notcontains $_ })
        Check 'the C runtime it needs is one Illustrator already needs' ($unmet.Count -eq 0) $(if ($unmet.Count -eq 0) { 'Illustrator.exe names the same {0}' -f ($needed -join ', ') } else { 'Illustrator does not name: ' + ($unmet -join ', ') })
    }
}

Check 'the working tree was clean when it was built' ($dirty.Count -eq 0) ("{0} uncommitted change(s)" -f $dirty.Count)

Save-ProbeResults -Path ($outPath -replace '\.txt$', '.tsv')
Save-ProbeTranscript -Path $outPath -Lines $log
if ($script:failed -gt 0) { throw ("{0} build check(s) failed." -f $script:failed) }
