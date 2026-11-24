param([string]$To = '4194304', [switch]$Quick)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$exe = '.\build\sagev-batch.exe'
New-Item -ItemType Directory -Force results\raw | Out-Null
$common = @('--batch', 'data\batch.bin', '--runs', '3')
if ($Quick) { $To = '262144' }

function Bench([string]$model, [string]$csv, [string[]]$extra) {
  & $exe bench --model $model --expected "data\expected_$model.bin" @common --csv "results\raw\$csv.csv" @extra
  if ($LASTEXITCODE -ne 0) { throw "bench failed: $model $csv" }
}

foreach ($m in 'p0', 'p2') {
  Bench $m "${m}_sweep" @('--from', '1024', '--to', $To)
}

foreach ($order in 'none', 'count', 'cycles') {
  Bench 'p2' "p2_order_$order" @('--from', '65536', '--to', '1048576', '--cpu', '0', '--order', $order)
}

foreach ($m in 'p0', 'p2') {
  foreach ($block in '64', '128', '256') {
    Bench $m "${m}_global_b$block" @('--from', '262144', '--to', '1048576', '--cpu', '0', '--block', $block)
    Bench $m "${m}_lds_b$block" @('--from', '262144', '--to', '1048576', '--cpu', '0', '--block', $block, '--lds', '1')
  }
}

Bench 'p2' 'p2_pageable' @('--from', '262144', '--to', $To, '--cpu', '0')
foreach ($s in '1', '2', '4') {
  Bench 'p2' "p2_pinned_s$s" @('--from', '262144', '--to', $To, '--cpu', '0', '--pinned', '1', '--streams', $s)
}
Write-Output 'experiments done; raw tables in results\raw'
