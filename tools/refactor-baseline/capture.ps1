# tools/refactor-baseline/capture.ps1
#
# P0 baseline snapshot entry point for the repo-structure-refactor spec.
# PowerShell counterpart of capture.sh; mechanically identical command face.
#
# Spec references:
#   _Design: §2.8 P0, §12.1, §12.6, Property 3_
#   _Requirements: 14.1, 17.5_

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('setup', 'ctest', 'playwright', 'endpoints', 'all', 'verify', 'selftest', 'help')]
    [string]$Action = 'help',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config,

    [string]$BuildDir,

    [switch]$FromFixture,

    [ValidateSet('admin', 'frontend', 'all')]
    [string]$App = 'all',

    [string]$JsonPath,

    [string]$ListPath,

    [string]$OpenApi
)

$ErrorActionPreference = 'Stop'

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$BaselineDir = $ScriptDir
$RepoRoot    = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
$Parser      = Join-Path $ScriptDir 'parse_ctest.py'
$FixtureDir  = Join-Path $ScriptDir 'fixtures'

function Write-Log  { param([string]$Msg) Write-Host "[baseline] $Msg" }
function Write-Warn { param([string]$Msg) Write-Host "[baseline][warn] $Msg" -ForegroundColor Yellow }
function Write-Err  { param([string]$Msg) Write-Host "[baseline][err]  $Msg" -ForegroundColor Red }

function Resolve-Python {
    # On Windows, `python3` often resolves to the Microsoft Store alias under
    # WindowsApps which is a stub launcher that opens the Store rather than
    # running Python. We probe candidates in order and skip any that resolve
    # under a `WindowsApps` path.
    foreach ($cand in @('python', 'python3')) {
        $cmd = Get-Command $cand -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        if ($cmd.Source -and ($cmd.Source -match '\\WindowsApps\\')) { continue }
        return $cmd.Source
    }
    Write-Err 'python (real interpreter) not found in PATH (needed by parse_ctest.py)'
    Write-Err 'install Python 3.x and ensure it is on PATH, not just the Store stub.'
    exit 1
}

function Resolve-OsTag {
    if ($IsWindows -or $env:OS -eq 'Windows_NT') { return 'windows' }
    if ($IsMacOS) { return 'macos' }
    if ($IsLinux) { return 'linux' }
    return 'unknown'
}

function Resolve-Cfg {
    param([string]$Cfg)
    if (-not $Cfg -or $Cfg -eq '') {
        if ($env:OAUTH2_CTEST_CONFIG) { $Cfg = $env:OAUTH2_CTEST_CONFIG } else { $Cfg = 'Debug' }
    }
    switch ($Cfg) {
        'debug'   { return 'Debug' }
        'release' { return 'Release' }
        default {
            if (@('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel') -contains $Cfg) { return $Cfg }
            Write-Err "Unsupported ctest config: $Cfg"
            exit 1
        }
    }
}

function Invoke-Setup {
    Write-Log "Creating baseline subdirectory layout under: $BaselineDir"
    foreach ($sub in @('ctest', 'playwright', 'endpoints')) {
        $dir = Join-Path $BaselineDir $sub
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir | Out-Null
        }
        $marker = Join-Path $dir '.gitkeep'
        if (-not (Test-Path $marker)) {
            Set-Content -Path $marker -Value '' -NoNewline
        }
    }
    Write-Log 'setup done.'
}

function Invoke-Ctest {
    $cfg = Resolve-Cfg -Cfg $Config
    $bd  = if ($BuildDir) { $BuildDir } else { Join-Path $RepoRoot 'build' }
    $osTag = Resolve-OsTag
    $outDir = Join-Path $BaselineDir 'ctest'
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
    $outFile = Join-Path $outDir ("{0}-{1}.txt" -f $osTag, $cfg.ToLowerInvariant())

    $py = Resolve-Python

    $listingTmp = New-TemporaryFile
    $runTmp     = New-TemporaryFile
    try {
        if ($FromFixture) {
            Write-Log 'ctest: using bundled fixtures (offline self-test mode)'
            Copy-Item (Join-Path $FixtureDir 'ctest-N.sample.txt')    -Destination $listingTmp.FullName -Force
            Copy-Item (Join-Path $FixtureDir 'ctest-pass.sample.txt') -Destination $runTmp.FullName     -Force
        }
        else {
            if (-not (Test-Path $bd)) {
                Write-Err "build dir not found: $bd"
                Write-Err "configure first (cmake -S . -B build [-DCMAKE_BUILD_TYPE=$cfg])"
                Write-Err 'or rerun with -FromFixture to capture against bundled fixtures.'
                exit 1
            }
            $ctest = Get-Command ctest -ErrorAction SilentlyContinue
            if (-not $ctest) {
                Write-Err 'ctest not found in PATH'
                exit 1
            }
            Write-Log "ctest -N -C $cfg (build_dir=$bd)"
            Push-Location $bd
            try {
                & ctest -N -C $cfg | Out-File -FilePath $listingTmp.FullName -Encoding ascii
                Write-Log "ctest -C $cfg --output-on-failure (build_dir=$bd)"
                & ctest -C $cfg --output-on-failure | Out-File -FilePath $runTmp.FullName -Encoding ascii
                if ($LASTEXITCODE -ne 0) {
                    Write-Warn "ctest exited $LASTEXITCODE; run record will contain FAILED rows"
                }
            }
            finally { Pop-Location }
        }

        & $py $Parser --listing $listingTmp.FullName --run $runTmp.FullName --out $outFile
        if ($LASTEXITCODE -ne 0) {
            Write-Err "parse_ctest.py exited $LASTEXITCODE"
            exit $LASTEXITCODE
        }
        Write-Log "ctest baseline written: $outFile"
    }
    finally {
        if (Test-Path $listingTmp.FullName) { Remove-Item $listingTmp.FullName -Force }
        if (Test-Path $runTmp.FullName)     { Remove-Item $runTmp.FullName     -Force }
    }
}

function Invoke-Playwright {
    $py = Resolve-Python
    $parser = Join-Path $ScriptDir 'parse_playwright.py'
    $outDir = Join-Path $BaselineDir 'playwright'
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

    $apps = if ($App -eq 'all') { @('admin', 'frontend') } else { @($App) }
    foreach ($a in $apps) {
        $subdir = switch ($a) {
            'admin'    { 'frontends/admin' }
            'frontend' { 'frontends/user' }
        }
        $outFile = Join-Path $outDir ("{0}.txt" -f $a)
        $tmp = New-TemporaryFile
        try {
            if ($FromFixture) {
                Write-Log "playwright[$a]: using bundled --list fixture"
                Copy-Item (Join-Path $FixtureDir 'playwright-list.sample.txt') -Destination $tmp.FullName -Force
                & $py $parser --list $tmp.FullName --out $outFile
            }
            elseif ($JsonPath) {
                Write-Log "playwright[$a]: parsing pre-collected json: $JsonPath"
                & $py $parser --json $JsonPath --out $outFile
            }
            elseif ($ListPath) {
                Write-Log "playwright[$a]: parsing pre-collected list: $ListPath"
                & $py $parser --list $ListPath --out $outFile
            }
            else {
                $appDir = Join-Path $RepoRoot $subdir
                $bin = Join-Path $appDir 'node_modules\.bin\playwright.cmd'
                if (-not (Test-Path $bin)) {
                    Write-Err "playwright[$a]: $appDir\node_modules not found; run npm ci first"
                    Write-Err 'or rerun with -FromFixture for offline self-test.'
                    exit 1
                }
                Write-Log "playwright[$a]: $subdir -> playwright test --list --reporter=json"
                # Use cmd /c to redirect stdout to file at the OS level: this
                # avoids PowerShell mojibake on Unicode characters that
                # playwright emits to its progress lines (e.g. U+203A) when
                # the console code page is not UTF-8.
                $bin2 = $bin.Replace('"', '""')
                $tmpFile = $tmp.FullName
                $appDir2 = $appDir.Replace('"', '""')
                & cmd.exe /c "cd /d `"$appDir2`" && `"$bin2`" test --list --reporter=json > `"$tmpFile`" 2>NUL"
                $exitCode = $LASTEXITCODE
                if ($exitCode -ne 0) {
                    Write-Warn "playwright test --list --reporter=json exited $exitCode; continuing if file is non-empty"
                }
                if (-not (Test-Path $tmpFile) -or ((Get-Item $tmpFile).Length -eq 0)) {
                    Write-Err "playwright[$a]: empty output captured"
                    exit 1
                }
                & $py $parser --json $tmpFile --out $outFile
            }
            if ($LASTEXITCODE -ne 0) {
                Write-Err "parse_playwright.py exited $LASTEXITCODE"
                exit $LASTEXITCODE
            }
            Write-Log "playwright baseline written: $outFile"
        }
        finally {
            if (Test-Path $tmp.FullName) { Remove-Item $tmp.FullName -Force }
        }
    }
}

function Invoke-Endpoints {
    $py = Resolve-Python
    $parser = Join-Path $ScriptDir 'parse_endpoints.py'
    $outDir = Join-Path $BaselineDir 'endpoints'
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

    if ($FromFixture) {
        Write-Log 'endpoints: using bundled OpenAPI fixture (offline)'
        $outFile = Join-Path $outDir 'openapi-fixture.signature.txt'
        & $py $parser --openapi (Join-Path $FixtureDir 'openapi-mini.yaml') --out $outFile
        if ($LASTEXITCODE -ne 0) { Write-Err "parse_endpoints.py exited $LASTEXITCODE"; exit $LASTEXITCODE }
        Write-Log "endpoints baseline written: $outFile"
        return
    }

    $openapiPath = if ($OpenApi) { $OpenApi } else { Join-Path $RepoRoot 'apps\server\openapi.yaml' }
    if (-not (Test-Path $openapiPath)) {
        Write-Err "OpenAPI file not found: $openapiPath"
        Write-Err '(P0 scaffolding mode B: extract static endpoint signatures from'
        Write-Err ' apps\server\openapi.yaml. Live status/headers/body capture is'
        Write-Err ' backfilled in P7 once docker compose smoke is reachable.)'
        exit 1
    }

    $outFile = Join-Path $outDir 'openapi.signature.txt'
    & $py $parser --openapi $openapiPath --out $outFile
    if ($LASTEXITCODE -ne 0) { Write-Err "parse_endpoints.py exited $LASTEXITCODE"; exit $LASTEXITCODE }
    Write-Log "endpoints baseline written: $outFile"
    Write-Log '(scaffolding mode B; live response capture deferred to P7)'
}

function Invoke-All {
    Invoke-Setup
    Invoke-Ctest
    Invoke-Playwright
    Invoke-Endpoints
}

function Invoke-Verify {
    Write-Log 'Verifying baseline directories are non-empty (task 1.7 gate input)...'
    $fail = 0
    foreach ($sub in @('ctest', 'playwright', 'endpoints')) {
        $dir = Join-Path $BaselineDir $sub
        if (-not (Test-Path $dir)) {
            Write-Err "Missing directory: $dir"
            $fail = 1
            continue
        }
        $entries = Get-ChildItem -Path $dir -Force | Where-Object { $_.Name -ne '.gitkeep' }
        if (-not $entries -or $entries.Count -eq 0) {
            Write-Err "Empty baseline subdirectory: $dir (only .gitkeep present)"
            $fail = 1
        }
        else {
            Write-Log ("OK  {0} ({1} entries)" -f $dir, $entries.Count)
        }
    }

    Write-Log 'Running tools\check-orm-exempt.ps1 ...'
    & powershell -ExecutionPolicy Bypass -File (Join-Path $RepoRoot 'tools\check-orm-exempt.ps1')
    if ($LASTEXITCODE -ne 0) { $fail = 1 }

    $sig = Join-Path $BaselineDir 'endpoints\openapi.signature.txt'
    if (Test-Path $sig) {
        Write-Log 'Running tools\diff-endpoint-baseline.py ...'
        $py = Resolve-Python
        & $py (Join-Path $RepoRoot 'tools\diff-endpoint-baseline.py')
        if ($LASTEXITCODE -ne 0) { $fail = 1 }
    }
    else {
        Write-Warn "endpoints\openapi.signature.txt absent; skipping diff-endpoint-baseline check"
    }

    exit $fail
}

function Invoke-Selftest {
    $py = Resolve-Python
    & $py $Parser --selftest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $py (Join-Path $ScriptDir 'parse_playwright.py') --selftest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $py (Join-Path $ScriptDir 'parse_endpoints.py') --selftest
    exit $LASTEXITCODE
}

function Invoke-Help {
@'
Usage: capture.ps1 <subcommand> [-Config <Debug|Release>] [-BuildDir <path>] [-FromFixture]
                   [-App admin|frontend|all] [-JsonPath <file>] [-ListPath <file>]

Subcommands:
  setup        Create baseline subdirectories (idempotent, task 1.1).
  ctest        Capture ctest baseline for the current host. Task 1.2.
               -Config Debug|Release (default Debug or $env:OAUTH2_CTEST_CONFIG)
               -BuildDir <path>      (default <repo>/build)
               -FromFixture          use bundled sample (offline self-test)
  playwright   Capture Playwright baseline. Task 1.3.
               -App admin|frontend|all   (default all)
               -FromFixture              use bundled --list fixture (offline)
               -JsonPath <file>          parse pre-collected reporter=json file
               -ListPath <file>          parse pre-collected --list output file
  endpoints    Capture HTTP endpoint baseline. Task 1.4 (scheme B: static
               OpenAPI signatures from apps\server\openapi.yaml; live
               response capture is backfilled in P7).
               -OpenApi <path>           override the input OpenAPI
               -FromFixture              use bundled mini OpenAPI fixture
  all          setup + ctest + playwright + endpoints.
  verify       Assert all baseline subdirectories are non-empty (P0 gate).
  selftest     Run parse_ctest.py + parse_playwright.py self-tests against fixtures.
  help         Show this message.

The 6-cell ctest matrix (Linux/macOS/Windows x Debug/Release) is captured by
running this script on each runner; each invocation emits one file
ctest\<os>-<cfg>.txt.

Playwright baselines: tools\refactor-baseline\playwright\{admin,frontend}.txt.
'@ | Write-Host
}

switch ($Action) {
    'setup'      { Invoke-Setup }
    'ctest'      { Invoke-Ctest }
    'playwright' { Invoke-Playwright }
    'endpoints'  { Invoke-Endpoints }
    'all'        { Invoke-All }
    'verify'     { Invoke-Verify }
    'selftest'   { Invoke-Selftest }
    'help'       { Invoke-Help }
    default      { Invoke-Help }
}
