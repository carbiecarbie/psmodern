# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Builds PSModern from the committed HEAD and packages it as a Windows release zip.

.DESCRIPTION
HEAD is exported with git archive into a temporary folder under -OutputDir,
configured with CMake and Ninja (RelWithDebInfo, tests off) and built for the
psmodern target only; that executable is stripped into a staging copy. The
script refuses a dirty git tree, an output folder inside the repository and an
existing zip, .partial, staging or build folder. It requires exactly the DLL
set SDL2.dll, libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll from
the UCRT64 bin folder, runs the staged psmodern.exe --version and requires the
output PSModern <version>. The zip is written as <name>.zip.partial and renamed
only after everything passed. It never packages ROMs, saves or debug files.
The tag v<version> does not have to exist yet (the zip is play-tested before
tagging); the script ends by printing the commit the tag must point to.

.PARAMETER OutputDir
Folder outside the repository that receives the build folder, the staging
folder and the zip.
.PARAMETER Ucrt64
MSYS2 UCRT64 root; its bin folder supplies cmake, ninja and the compiler.
.PARAMETER Ldd
MSYS2 ldd.exe, used to find the runtime DLLs.
.PARAMETER KeepBuild
Keeps the temporary export and build folder after a successful run.
.PARAMETER BuildDir
Testing shortcut only: packages the psmodern.exe of an existing build
directory (relative to the repository, or absolute) instead of building HEAD.
Requires -AllowDirty, so the package name always carries the -dirty suffix.
.PARAMETER AllowDirty
For testing the script only; never for a published zip. Allows a dirty tree
and adds a -dirty suffix to the package name.
.PARAMETER VersionOverride
Hidden test aid: replaces the version read from CMakeLists.txt. Requires
-AllowDirty, so the package name always carries the -dirty suffix.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$OutputDir,
	[string]$BuildDir = '',
	[string]$Ucrt64 = 'C:\msys64\ucrt64',
	[string]$Ldd = 'C:\msys64\usr\bin\ldd.exe',
	[switch]$AllowDirty,
	[switch]$KeepBuild,
	[string]$VersionOverride = ''
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$github = 'https://github.com/carbiecarbie/psmodern'

function Resolve-Argument([string]$path) {
	return $PSCmdlet.GetUnresolvedProviderPathFromPSPath($path).TrimEnd('\', '/')
}
function Require-File([string]$path) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required file: $path" }
	return $path
}

if ($BuildDir -and -not $AllowDirty) { throw '-BuildDir requires -AllowDirty (it is for testing the script only)' }
if ($VersionOverride -and -not $AllowDirty) { throw '-VersionOverride requires -AllowDirty' }

# Clean tree and the commit being packaged.
$status = @(& git -C $repository status --porcelain)
if ($LASTEXITCODE) { throw 'git status failed' }
if ($status.Count -gt 0 -and -not $AllowDirty) { throw 'The git working tree is not clean; commit or stash first' }
$head = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $head -notmatch '^[0-9a-f]{40}$') { throw 'git rev-parse failed' }

# Version: from HEAD when HEAD is built, from the working tree for -BuildDir.
if ($BuildDir) { $cmake = Get-Content -LiteralPath (Join-Path $repository 'CMakeLists.txt') -Raw }
else {
	$cmake = (@(& git -C $repository show 'HEAD:CMakeLists.txt')) -join "`n"
	if ($LASTEXITCODE) { throw 'git show HEAD:CMakeLists.txt failed' }
}
$version = ([regex]::Match($cmake, 'project\(PSModern VERSION ([0-9]+\.[0-9]+\.[0-9]+)')).Groups[1].Value
if ($VersionOverride) { $version = $VersionOverride }
if (-not $version) { throw 'Cannot read the version: CMakeLists.txt needs "project(PSModern VERSION x.y.z"' }

# Output paths.
$output = Resolve-Argument $OutputDir
if ($output.Equals($repository, [StringComparison]::OrdinalIgnoreCase) -or
	$output.StartsWith($repository + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Output must be outside the repository'
}
$suffix = ''
if ($AllowDirty) { $suffix = '-dirty' }
$name = "PSModern-$version-windows-x64$suffix"
$zip = Join-Path $output "$name.zip"
$partial = "$zip.partial"
$stage = Join-Path $output $name
$work = Join-Path $output "PSModern-$version-build"
foreach ($p in @($zip, $partial, $stage, $work)) {
	if (Test-Path -LiteralPath $p) { throw "Output already exists: $p" }
}

# Tools.
$ucrtRoot = Resolve-Argument $Ucrt64
$ucrtBin = Join-Path $ucrtRoot 'bin'
$strip = Require-File (Join-Path $ucrtBin 'strip.exe')
$cmakeExe = Require-File (Join-Path $ucrtBin 'cmake.exe')
Require-File (Join-Path $ucrtBin 'ninja.exe') | Out-Null
$lddExe = Require-File (Resolve-Argument $Ldd)
$pacman = Require-File (Join-Path (Split-Path $lddExe) 'pacman.exe')
$expectedDlls = @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'SDL2.dll')

$succeeded = $false
try {
	New-Item -ItemType Directory -Force -Path $output | Out-Null
	New-Item -ItemType Directory -Path $work | Out-Null

	# Sources: an export of HEAD, or the repository itself for -BuildDir.
	$src = $repository
	$exe = $null
	if ($BuildDir) {
		$buildPath = $BuildDir
		if (-not [IO.Path]::IsPathRooted($buildPath)) { $buildPath = Join-Path $repository $BuildDir }
		$exe = Require-File (Join-Path (Resolve-Argument $buildPath) 'psmodern.exe')
	} else {
		$src = Join-Path $work 'source'
		$archive = Join-Path $work 'head.zip'
		& git -C $repository archive --format=zip -o $archive HEAD
		if ($LASTEXITCODE) { throw 'git archive failed' }
		Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
		[IO.Compression.ZipFile]::ExtractToDirectory($archive, $src)
		Remove-Item -LiteralPath $archive
		$buildTree = Join-Path $work 'build'
		$oldPath = $env:PATH
		$env:PATH = "$ucrtBin;$oldPath"
		try {
			& $cmakeExe -S $src -B $buildTree -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPSMODERN_ROM_TESTS=OFF -DBUILD_TESTING=OFF | Out-Host
			if ($LASTEXITCODE) { throw 'cmake configure failed' }
			& $cmakeExe --build $buildTree --target psmodern | Out-Host
			if ($LASTEXITCODE) { throw 'cmake build failed' }
		} finally { $env:PATH = $oldPath }
		$exe = Require-File (Join-Path $buildTree 'psmodern.exe')
	}

	# Pinned third-party revisions must still be stated in docs/dependencies.md.
	$deps = Get-Content -LiteralPath (Join-Path $src 'docs/dependencies.md') -Raw
	$z80Commit = 'dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9'
	$zetaCommit = '42830b0e735cc3a83c7630e2dcf21949e8441c6d'
	foreach ($c in @($z80Commit, $zetaCommit)) {
		if (-not $deps.Contains($c)) { throw "docs/dependencies.md no longer mentions $c; update the script and the notices" }
	}

	$share = Join-Path $ucrtRoot 'share\licenses'
	$licenseSources = [ordered]@{
		'SDL2.txt'                  = Join-Path $share 'SDL2\LICENSE.txt'
		'winpthreads.txt'           = Join-Path $share 'winpthreads\COPYING'
		'GCC-runtime-exception.txt' = Join-Path $share 'gcc-libs\COPYING.RUNTIME'
		'GCC-GPL-3.0.txt'           = Join-Path $share 'gcc-libs\COPYING3'
		'redcode-z80-LGPL-3.0.txt'  = Join-Path $src 'third_party\redcode-z80\COPYING.LESSER'
		'Zeta-LGPL-3.0.txt'         = Join-Path $src 'third_party\zeta\COPYING.LESSER'
	}
	foreach ($p in $licenseSources.Values) { Require-File $p | Out-Null }
	$licenseFile = Require-File (Join-Path $src 'LICENSE')
	$readmeFile = Require-File (Join-Path $src 'tools\release\README.txt')

	# Package versions (shipped UCRT64 packages).
	$pkgNames = @('mingw-w64-ucrt-x86_64-SDL2', 'mingw-w64-ucrt-x86_64-gcc-libs', 'mingw-w64-ucrt-x86_64-libwinpthread')
	$packageLines = @(& $pacman -Q @pkgNames)
	if ($LASTEXITCODE -or $packageLines.Count -ne $pkgNames.Count) { throw 'pacman version query failed' }
	$packages = $packageLines -join "`r`n  "

	New-Item -ItemType Directory -Path $stage | Out-Null
	$stagedExe = Join-Path $stage 'psmodern.exe'
	& $strip --strip-debug -o $stagedExe $exe
	if ($LASTEXITCODE) { throw 'strip failed' }

	# Copy exactly the non-system DLLs ldd resolves, only from UCRT64, and
	# require exactly the expected set.
	$oldPath = $env:PATH
	$env:PATH = "$ucrtBin;$oldPath"
	try { $lddOutput = @(& $lddExe $stagedExe); $lddExit = $LASTEXITCODE } finally { $env:PATH = $oldPath }
	if ($lddExit) { throw 'ldd failed' }
	$dlls = @()
	foreach ($line in $lddOutput) {
		if ($line -match 'not found') { throw "Missing runtime dependency: $line" }
		if ($line -match '^\s*(\S+\.dll) => (\S+)') {
			$dllName = $Matches[1]; $dllPath = $Matches[2]
			if ($dllPath -like '/ucrt64/bin/*') { $dlls += $dllName }
			elseif ($dllPath -like '/c/WINDOWS/*') {
				if ($expectedDlls -contains $dllName) { throw "Runtime library resolved to a system path: $line" }
			} else { throw "Unexpected runtime dependency: $line" }
		} elseif ($line.Trim()) { throw "Unrecognised ldd line: $line" }
	}
	$found = @($dlls | Sort-Object -Unique)
	if ((($found | ForEach-Object { $_.ToLowerInvariant() }) -join ',') -ne (($expectedDlls | Sort-Object | ForEach-Object { $_.ToLowerInvariant() }) -join ',')) {
		throw "Unexpected runtime DLL set: $($found -join ', '); expected $($expectedDlls -join ', ')"
	}
	foreach ($dll in $found) { Copy-Item -LiteralPath (Require-File (Join-Path $ucrtBin $dll)) -Destination $stage }

	Copy-Item -LiteralPath $licenseFile -Destination (Join-Path $stage 'LICENSE.txt')
	Copy-Item -LiteralPath $readmeFile -Destination (Join-Path $stage 'README.txt')
	$licenses = Join-Path $stage 'licenses'
	New-Item -ItemType Directory -Path $licenses | Out-Null
	foreach ($key in $licenseSources.Keys) { Copy-Item -LiteralPath $licenseSources[$key] -Destination (Join-Path $licenses $key) }

@"
PSModern $version - third-party notices

PSModern is free software under the GNU General Public License version 3 or
later (LICENSE.txt).

Corresponding source:
  PSModern  $github
            commit $head
            (the tag v$version points to this commit)

Vendored libraries (their source is part of the PSModern source above):
  redcode/Z80  commit $z80Commit
               Z80 CPU core, LGPL-3.0-or-later (licenses\redcode-z80-LGPL-3.0.txt)
               https://github.com/redcode/Z80
  redcode/Zeta commit $zetaCommit
               header subset, LGPL-3.0 (licenses\Zeta-LGPL-3.0.txt)
               https://github.com/redcode/Zeta
Both are linked statically into psmodern.exe. The LGPL-3.0 supplements the
GNU GPL version 3 (LICENSE.txt, licenses\GCC-GPL-3.0.txt).

Bundled runtime libraries, MSYS2 UCRT64 builds (license texts in licenses\):
  SDL2.dll             Simple DirectMedia Layer, zlib license (licenses\SDL2.txt)
  libwinpthread-1.dll  mingw-w64 winpthreads, MIT-style license (licenses\winpthreads.txt)
  libgcc_s_seh-1.dll   GCC runtime library, GPL version 3 with the GCC Runtime
  libstdc++-6.dll      Library Exception 3.1 (licenses\GCC-runtime-exception.txt,
                       licenses\GCC-GPL-3.0.txt)

Package versions:
  $packages

No Phantasy Star data is included; bring your own cartridge dump.
"@ | Set-Content -Encoding ascii -LiteralPath (Join-Path $stage 'THIRD-PARTY.txt')

	# The staged program must report its version with only System32 on PATH.
	$info = New-Object System.Diagnostics.ProcessStartInfo
	$info.FileName = $stagedExe
	$info.Arguments = '--version'
	$info.UseShellExecute = $false
	$info.RedirectStandardOutput = $true
	$info.RedirectStandardError = $true
	$info.CreateNoWindow = $true
	$info.EnvironmentVariables['PATH'] = Join-Path $env:SystemRoot 'System32'
	$process = [Diagnostics.Process]::Start($info)
	if (-not $process.WaitForExit(30000)) { $process.Kill(); throw 'The staged psmodern.exe --version did not finish in 30 seconds' }
	$versionOutput = $process.StandardOutput.ReadToEnd()
	$versionExit = $process.ExitCode
	$process.Dispose()
	if ($versionExit -ne 0 -or $versionOutput.TrimEnd("`r", "`n") -cne "PSModern $version") {
		throw "The staged psmodern.exe --version printed '$($versionOutput.TrimEnd("`r", "`n"))' (exit $versionExit); expected 'PSModern $version'"
	}

	# Safety: no game data, saves, recordings, debug files or stray folders.
	$bad = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Name -like '*.sms' -or $_.Name -like '*.gg' -or $_.Name -like '*.sav' -or
		$_.Name -like '*.srm' -or $_.Name -like '*.options' -or $_.Name -like '*.lock' -or $_.Name -like '*.psrec' -or $_.Name -like '*.pdb' })
	if ($bad.Count) { throw "Forbidden files in the package: $($bad.FullName -join ', ')" }
	$dirs = @(Get-ChildItem -LiteralPath $stage -Recurse -Directory | Where-Object { $_.Name -ne 'licenses' -or $_.Parent.FullName -ne $stage })
	if ($dirs.Count) { throw "Unexpected folders in the package: $($dirs.FullName -join ', ')" }

	# Name entries explicitly: PowerShell 5.1's archive APIs write '\' separators.
	Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
	$archiveFile = [IO.Compression.ZipFile]::Open($partial, [IO.Compression.ZipArchiveMode]::Create)
	try {
		foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
			$entry = "$name/" + $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
			[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archiveFile, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
		}
	} finally { $archiveFile.Dispose() }
	Move-Item -LiteralPath $partial -Destination $zip
	$succeeded = $true
} finally {
	if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
	if (-not $succeeded -and (Test-Path -LiteralPath $stage)) { Remove-Item -LiteralPath $stage -Recurse -Force }
	if (-not $KeepBuild -and (Test-Path -LiteralPath $work)) { Remove-Item -LiteralPath $work -Recurse -Force }
}

Write-Output "HEAD:          $head"
Write-Output "exe SHA-256:   $((Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash)"
Write-Output "zip SHA-256:   $((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)"
Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($stage.Length + 1) } | Write-Output
Write-Output "Package: $zip ($((Get-Item -LiteralPath $zip).Length) bytes)"
Write-Output "Reminder: the tag v$version must point to $head before the release is published."
