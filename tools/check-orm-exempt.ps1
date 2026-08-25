# tools/check-orm-exempt.ps1
#
# PowerShell mirror of tools/check-orm-exempt.sh. Mechanically identical
# assertion set so the manage parity check (P8) sees no behavioural drift
# between platforms.
#
# Spec references:
#   _Design: §0, §0.1, §0.3, §4.1.1, Property 1_
#   _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.8, 1.9, 1.10_

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot    = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$IncludeDir  = Join-Path $RepoRoot 'OAuth2Plugin\include\oauth2\models'
$SrcDir      = Join-Path $RepoRoot 'OAuth2Plugin\src\models'

$ExemptClasses = @(
    'Oauth2AccessTokens'
    'Oauth2Clients'
    'Oauth2Codes'
    'Oauth2RefreshTokens'
    'Oauth2Scopes'
    'Oauth2ClientScopes'
    'Oauth2SubjectMappings'
    'Oauth2UserConsents'
    'Organizations'
    'Permissions'
    'Roles'
    'RolePermissions'
    'UserRoles'
    'Users'
)

$ExemptConfigs = @(
    'OAuth2Plugin\src\models\model.json'
    'OAuth2Plugin\src\models\model-postgresql.json'
    'OAuth2Plugin\src\models\.clang-format'
)

$ExpectedNamespaceOuter = 'drogon_model'
$ExpectedNamespaceInner = 'fulla_db'
$ProvenanceRegex        = 'DO NOT EDIT.*drogon_ctl'

$script:Failures = 0
$script:Checks   = 0

function Write-OkLog  { param([string]$Msg) Write-Host "[orm-exempt] $Msg" }
function Write-OkErr  {
    param([string]$Msg)
    Write-Host "[orm-exempt][err]  $Msg" -ForegroundColor Red
    $script:Failures++
}

function Test-FileContainsPattern {
    param([string]$Path, [string]$Pattern)
    if (-not (Test-Path $Path)) { return $false }
    return (Select-String -Path $Path -Pattern $Pattern -Quiet)
}

# Validate include / src directories exist.
if (-not (Test-Path $IncludeDir)) { Write-OkErr "missing include directory: $IncludeDir (R1.2)" }
if (-not (Test-Path $SrcDir))     { Write-OkErr "missing src directory: $SrcDir (R1.3)" }

foreach ($cls in $ExemptClasses) {
    $hdr = Join-Path $IncludeDir "$cls.h"
    $src = Join-Path $SrcDir     "$cls.cc"

    if (-not (Test-Path $hdr)) {
        Write-OkErr "header missing: $hdr (R1.1, R1.2)"
        $script:Checks += 4
        continue
    }
    if (-not (Test-Path $src)) {
        Write-OkErr "source missing: $src (R1.1, R1.3)"
    }

    if (-not (Test-FileContainsPattern -Path $hdr -Pattern "^class\s+$cls\b")) {
        Write-OkErr "class declaration missing in header: $cls (R1.1)"
    }
    if (-not (Test-FileContainsPattern -Path $hdr -Pattern "^namespace\s+$ExpectedNamespaceOuter\s*$")) {
        Write-OkErr "outer namespace missing: $cls expected 'namespace $ExpectedNamespaceOuter' (R1.4)"
    }
    if (-not (Test-FileContainsPattern -Path $hdr -Pattern "^namespace\s+$ExpectedNamespaceInner\s*$")) {
        Write-OkErr "inner namespace missing: $cls expected 'namespace $ExpectedNamespaceInner' (R1.4)"
    }
    if (-not (Test-FileContainsPattern -Path $hdr -Pattern $ProvenanceRegex)) {
        Write-OkErr "provenance comment removed from header: $cls (R1.8)"
    }
    if ((Test-Path $src) -and -not (Test-FileContainsPattern -Path $src -Pattern $ProvenanceRegex)) {
        Write-OkErr "provenance comment removed from source: $cls.cc (R1.8)"
    }

    $script:Checks += 4
}

foreach ($cfg in $ExemptConfigs) {
    $p = Join-Path $RepoRoot $cfg
    if (-not (Test-Path $p)) {
        Write-OkErr "exempt config missing: $cfg (R1.5)"
    }
    $script:Checks++
}

# R1.9: no ORM-exempt class file outside the canonical models directories.
$pluginRoot = Join-Path $RepoRoot 'OAuth2Plugin'
foreach ($cls in $ExemptClasses) {
    $candidates = Get-ChildItem -Path $pluginRoot -Recurse -File -Include "$cls.h", "$cls.cc" -ErrorAction SilentlyContinue |
        Where-Object {
            $full = $_.FullName
            ($full -notlike "$IncludeDir\*") -and ($full -notlike "$SrcDir\*") -and ($full -notlike "*\models_backup\*")
        }
    foreach ($stray in $candidates) {
        Write-OkErr "ORM-exempt file found outside models/: $($stray.FullName) (R1.9)"
    }
    $script:Checks++
}

if ($script:Failures -eq 0) {
    Write-OkLog ("OK: {0} ORM exemption assertions passed (14 classes, {1} configs)." -f $script:Checks, $ExemptConfigs.Count)
    exit 0
}
Write-OkErr "FAILED: $($script:Failures) of $($script:Checks) assertions; see messages above."
exit 1
