<#
.SYNOPSIS
    Measures what Adobe Free Distort does to a point: writes a known source
    rectangle and destination quad, expands a copy of the result, and records
    every anchor and handle before and after.

.DESCRIPTION
    tools/solve-mapping.py fits candidate models (affine, homography, bilinear
    with and without the input-bounds renormalization) to what this records.

    Needs Illustrator running with EnhancedFreeDistort.aip loaded. Writes
    docs/evidence/mapping.tsv.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ai.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $repo 'docs\evidence'

# case, fixture, source l,t,r,b, destination (8)
$cases = @(
    @('grid-source-is-bounds',     'grid',   '100,300,400,100', '130,350,450,290,70,80,370,130'),
    @('grid-parallelogram',        'grid',   '100,300,400,100', '140,300,440,300,60,100,360,100'),
    @('grid-trapezoid',            'grid',   '100,300,400,100', '160,300,340,300,100,100,400,100'),
    @('grid-bow-tie',              'grid',   '100,300,400,100', '400,300,100,300,100,100,400,100'),
    @('curves-source-is-bounds',   'curves', '98.0618520421503,300,406.170447970364,94.0192378864667', '130,350,450,290,70,80,370,130'),
    @('curves-source-not-bounds',  'curves', '120,280,380,110', '130,350,450,290,70,80,370,130'),
    @('curves-source-far-away',    'curves', '1000,900,1260,730', '1010,970,1330,910,950,700,1250,750')
)

$rows = New-Object Collections.Generic.List[string]
$rows.Add("case`tfixture`tsource`tdestination`tinputBounds`tkind`tindex`tah`tav`tlh`tlv`trh`trv")
Initialize-AiSession | Out-Null
foreach ($c in $cases) {
    $name, $fixture, $source, $destination = $c
    Invoke-Efd ("EFD.clear(); EFD.{0}('fx'); EFD.selectOnly('fx');" -f $fixture) | Out-Null
    Send-AiMessage 'fd append' | Out-Null
    $wrote = Send-AiMessage 'fd write' ("0|{0}|{1}" -f $source, $destination)
    if ((($wrote -split "`r?`n") | Where-Object { $_ -like "result`t*" }) -ne "result`t0") { throw "write failed for $name`: $wrote" }
    $bounds = Send-AiMessage 'fd bounds' '0'
    # The rectangle the renderer lays the quad onto, as Adobe's own edit path
    # reports it.
    $line = ($bounds -split "`r?`n") | Where-Object { $_ -like "input bounds`t*" } | Select-Object -First 1
    $inputBounds = $line.Substring(13).Trim('[', ']') -replace ' ', ','
    $dump = Invoke-Efd 'EFD.sourceAndResult("fx");'
    foreach ($row in ($dump -split "`r?`n")) {
        if (-not $row) { continue }
        $rows.Add(("{0}`t{1}`t{2}`t{3}`t{4}`t{5}" -f $name, $fixture, $source, $destination, $inputBounds, $row))
    }
    Write-Output ("{0}: {1} rows" -f $name, (($dump -split "`r?`n") | Where-Object { $_ }).Count)
}
[System.IO.File]::WriteAllLines((Join-Path $evidence 'mapping.tsv'), (Hide-Personal $rows))
