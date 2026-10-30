# PackSeller release packaging. Does NOT touch the game folder and uploads nothing.
# Usage: powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Version 0.1.0] [-Dll <path>]
# Output: dist\PackSeller\ (mod folder) and dist\PackSeller-<Version>.zip

param(
    [string]$Version = "0.1.0",
    [string]$Dll = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ($Dll -eq "") { $Dll = Join-Path $root "src\x64\Release\PackSeller.dll" }

$dist    = Join-Path $root "dist"
$modDir  = Join-Path $dist "PackSeller"
$zipPath = Join-Path $dist ("PackSeller-" + $Version + ".zip")

# source file -> name inside the package
$files = @(
    @{ Src = $Dll;                                   Name = "PackSeller.dll" },
    @{ Src = (Join-Path $root "PackSeller.mod");     Name = "PackSeller.mod" },
    @{ Src = (Join-Path $root "RE_Kenshi.json");     Name = "RE_Kenshi.json" },
    @{ Src = (Join-Path $dist "PackSeller.cfg.release"); Name = "PackSeller.cfg" },
    @{ Src = (Join-Path $root "README.md");          Name = "README.txt" },
    @{ Src = (Join-Path $root "LICENSE");            Name = "LICENSE" },
    @{ Src = (Join-Path $root "CHANGELOG.md");       Name = "CHANGELOG.txt" },
    @{ Src = (Join-Path $root "NOTICE.md");          Name = "NOTICE.md" }
)

foreach ($f in $files) {
    if (-not (Test-Path -LiteralPath $f.Src)) { throw ("Missing input: " + $f.Src) }
}

# the release config must be the safe one
$cfg = Get-Content -LiteralPath (Join-Path $dist "PackSeller.cfg.release") -Raw
foreach ($line in @("sell_dry_run=0", "debug_log=0", "order_enabled=1")) {
    if ($cfg -notmatch ("(?m)^" + [regex]::Escape($line) + "\s*$")) { throw ("Release cfg lacks: " + $line) }
}

# clean output (only inside dist\)
if (Test-Path -LiteralPath $modDir)  { Remove-Item -LiteralPath $modDir -Recurse -Force }
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
New-Item -ItemType Directory -Path $modDir | Out-Null

foreach ($f in $files) {
    Copy-Item -LiteralPath $f.Src -Destination (Join-Path $modDir $f.Name)
}

# zip with forward-slash entry names and a top-level folder PackSeller/
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in $files) {
        $entryName = "PackSeller/" + $f.Name
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, (Join-Path $modDir $f.Name), $entryName, [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally { $zip.Dispose() }

# verify: extract into a clean temp dir, compare SHA-256, look for forbidden content
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("PackSellerVerify_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $tmp)
    $ok = $true
    $extracted = Get-ChildItem -LiteralPath (Join-Path $tmp "PackSeller") -Recurse -File
    if ($extracted.Count -ne $files.Count) { Write-Host "FAIL: file count mismatch"; $ok = $false }
    foreach ($f in $files) {
        $a = (Get-FileHash -LiteralPath $f.Src -Algorithm SHA256).Hash
        $b = (Get-FileHash -LiteralPath (Join-Path $tmp ("PackSeller\" + $f.Name)) -Algorithm SHA256).Hash
        if ($a -ne $b) { Write-Host ("FAIL: hash mismatch " + $f.Name); $ok = $false }
    }
    foreach ($x in $extracted) {
        if ($x.Extension -in @(".pdb", ".obj", ".lib", ".exp", ".ilk", ".cpp", ".h", ".vcxproj", ".sln")) {
            Write-Host ("FAIL: forbidden file " + $x.Name); $ok = $false
        }
        if ($x.Extension -ne ".dll") {
            $text = Get-Content -LiteralPath $x.FullName -Raw
            if ($text -match '[A-Za-z]:\\(Users|SteamLibrary|Claude)' -or $text -match 'build/') {
                Write-Host ("FAIL: personal path or build/ in " + $x.Name); $ok = $false
            }
        }
    }
    if (-not $ok) { throw "Package verification failed" }
    Write-Host "Verification OK (extract to clean dir, SHA-256 equal, no forbidden files, no personal paths)"
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force
}

Write-Host ""
Write-Host ("Zip: " + $zipPath)
Write-Host "Contents:"
$za = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try { foreach ($e in $za.Entries) { Write-Host ("  {0}  {1} bytes" -f $e.FullName, $e.Length) } } finally { $za.Dispose() }
Write-Host "SHA-256:"
Get-ChildItem -LiteralPath $modDir -File | ForEach-Object {
    Write-Host ("  {0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name)
}
Write-Host ("  {0}  {1}" -f (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash, (Split-Path -Leaf $zipPath))
