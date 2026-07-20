param(
  [string]$Engine = (Join-Path $PSScriptRoot "..\source\KomoringHeights-by-gcc.exe")
)

$ErrorActionPreference = "Stop"
$enginePath = (Resolve-Path -LiteralPath $Engine).Path
$tempJson = Join-Path ([System.IO.Path]::GetTempPath()) ("komoring-tsume-" + [guid]::NewGuid() + ".jsonl")
try {
  $commands = @"
usi
isready
position sfen 4k4/9/4G4/9/9/9/9/9/9 b R 1
user tsume_save 3 100000 $tempJson
user tsume_load $tempJson 3 100000
position sfen 5k3/9/5G3/9/9/9/9/9/9 b R 1
user tsume_verify 3 100000
position sfen Bk7/2G6/9/9/9/9/9/9/9 b - 1
user tsume_verify 1 100000
# Regression: a futile interposition is excluded from the legal defence set;
# observing such a pseudo-reply must not make an otherwise complete work imperfect.
position sfen 8B/5+RgSk/9/9/7L+p/9/9/9/9 b Nrb3g3s3n3l17p 1
user tsume_verify 9 20000000
user tsume_generate 5 1 rejected.sfen
position sfen 7K1/9/9/9/9/9/9/9/7k1 w - 1
user tsume_verify 3 1000
position startpos
user tsume_solve
quit
"@ | & $enginePath
  $text = $commands -join "`n"

  if ($text -notmatch "usiok" -or $text -notmatch "readyok") { throw "USI handshake failed" }
  if ($text -notmatch "\[tsume_generate\]") { throw "short-generation guard failed" }
  if (($commands | Select-String -Pattern "\[tsume\] position error" -AllMatches).Matches.Count -ne 2) {
    throw "invalid-position validation failed"
  }
  if (($commands | Select-String -Pattern "checkmate R\*5b 5a4a 5b4b\+" -AllMatches).Matches.Count -ne 2) {
    throw "save/load mate reproduction failed"
  }
  if ($text -notmatch "checkmate nomate") { throw "legacy solve regression failed" }

  $records = $commands | Where-Object { $_ -match "tsume_json " } | ForEach-Object {
    ($_ -replace '^.*tsume_json ', '') | ConvertFrom-Json
  }
  if ($records.Count -ne 4) { throw "expected four JSON records" }
  if ($records[0].id -ne $records[1].id -or
      $records[0].canonicalId -ne $records[1].canonicalId -or
      $records[0].matePly -ne $records[1].matePly) { throw "JSON round-trip mismatch" }
  if ($records[0].canonicalId -ne $records[2].canonicalId) { throw "mirror canonicalization mismatch" }
  if (-not $records[3].complete -or -not $records[3].unique -or -not $records[3].perfect -or
      -not $records[3].futileInterposition -or $records[3].matePly -ne 9) {
    throw "futile-interposition perfection regression"
  }
  if ($null -eq $records[0].scores.totalScore -or $null -eq $records[0].attacks -or
      $null -eq $records[0].reasons) { throw "required JSON fields missing" }
  if ($records[0].defenderHand -ne "r2b3g4s4n4l18p") { throw "39-piece reserve reconstruction failed" }
  Write-Output "tsume mode regression: PASS"
}
finally {
  if (Test-Path -LiteralPath $tempJson) { Remove-Item -LiteralPath $tempJson }
}
