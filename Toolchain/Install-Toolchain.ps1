<#
.SYNOPSIS
    Installs the full UEInsightMCP trace toolchain on Windows.

.DESCRIPTION
    Sets up both halves of the trace workflow:

      1. ue-insight-mcp  - live Editor/PIE bridge (Python, talks to the plugin
                           over TCP 127.0.0.1:55559).
      2. ue-trace        - offline .utrace analyzer (@mtuska/ue-trace-mcp),
                           installed to a stable directory with the Windows
                           fixes from Toolchain/Patches applied.

    It also copies the bundled Claude Code skills and registers both MCP
    servers at user scope.

    The script is idempotent: re-running it repairs a partial install rather
    than duplicating one. Use -CheckOnly to audit without changing anything.

.PARAMETER TraceMcpVersion
    Version of @mtuska/ue-trace-mcp to pin. A matching patch file must exist at
    Toolchain/Patches/ue-trace-mcp-<version>-windows.patch.

.PARAMETER InstallRoot
    Parent directory for versioned analyzer installs.

.PARAMETER SkillsRoot
    Destination for the bundled Claude Code skills.

.PARAMETER CheckOnly
    Report the state of every component and exit without modifying anything.

.EXAMPLE
    .\Install-Toolchain.ps1

.EXAMPLE
    .\Install-Toolchain.ps1 -CheckOnly

.EXAMPLE
    .\Install-Toolchain.ps1 -SkipMcpRegister -SkipSkills
#>
[CmdletBinding()]
param(
    [string] $TraceMcpVersion = '0.5.2',
    [string] $InstallRoot     = (Join-Path $env:USERPROFILE '.local\share\ue-trace-mcp'),
    [string] $SkillsRoot      = (Join-Path $env:USERPROFILE '.claude\skills'),
    [switch] $SkipPythonBridge,
    [switch] $SkipTraceAnalyzer,
    [switch] $SkipSkills,
    [switch] $SkipMcpRegister,
    [switch] $CheckOnly,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ToolchainDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PluginDir    = Split-Path -Parent $ToolchainDir
$PythonDir    = Join-Path $PluginDir 'Python'
$PatchesDir   = Join-Path $ToolchainDir 'Patches'
$SkillsSrc    = Join-Path $ToolchainDir 'Skills'

$script:Problems = @()

function Write-Step  { param([string] $Message) Write-Host ''; Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Ok    { param([string] $Message) Write-Host "    [ok]   $Message" -ForegroundColor Green }
function Write-Info  { param([string] $Message) Write-Host "    [info] $Message" -ForegroundColor Gray }
function Write-Skip  { param([string] $Message) Write-Host "    [skip] $Message" -ForegroundColor DarkGray }
function Write-Problem {
    param([string] $Message)
    Write-Host "    [FAIL] $Message" -ForegroundColor Red
    $script:Problems += $Message
}

function Get-CommandPath {
    param([string] $Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) { return $null }
    if ($cmd.Source) { return $cmd.Source }
    return $cmd.Name
}

function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string]   $FilePath,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [string] $WorkingDirectory,
        [switch] $AllowFailure
    )
    $prev = $null
    if ($WorkingDirectory) { $prev = (Get-Location).Path; Set-Location $WorkingDirectory }
    try {
        $output = & $FilePath @Arguments 2>&1 | Out-String
        $code   = $LASTEXITCODE
    }
    finally {
        if ($prev) { Set-Location $prev }
    }
    if ($code -ne 0 -and -not $AllowFailure) {
        $trimmed = ($output -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -Last 8) -join "`n"
        throw "$FilePath exited with $code`n$trimmed"
    }
    return [pscustomobject]@{ ExitCode = $code; Output = $output }
}

# ---------------------------------------------------------------------------
# 0. Preflight
# ---------------------------------------------------------------------------

Write-Step 'Preflight'

if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'This toolchain requires 64-bit Windows.'
}

$NodeExe   = Get-CommandPath 'node'
$NpmCmd    = Get-CommandPath 'npm'
$GitExe    = Get-CommandPath 'git'
$UvExe     = Get-CommandPath 'uv'
$ClaudeCmd = Get-CommandPath 'claude'

if ($NodeExe)   { Write-Ok      "node   $((Invoke-Native $NodeExe @('--version')).Output.Trim())  $NodeExe" }
else            { Write-Problem 'node not found on PATH. Install Node.js 18+ (https://nodejs.org).' }

if ($NpmCmd)    { Write-Ok      "npm    found" } else { Write-Problem 'npm not found on PATH.' }
if ($GitExe)    { Write-Ok      "git    found" } else { Write-Problem 'git not found on PATH (needed to apply the Windows patch).' }

if ($UvExe)     { Write-Ok      "uv     found  $UvExe" }
elseif (-not $SkipPythonBridge) { Write-Problem 'uv not found on PATH. Install from https://docs.astral.sh/uv/ or pass -SkipPythonBridge.' }

if ($ClaudeCmd) { Write-Ok      "claude found  $ClaudeCmd" }
elseif (-not $SkipMcpRegister)  { Write-Problem 'claude CLI not found on PATH. Install Claude Code or pass -SkipMcpRegister.' }

$PatchFile = Join-Path $PatchesDir "ue-trace-mcp-$TraceMcpVersion-windows.patch"
if (Test-Path -LiteralPath $PatchFile) {
    Write-Ok "patch  $([IO.Path]::GetFileName($PatchFile))"
}
elseif (-not $SkipTraceAnalyzer) {
    Write-Problem "No patch bundled for version $TraceMcpVersion at $PatchFile."
}

if ($script:Problems.Count -gt 0 -and -not $CheckOnly) {
    Write-Host ''
    throw "Preflight failed:`n  - " + ($script:Problems -join "`n  - ")
}

$VersionRoot = Join-Path $InstallRoot $TraceMcpVersion
$AppDir      = Join-Path $VersionRoot 'app'
$NativeDir   = Join-Path $VersionRoot 'native\windows-x64'
$PackageDir  = Join-Path $AppDir 'node_modules\@mtuska\ue-trace-mcp'
$DistEntry   = Join-Path $PackageDir 'dist\index.js'
$DigestBin   = Join-Path $NativeDir 'TraceDigest.exe'
$InsightExe  = Join-Path $env:USERPROFILE '.local\bin\ue-insight-mcp.exe'

# ---------------------------------------------------------------------------
# 1. Live Editor bridge (Python)
# ---------------------------------------------------------------------------

Write-Step 'Live Editor bridge (ue-insight-mcp)'

if ($SkipPythonBridge) {
    Write-Skip 'Skipped by -SkipPythonBridge.'
}
elseif ($CheckOnly) {
    if (Test-Path -LiteralPath $InsightExe) { Write-Ok "Installed at $InsightExe" }
    else { Write-Problem "Not installed: $InsightExe missing." }
}
else {
    if ((Test-Path -LiteralPath $InsightExe) -and -not $Force) {
        Write-Info 'Already installed; reinstalling to pick up local source changes.'
    }
    Write-Info "uv tool install --force --from `"$PythonDir`" ue-insight-mcp"
    Invoke-Native $UvExe @('tool', 'install', '--force', '--from', $PythonDir, 'ue-insight-mcp') | Out-Null
    if (Test-Path -LiteralPath $InsightExe) { Write-Ok "Installed at $InsightExe" }
    else { Write-Problem "uv reported success but $InsightExe is missing. Check that ~/.local/bin is on PATH." }
}

# ---------------------------------------------------------------------------
# 2. Offline analyzer (@mtuska/ue-trace-mcp, patched)
# ---------------------------------------------------------------------------

Write-Step "Offline analyzer (ue-trace $TraceMcpVersion)"

# Markers that prove each Windows fix is present in the installed dist.
$PatchMarkers = @{
    'dist\index.js'  = 'enableDaemon: process.platform !== "win32"'
    'dist\server.js' = 'process.platform !== "win32" && (opts.enableDaemon'
    'dist\digest.js' = 'TRACE_ONESHOT_TIMEOUT_MS'
    'dist\binary.js' = 'assertWindowsRuntime'
}

function Test-PatchApplied {
    param([string] $PackageRoot)
    foreach ($rel in $PatchMarkers.Keys) {
        $file = Join-Path $PackageRoot $rel
        if (-not (Test-Path -LiteralPath $file)) { return $false }
        if (-not (Select-String -LiteralPath $file -Pattern $PatchMarkers[$rel] -SimpleMatch -Quiet)) {
            return $false
        }
    }
    return $true
}

if ($SkipTraceAnalyzer) {
    Write-Skip 'Skipped by -SkipTraceAnalyzer.'
}
elseif ($CheckOnly) {
    if (Test-Path -LiteralPath $DistEntry) { Write-Ok "Package present at $PackageDir" }
    else { Write-Problem "Package missing: $DistEntry" }

    if ((Test-Path -LiteralPath $PackageDir) -and (Test-PatchApplied $PackageDir)) { Write-Ok 'Windows fixes applied.' }
    else { Write-Problem 'Windows fixes NOT applied to the installed dist.' }

    foreach ($f in @('TraceDigest.exe', 'tbbmalloc.dll', 'WinPixEventRuntime.dll')) {
        if (Test-Path -LiteralPath (Join-Path $NativeDir $f)) { Write-Ok "native/$f" }
        else { Write-Problem "native runtime missing: $f" }
    }
}
else {
    New-Item -ItemType Directory -Force -Path $AppDir, $NativeDir | Out-Null

    # 2a. Pinned npm install into a private host directory.
    $hostManifest = [ordered]@{
        name         = 'ue-trace-mcp-host'
        version      = '1.0.0'
        private      = $true
        description  = 'Stable local host for @mtuska/ue-trace-mcp (see UEInsightMCP/Toolchain).'
        dependencies = [ordered]@{ '@mtuska/ue-trace-mcp' = $TraceMcpVersion }
    }
    ($hostManifest | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath (Join-Path $AppDir 'package.json') -Encoding utf8

    $needInstall = $Force -or -not (Test-Path -LiteralPath $DistEntry)
    if ($needInstall) {
        Write-Info "npm install --omit=dev --ignore-scripts  (in $AppDir)"
        Invoke-Native $NpmCmd @('install', '--omit=dev', '--ignore-scripts', '--no-audit', '--no-fund') -WorkingDirectory $AppDir | Out-Null
    }
    else {
        Write-Info 'Package already installed; reusing (pass -Force to reinstall).'
    }

    if (-not (Test-Path -LiteralPath $DistEntry)) {
        throw "npm install finished but $DistEntry is missing."
    }
    Write-Ok "Package installed at $PackageDir"

    # 2b. Apply the Windows fixes.
    if (Test-PatchApplied $PackageDir) {
        Write-Ok 'Windows fixes already applied.'
    }
    else {
        # core.autocrlf=false keeps the dist files byte-identical to upstream + patch.
        $apply = Invoke-Native $GitExe @('-C', $PackageDir, '-c', 'core.autocrlf=false', 'apply', '--verbose', $PatchFile) -AllowFailure
        if ($apply.ExitCode -ne 0) {
            throw "Failed to apply $PatchFile to $PackageDir`n$($apply.Output)"
        }
        if (-not (Test-PatchApplied $PackageDir)) {
            throw "git apply succeeded but the expected markers are missing in $PackageDir."
        }
        Write-Ok 'Windows fixes applied (daemon disabled, one-shot -out JSON, 300s timeout, runtime assertion).'
    }

    # 2c. Native runtime, verified against the manifest baked into the package.
    $manifestPath = Join-Path $PackageDir 'binaries.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw "binaries.json not found in $PackageDir; cannot resolve the TraceDigest release."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $entry    = $manifest.binaries.'windows-x64'
    if ($null -eq $entry) { throw 'binaries.json has no windows-x64 entry.' }

    $runtimeFiles   = @('TraceDigest.exe', 'tbbmalloc.dll', 'WinPixEventRuntime.dll')
    $runtimeMissing = @($runtimeFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $NativeDir $_)) })

    if ($runtimeMissing.Count -eq 0 -and -not $Force) {
        Write-Ok 'Native runtime already staged.'
    }
    else {
        $url     = "https://github.com/mtuska/ue-trace-mcp/releases/download/v$($manifest.version)/$($entry.filename)"
        $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("ue-trace-native-" + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
        $archive = Join-Path $tempDir $entry.filename
        try {
            Write-Info "Downloading $($entry.filename)"
            $progress = $ProgressPreference
            $ProgressPreference = 'SilentlyContinue'
            try { Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing }
            finally { $ProgressPreference = $progress }

            $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($actual -ne $entry.sha256.ToLowerInvariant()) {
                throw "SHA-256 mismatch for $($entry.filename): expected $($entry.sha256), got $actual. Refusing to extract."
            }
            Write-Ok "SHA-256 verified against binaries.json"

            Expand-Archive -LiteralPath $archive -DestinationPath $tempDir -Force
            foreach ($f in $runtimeFiles) {
                $found = Get-ChildItem -LiteralPath $tempDir -Recurse -Filter $f -File | Select-Object -First 1
                if ($null -eq $found) { throw "Release archive does not contain $f." }
                Copy-Item -LiteralPath $found.FullName -Destination (Join-Path $NativeDir $f) -Force
            }
            Write-Ok "Native runtime staged in $NativeDir"
        }
        finally {
            Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# ---------------------------------------------------------------------------
# 3. Claude Code skills
# ---------------------------------------------------------------------------

Write-Step 'Claude Code skills'

if ($SkipSkills) {
    Write-Skip 'Skipped by -SkipSkills.'
}
elseif (-not (Test-Path -LiteralPath $SkillsSrc)) {
    Write-Problem "Bundled skills directory missing: $SkillsSrc"
}
else {
    foreach ($skill in Get-ChildItem -LiteralPath $SkillsSrc -Directory) {
        $dest = Join-Path $SkillsRoot $skill.Name
        if ($CheckOnly) {
            if (Test-Path -LiteralPath (Join-Path $dest 'SKILL.md')) { Write-Ok "$($skill.Name) installed" }
            else { Write-Problem "$($skill.Name) not installed at $dest" }
            continue
        }
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Path (Join-Path $skill.FullName '*') -Destination $dest -Recurse -Force
        Write-Ok "$($skill.Name) -> $dest"
    }
}

# ---------------------------------------------------------------------------
# 4. MCP registration (user scope)
# ---------------------------------------------------------------------------

Write-Step 'MCP registration (user scope)'

if ($SkipMcpRegister) {
    Write-Skip 'Skipped by -SkipMcpRegister.'
}
elseif ($CheckOnly) {
    $listed = (Invoke-Native $ClaudeCmd @('mcp', 'list') -AllowFailure).Output
    foreach ($name in @('ue-insight-mcp', 'ue-trace')) {
        if ($listed -match [regex]::Escape($name)) { Write-Ok "$name registered" }
        else { Write-Problem "$name not registered" }
    }
}
else {
    # `claude mcp add` refuses to overwrite, so remove first. Absent entries are fine.
    foreach ($name in @('ue-insight-mcp', 'ue-trace')) {
        Invoke-Native $ClaudeCmd @('mcp', 'remove', '--scope', 'user', $name) -AllowFailure | Out-Null
    }

    if (-not $SkipPythonBridge) {
        Invoke-Native $ClaudeCmd @('mcp', 'add', '--scope', 'user', 'ue-insight-mcp', '--', $InsightExe) | Out-Null
        Write-Ok 'ue-insight-mcp registered.'
    }

    if (-not $SkipTraceAnalyzer) {
        # NOTE: `claude mcp add -e` is variadic, so the server name must come
        # BEFORE the first -e or it gets swallowed as an env var.
        Invoke-Native $ClaudeCmd @(
            'mcp', 'add', '--scope', 'user', 'ue-trace',
            '-e', "TRACE_DIGEST_BIN=$DigestBin",
            '-e', 'TRACE_CACHE_SIZE=5',
            '--', $NodeExe, $DistEntry
        ) | Out-Null
        Write-Ok 'ue-trace registered.'
    }
}

# ---------------------------------------------------------------------------
# 5. Verify
# ---------------------------------------------------------------------------

Write-Step 'Verify'

if (-not $SkipTraceAnalyzer -and (Test-Path -LiteralPath $DistEntry)) {
    foreach ($rel in @('dist\index.js', 'dist\server.js', 'dist\digest.js', 'dist\binary.js')) {
        $check = Invoke-Native $NodeExe @('--check', (Join-Path $PackageDir $rel)) -AllowFailure
        if ($check.ExitCode -eq 0) { Write-Ok "node --check $rel" }
        else { Write-Problem "node --check failed for $rel`n$($check.Output)" }
    }
}

if (-not $SkipMcpRegister -and $ClaudeCmd) {
    $listed = (Invoke-Native $ClaudeCmd @('mcp', 'list') -AllowFailure).Output
    foreach ($line in ($listed -split "`r?`n")) {
        if ($line -match 'ue-insight-mcp|ue-trace') { Write-Info $line.Trim() }
    }
}

Write-Host ''
if ($script:Problems.Count -gt 0) {
    Write-Host 'Toolchain install incomplete:' -ForegroundColor Red
    foreach ($p in $script:Problems) { Write-Host "  - $p" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'See Toolchain/README.md for per-component recovery steps.' -ForegroundColor Yellow
    exit 1
}

Write-Host 'Toolchain ready.' -ForegroundColor Green
Write-Host ''
Write-Host 'Remaining manual steps (see Toolchain/Skills/ue-trace-install/SKILL.md):' -ForegroundColor Yellow
Write-Host '  1. Build the plugin so Binaries/Win64 contains a DLL matching your engine BuildId.'
Write-Host '  2. Enable UEInsightMCP in your .uproject.'
Write-Host '  3. Restart the Editor, adding -trace=default,cpu,memory when you need allocation data.'
Write-Host '  4. Restart Claude Code so it picks up the MCP registrations.'
exit 0
