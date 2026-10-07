# Run only on real Windows. Produces a new evidence directory; changes no settings.
param(
    [Parameter(Mandatory=$true)][string]$Clip,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$FixtureManifest,
    [ValidateRange(0,32)][int]$Adapter = 0,
    [string]$BuildManifest = (Join-Path $PSScriptRoot 'build/build-manifest.json'),
    [string]$Probe = (Join-Path $PSScriptRoot 'build/mediaengine_readback.exe')
)
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Real Windows is required.' }
$clipPath = (Resolve-Path -LiteralPath $Clip).Path
$probePath = (Resolve-Path -LiteralPath $Probe).Path
$buildManifestPath = (Resolve-Path -LiteralPath $BuildManifest).Path
$fixtureManifestPath = (Resolve-Path -LiteralPath $FixtureManifest).Path
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Output must be a new directory.' }
New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop | Out-Null
$capturePath = Join-Path $outputPath 'capture'
$before = @{
    clip = (Get-FileHash -Algorithm SHA256 -LiteralPath $clipPath).Hash
    probe = (Get-FileHash -Algorithm SHA256 -LiteralPath $probePath).Hash
    build_manifest = (Get-FileHash -Algorithm SHA256 -LiteralPath $buildManifestPath).Hash
    fixture_manifest = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixtureManifestPath).Hash
}
$identities = Get-Content -Raw -LiteralPath $fixtureManifestPath | ConvertFrom-Json
$build = Get-Content -Raw -LiteralPath $buildManifestPath | ConvertFrom-Json
function Test-BuildInputs {
    $expected = @('mediaengine_readback.c', 'run-windows.ps1', 'analyze.py', 'build_manifest.py',
                  'build-cross.sh', 'build-windows.cmd', 'prepare_fixtures.py', 'reference.json')
    $names = @($build.sources.PSObject.Properties.Name)
    if ($build.schema -ne 1 -or $names.Count -ne $expected.Count -or $build.executable.name -ne 'mediaengine_readback.exe') {
        throw 'Unexpected build manifest schema.'
    }
    foreach ($name in $expected) {
        if ($name -notin $names -or (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot $name)).Hash.ToLowerInvariant() -ne $build.sources.$name) {
            throw "Build source differs from manifest: $name"
        }
    }
}
Test-BuildInputs
if ($before.probe.ToLowerInvariant() -ne $build.executable.sha256) { throw 'Executable differs from local build manifest.' }
if ($identities.schema -ne 1 -or $identities.reference_sha256 -ne $build.sources.'reference.json' -or
    $identities.generator_sha256 -ne $build.sources.'prepare_fixtures.py') { throw 'Fixture source revision differs from build.' }
$fixtureKind = $null
foreach ($kind in @('sdr10', 'pq10', 'hlg10')) {
    if ($before.clip.ToLowerInvariant() -eq $identities.clips.$kind.sha256 -and $identities.clips.$kind.exact_decode) { $fixtureKind = $kind }
}
if (-not $fixtureKind) { throw 'Clip is not one of the validated calibrated fixtures.' }
Copy-Item -LiteralPath $buildManifestPath -Destination (Join-Path $outputPath 'build-manifest.json')
Copy-Item -LiteralPath $fixtureManifestPath -Destination (Join-Path $outputPath 'fixture-manifest.json')
$report = [ordered]@{
    windows_runtime_test = $true
    windows_policy_acceptance = $false
    utc_started = [DateTime]::UtcNow.ToString('o')
    os = [Environment]::OSVersion.VersionString
    adapter_requested = $Adapter
    fixture_kind = $fixtureKind
    input_path = $clipPath
    before = $before
    timed_out = $false
    exited = $false
    exit_code = $null
    inputs_unchanged = $false
    failure = $null
    components = @()
}
$process = $null
try {
    # The C program accepts a local file URL; use URI escaping for spaces/#/Unicode.
    $uri = ([Uri]$clipPath).AbsoluteUri
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $probePath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WorkingDirectory = $PSScriptRoot
    # Windows file names cannot contain quotes. Generated capture path has no trailing slash.
    $start.Arguments = '"' + $uri + '" "' + $capturePath + '" ' + $Adapter
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'Probe process did not start.' }
    if (-not $process.WaitForExit(45000)) {
        $report.timed_out = $true
        $process.Kill()
        if (-not $process.WaitForExit(5000)) { throw 'Owned probe process did not exit after termination.' }
    }
    $report.exited = $process.HasExited
    $report.exit_code = $process.ExitCode
    $metadata = Join-Path $capturePath 'metadata.jsonl'
    if (Test-Path -LiteralPath $metadata) {
        # Record only paths of modules observed loaded by this process. A loaded module
        # does not prove that its conversion shader or hardware decoder was selected.
        foreach ($line in Get-Content -LiteralPath $metadata) {
            $entry = $line | ConvertFrom-Json
            if ($entry.kind -eq 'loaded_component' -and $entry.loaded -and $entry.path) {
                $item = Get-Item -LiteralPath $entry.path
                $signature = Get-AuthenticodeSignature -LiteralPath $entry.path
                $report.components += [ordered]@{
                    name = $entry.name; path = $entry.path
                    sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.path).Hash
                    version = $item.VersionInfo.FileVersion
                    signature_status = $signature.Status.ToString()
                    signer = $(if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null })
                }
            }
        }
    }
} catch {
    $report.failure = $_.Exception.Message
} finally {
    # A failure in collection must not skip owned-process cleanup or identity checks.
    try {
        if ($process -and -not $process.HasExited) {
            $process.Kill()
            if (-not $process.WaitForExit(5000)) { throw 'Probe remains running.' }
        }
        if ($process) { $report.exited = $process.HasExited; $process.Dispose() }
    } catch { $report.failure = "$($report.failure); cleanup: $($_.Exception.Message)" }
    try {
        $report.after = @{
            clip = (Get-FileHash -Algorithm SHA256 -LiteralPath $clipPath).Hash
            probe = (Get-FileHash -Algorithm SHA256 -LiteralPath $probePath).Hash
            build_manifest = (Get-FileHash -Algorithm SHA256 -LiteralPath $buildManifestPath).Hash
            fixture_manifest = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixtureManifestPath).Hash
        }
        Test-BuildInputs
        $report.inputs_unchanged = ($report.after.clip -eq $before.clip -and $report.after.probe -eq $before.probe -and
                                  $report.after.build_manifest -eq $before.build_manifest -and $report.after.fixture_manifest -eq $before.fixture_manifest)
    } catch { $report.failure = "$($report.failure); identity: $($_.Exception.Message)" }
    $report.utc_finished = [DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $outputPath 'run.json')
}
if ($report.failure -or $report.timed_out -or -not $report.exited -or -not $report.inputs_unchanged -or $report.exit_code -ne 0) { exit 1 }
exit 0
