<#
.SYNOPSIS
    Helpers for driving Illustrator from PowerShell: scripting, the plugin's
    test bridge, Adobe's own Free Distort dialog, and evidence files.

.DESCRIPTION
    Dot-source this file, then:

        . .\tools\ai.ps1
        Send-AiMessage version
        Send-AiMessage 'fd get' '0'

    Pure ASCII on purpose: Windows PowerShell 5.1 reads scripts in the system
    codepage.
#>

$script:BridgePlugin = 'FreeDistortPlus'

function Get-AiApp {
    [CmdletBinding()]
    param([int] $TimeoutSeconds = 180)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        try { return [Runtime.InteropServices.Marshal]::GetActiveObject('Illustrator.Application') }
        catch {
            if ((Get-Date) -ge $deadline) { throw 'Illustrator is not reachable over COM.' }
            Start-Sleep -Seconds 2
        }
    }
}

function Invoke-AiScript {
    [CmdletBinding(DefaultParameterSetName = 'Code')]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Code', Position = 0)]
        [string] $Code,
        [Parameter(Mandatory, ParameterSetName = 'File')]
        [string] $Path
    )

    if ($PSCmdlet.ParameterSetName -eq 'File') {
        $Code = [System.IO.File]::ReadAllText($Path)
    }
    (Get-AiApp).DoJavaScript($Code)
}

function Send-AiMessage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory, Position = 0)] [string] $Selector,
        [Parameter(Position = 1)] [string] $Arguments = '',
        [string] $Plugin = $script:BridgePlugin
    )

    # Both strings travel through ExtendScript source, so quote and backslash
    # have to survive the trip.
    $escape = { param($s) $s -replace '\\', '\\' -replace '"', '\"' }
    $sel = & $escape $Selector
    $arg = & $escape $Arguments
    Invoke-AiScript "app.sendScriptMessage(`"$Plugin`", `"$sel`", `"$arg`");"
}

function Stop-Ai {
    [CmdletBinding()]
    param()

    if (-not (Get-Process Illustrator -ErrorAction SilentlyContinue)) { return 'Illustrator was not running.' }
    # Close documents first so nothing raises a save prompt, then use the Quit
    # menu command, which works where Application.Quit() sometimes does not.
    try { Invoke-AiScript 'while (app.documents.length > 0) { app.documents[0].close(SaveOptions.DONOTSAVECHANGES); }' | Out-Null } catch { }
    try { Invoke-AiScript 'app.executeMenuCommand("quit");' | Out-Null } catch { }

    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Process Illustrator -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    if (Get-Process Illustrator -ErrorAction SilentlyContinue) { throw 'Illustrator did not quit.' }
    'Illustrator stopped.'
}

function Wait-AiReady {
    <#
    .SYNOPSIS
        Waits until Illustrator will run a script, not merely answer COM.
    #>
    [CmdletBinding()]
    param([int] $TimeoutSeconds = 180)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            (Get-AiApp -TimeoutSeconds 10).DoJavaScript('app.documents.length + "";') | Out-Null
            return $true
        }
        catch { Start-Sleep -Seconds 2 }
    }
    return $false
}

function Start-Ai {
    [CmdletBinding()]
    param([string] $Exe = 'C:\Program Files\Adobe\Adobe Illustrator 2026\Support Files\Contents\Windows\Illustrator.exe')

    # Minimized, so a restart for a deploy does not take the foreground from
    # whoever is using the machine. Started in Illustrator's own folder: a
    # process inherits its working directory, and Illustrator's helpers
    # (CEPHtmlEngine, the crash processor) outlive it holding whatever folder
    # the caller was in, which then cannot be renamed.
    if (-not (Get-Process Illustrator -ErrorAction SilentlyContinue)) { Start-Process $Exe -WindowStyle Minimized -WorkingDirectory (Split-Path -Parent $Exe) }
    if (-not (Wait-AiReady)) { throw 'Illustrator started but never became ready to run a script.' }
    'Illustrator running.'
}

function Restart-Ai {
    [CmdletBinding()]
    param()
    Stop-Ai | Out-Null
    Start-Ai
}

function Initialize-AiSession {
    <#
    .SYNOPSIS
        Turns alerts off and makes sure there is a document to work in.
    .DESCRIPTION
        A modal alert blocks the scripting call that raised it, so probes run
        with alerts off. That has a consequence worth knowing for this project:
        with alerts off, asking Adobe's Free Distort to edit its parameters
        does not show its dialog -- it commits at once, exactly as if OK had
        been pressed on an untouched dialog. Invoke-FreeDistortDialog turns
        alerts back on for the one call that needs the real window.
    #>
    [CmdletBinding()]
    param()
    Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "alerts off";' | Out-Null
    # Always send the current fixtures: the engine keeps whatever copy an
    # earlier probe installed, however old.
    Invoke-AiScript -Path (Join-Path $PSScriptRoot 'fixtures.jsx') | Out-Null
    # The probes' own document is opened or created, and made active, in a
    # call of its own. A plugin asked about the selection in the same call
    # that switched documents still sees the previous document, and answers
    # "No selection." for artwork that is plainly selected.
    Invoke-Fdp 'FDP.doc(); app.redraw(); "ready";'
}

function Invoke-Fdp {
    <#
    .SYNOPSIS
        Runs one expression against tools/fixtures.jsx, installing it first if
        the scripting engine does not have it.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory, Position = 0)] [string] $Expression)

    $probe = Invoke-AiScript 'typeof FDP === "undefined" ? "no" : "yes";'
    if ($probe -ne 'yes') { Invoke-AiScript -Path (Join-Path $PSScriptRoot 'fixtures.jsx') | Out-Null }
    Invoke-AiScript $Expression
}

#  Probe results, recorded as data as well as prose.

function Start-ProbeResults {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Probe)
    $script:ProbeName = $Probe
    $script:ProbeRows = New-Object Collections.Generic.List[string]
    $script:ProbeRows.Add("probe`tgroup`tcase`texpected`tobserved`tstatus")
}

function Add-ProbeResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Case,
        [string] $Group = '',
        [string] $Expected = '',
        [string] $Observed = '',
        [Parameter(Mandatory)] [string] $Status
    )
    if (-not $script:ProbeRows) { return }
    $clean = { param($s) ($s -replace "[`t`r`n]+", ' ').Trim() }
    $script:ProbeRows.Add(("{0}`t{1}`t{2}`t{3}`t{4}`t{5}" -f $script:ProbeName,
        (& $clean $Group), (& $clean $Case), (& $clean $Expected), (& $clean $Observed), $Status))
}

function Save-ProbeResults {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Path)
    if (-not $script:ProbeRows) { return }
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path)
    [System.IO.File]::WriteAllLines($Path, (Hide-Personal $script:ProbeRows))
}

function Format-AiNumber {
    <#
    .SYNOPSIS
        Formats a number for ExtendScript or the bridge, decimal point and all,
        whatever the machine's culture says a decimal separator is.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory, Position = 0)] [double] $Value)
    $Value.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
}

#  Keeping the machine out of the evidence. Every transcript and result file
#  is written through Hide-Personal, so a path that arrives from a compiler, a
#  trace, or Windows is redacted as it is written rather than scrubbed later.

function Hide-Personal {
    [CmdletBinding()]
    param([Parameter(Mandatory, Position = 0, ValueFromPipeline)] [AllowNull()] $Text)

    begin {
        $repoRoot = Split-Path -Parent $PSScriptRoot
        $rules = @(
            @{ From = $repoRoot;                                             To = '<repo>' },
            @{ From = [Environment]::GetFolderPath('LocalApplicationData'); To = '<localappdata>' },
            @{ From = [Environment]::GetFolderPath('ApplicationData');      To = '<appdata>' },
            @{ From = [IO.Path]::GetTempPath().TrimEnd('\');                 To = '<temp>' },
            @{ From = $env:USERPROFILE;                                      To = '<user>' },
            @{ From = (Split-Path -Parent $repoRoot);                        To = '<workspace>' }
        ) | Where-Object { $_.From } | Sort-Object { -$_.From.Length }
        $name = $env:USERNAME
    }
    process {
        if ($null -eq $Text) { return $Text }
        $out = foreach ($line in @($Text)) {
            $s = [string] $line
            foreach ($rule in $rules) {
                $s = $s.Replace($rule.From, $rule.To)
                $s = $s.Replace($rule.From.Replace('\', '/'), $rule.To)
            }
            if ($name) { $s = $s -replace ('(?<![A-Za-z0-9])' + [regex]::Escape($name) + '(?![A-Za-z0-9])'), '<user>' }
            # Anything still shaped like an absolute path is redacted whatever
            # it spells: a path that came back through another encoding matches
            # none of the rules above.
            $s = $s -replace '(?<![A-Za-z])[A-Za-z]:[\\/](?![\\/])[^\r\n"'',;]*', '<path>'
            $s
        }
        $out
    }
}

function Save-ProbeTranscript {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [AllowEmptyCollection()] $Lines
    )
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path)
    [System.IO.File]::WriteAllLines($Path, @(Hide-Personal $Lines))
}

function Get-AiAdditionalPluginFolder {
    <#
    .SYNOPSIS
        Illustrator's Additional Plug-ins Folder as its preferences file
        records it, or an empty string when it is not set.
    #>
    [CmdletBinding()]
    param([int] $Generation = 30)

    $roaming = [Environment]::GetFolderPath('ApplicationData')
    $locale = 'en_US'
    $localeFile = Join-Path $roaming ("Adobe\Adobe Illustrator {0} Settings\.locale" -f $Generation)
    if (Test-Path $localeFile) {
        $read = ([IO.File]::ReadAllText($localeFile)).Trim()
        if ($read) { $locale = $read }
    }
    $prefs = Join-Path $roaming ("Adobe\Adobe Illustrator {0} Settings\{1}\x64\Adobe Illustrator Prefs" -f $Generation, $locale)
    if (-not (Test-Path $prefs)) { return '' }

    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $text = $latin1.GetString([IO.File]::ReadAllBytes($prefs))
    $m = [regex]::Match($text, "/plugins \[ (\d+)\r\n(.*?)\r\n\]\r\n", [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $m.Success) { return '' }
    $hex = ($m.Groups[2].Value -replace '[^0-9a-fA-F]', '')
    if ($hex.Length -lt 2) { return '' }
    $bytes = New-Object byte[] ($hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16) }
    [Text.Encoding]::UTF8.GetString($bytes)
}

function Get-VcToolchain {
    <#
    .SYNOPSIS
        cl.exe, MSBuild, and the INCLUDE and LIB a bare compiler call needs.
    #>
    [CmdletBinding()]
    param()

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio 2022 or the Build Tools.' }
    $install = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $install) { throw 'No Visual Studio C++ toolchain found.' }

    $toolsVersion = (Get-Content (Join-Path $install 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
    $vcTools = Join-Path $install ("VC\Tools\MSVC\{0}" -f $toolsVersion)
    $cl = Join-Path $vcTools 'bin\Hostx64\x64\cl.exe'
    if (-not (Test-Path $cl)) { throw "cl.exe not found at $cl" }

    $sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Microsoft SDKs\Windows\v10.0' -ErrorAction SilentlyContinue).InstallationFolder
    if (-not $sdkRoot) { $sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\' }
    $sdkVersion = (Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name

    [pscustomobject]@{
        Version = $toolsVersion
        Cl      = $cl
        MSBuild = (Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe')
        Include = @((Join-Path $vcTools 'include'), (Join-Path $sdkRoot "Include\$sdkVersion\ucrt"),
                    (Join-Path $sdkRoot "Include\$sdkVersion\um"), (Join-Path $sdkRoot "Include\$sdkVersion\shared")) -join ';'
        Lib     = @((Join-Path $vcTools 'lib\x64'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x64"),
                    (Join-Path $sdkRoot "Lib\$sdkVersion\um\x64")) -join ';'
    }
}

function Invoke-FreeDistortDialog {
    <#
    .SYNOPSIS
        Opens Adobe's own Free Distort dialog on the selected object, optionally
        drags one of its corner handles, and closes it with OK or Cancel.
    .DESCRIPTION
        The dialog is modal, so the call that opens it does not return until it
        closes; a background job waits for the window and works it. The job
        drives the dialog with posted window messages, which do not move the
        real mouse pointer or take the keyboard from anyone.

        The dialog is a #32770 titled "Free Distort" holding one Drover view.
        -Drag is x1,y1,x2,y2 in that view's client pixels. -ShotPrefix saves
        PNG captures of the dialog as it opened and after the drag.

        Alerts are switched on for the one call, because with alerts off the
        edit commits without showing anything, and are switched off again.
    .PARAMETER Effect
        Index of the Free Distort among the object's post-effects.
    #>
    [CmdletBinding()]
    param(
        [ValidateSet('ok', 'cancel')] [string] $Button = 'ok',
        [int[]] $Drag = @(),
        [string] $ShotPrefix = '',
        [int] $Effect = 0,
        [int] $TimeoutSeconds = 30
    )

    $driver = Start-Job -FilePath (Join-Path $PSScriptRoot 'dialog-driver.ps1') -ArgumentList $TimeoutSeconds, $Button, $ShotPrefix, $Drag
    Start-Sleep -Milliseconds 1500
    Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DISPLAYALERTS; "on";' | Out-Null
    try { $result = Send-AiMessage 'edit effect' ([string] $Effect) }
    finally { Invoke-AiScript 'app.userInteractionLevel = UserInteractionLevel.DONTDISPLAYALERTS; "off";' | Out-Null }
    $said = @(Receive-Job -Job $driver -Wait)
    Remove-Job $driver -Force
    [pscustomobject]@{ Driver = ($said -join ' | '); Host = ($result -replace "`r?`n", ' ').Trim() }
}
