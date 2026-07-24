$ErrorActionPreference = "Stop"
$root = "d:\work\development\Repos\cpp\projects\authforge"

$count = 0
Get-ChildItem -Path $root -Recurse -Include "*.h","*.cc" | Where-Object {
    $_.FullName -notmatch "\\build\\" -and
    $_.FullName -notmatch "\\models\\"
} | ForEach-Object {
    $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
    if ($bytes.Length -lt 3) { return }
    if ($bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) { return }
    $newBytes = New-Object byte[] ($bytes.Length - 3)
    [Array]::Copy($bytes, 3, $newBytes, 0, $bytes.Length - 3)
    [System.IO.File]::WriteAllBytes($_.FullName, $newBytes)
    $count++
    $rel = $_.FullName.Substring($root.Length + 1)
    Write-Host "  BOM removed: $rel"
}
Write-Host ""
Write-Host "Removed BOM from $count files"
