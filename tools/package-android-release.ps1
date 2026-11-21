# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Builds PSModern for Android from the committed HEAD and packages a signed release APK.

.DESCRIPTION
HEAD is exported with git archive into a temporary folder under -OutputDir,
built with Gradle (assembleRelease) against the pinned Android SDK/NDK and
extracted SDL2 2.32.10 source directory, and signed with the release keystore.
The script refuses a dirty git tree (unless -AllowDirty is given), an output
folder inside the repository, and any pre-existing output file or work folder.
It verifies apksigner (refusing the Android Debug certificate), 16 KiB
zipalign, aapt2 badging (package name, versionCode/versionName, non-debuggable),
required license/notice assets, absence of forbidden game/save files, and for
both ARM ABIs the Build ID match and absence of Z80/oracle symbols in libmain.so.
The APK is written as <name>.apk.partial and renamed only after all checks pass.

.PARAMETER OutputDir
Folder outside the repository that receives the signed APK and temporary build folder.
.PARAMETER Sdl2Dir
Path to the extracted SDL2 2.32.10 source directory (PSMODERN_SDL2).
.PARAMETER CredentialsFile
Optional key=value file outside the repository containing PSMODERN_KEYSTORE,
PSMODERN_KEY_ALIAS, PSMODERN_KEYSTORE_PASSWORD and PSMODERN_KEY_PASSWORD.
.PARAMETER Keystore
Path to the release keystore file (if not supplied via -CredentialsFile or environment).
.PARAMETER KeyAlias
Key alias inside the release keystore (defaults to 'psmodern').
.PARAMETER AndroidHome
Path to the Android SDK root (defaults to $env:ANDROID_HOME or $env:LOCALAPPDATA\Android\Sdk).
.PARAMETER KeepBuild
Keeps the temporary export and Gradle build folder after a successful run.
.PARAMETER AllowDirty
For testing the script only; never for a published APK. Allows a dirty tree and
adds a -dirty suffix to the APK name.
.PARAMETER UseWorkingTree
Testing shortcut only: builds from the working tree instead of git archive HEAD.
Requires -AllowDirty.
.PARAMETER VersionOverride
Hidden test aid: overrides the version read from CMakeLists.txt. Requires -AllowDirty.
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$OutputDir,
	[string]$Sdl2Dir = $env:PSMODERN_SDL2,
	[string]$CredentialsFile = '',
	[string]$Keystore = $env:PSMODERN_KEYSTORE,
	[string]$KeyAlias = $(if ($env:PSMODERN_KEY_ALIAS) { $env:PSMODERN_KEY_ALIAS } else { 'psmodern' }),
	[string]$AndroidHome = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }),
	[switch]$AllowDirty,
	[switch]$UseWorkingTree,
	[switch]$KeepBuild,
	[string]$VersionOverride = ''
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$expectedNdk = '28.2.13676358'
$expectedBuildTools = '36.0.0'

function Resolve-Argument([string]$path) {
	return $PSCmdlet.GetUnresolvedProviderPathFromPSPath($path).TrimEnd('\', '/')
}
function Require-File([string]$path) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required file: $path" }
	return $path
}
function Require-Dir([string]$path) {
	if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Missing required directory: $path" }
	return $path
}
function Invoke-Tool([string]$filePath, [string[]]$arguments, [string]$workingDirectory = '', [hashtable]$extraEnv = @{}) {
	$info = New-Object System.Diagnostics.ProcessStartInfo
	$info.FileName = $filePath
	$info.Arguments = ($arguments | ForEach-Object {
		if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
	}) -join ' '
	if ($workingDirectory) { $info.WorkingDirectory = $workingDirectory }
	$info.UseShellExecute = $false
	$info.RedirectStandardOutput = $true
	$info.RedirectStandardError = $true
	$info.CreateNoWindow = $true
	foreach ($k in $extraEnv.Keys) { $info.EnvironmentVariables[$k] = $extraEnv[$k] }
	$proc = [Diagnostics.Process]::Start($info)
	$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
	$stderrTask = $proc.StandardError.ReadToEndAsync()
	$proc.WaitForExit()
	return [PSCustomObject]@{
		ExitCode = $proc.ExitCode
		StdOut   = $stdoutTask.Result
		StdErr   = $stderrTask.Result
	}
}

if ($UseWorkingTree -and -not $AllowDirty) { throw '-UseWorkingTree requires -AllowDirty (testing only)' }
if ($VersionOverride -and -not $AllowDirty) { throw '-VersionOverride requires -AllowDirty' }

# Load signing credentials (never printed).
$keystorePassword = $env:PSMODERN_KEYSTORE_PASSWORD
$keyPassword = $env:PSMODERN_KEY_PASSWORD
if ($CredentialsFile) {
	$credPath = Require-File (Resolve-Argument $CredentialsFile)
	foreach ($line in Get-Content -LiteralPath $credPath) {
		if ($line -match '^\s*#' -or -not $line.Trim()) { continue }
		if ($line -match '^([A-Z_]+)=(.*)$') {
			switch ($Matches[1]) {
				'PSMODERN_KEYSTORE'          { if (-not $Keystore) { $Keystore = $Matches[2] } }
				'PSMODERN_KEY_ALIAS'         { $KeyAlias = $Matches[2] }
				'PSMODERN_KEYSTORE_PASSWORD' { $keystorePassword = $Matches[2] }
				'PSMODERN_KEY_PASSWORD'      { $keyPassword = $Matches[2] }
			}
		}
	}
}
if (-not $keyPassword) { $keyPassword = $keystorePassword }
if (-not $Keystore) { throw 'Release keystore is required (-Keystore, -CredentialsFile, or PSMODERN_KEYSTORE)' }
if (-not $keystorePassword) { throw 'Keystore password is required (-CredentialsFile or PSMODERN_KEYSTORE_PASSWORD)' }
$keystorePath = Require-File (Resolve-Argument $Keystore)

if (-not $Sdl2Dir) { throw 'SDL2 2.32.10 source directory is required (-Sdl2Dir or PSMODERN_SDL2)' }
$sdlPath = Require-Dir (Resolve-Argument $Sdl2Dir)
Require-File (Join-Path $sdlPath 'LICENSE.txt') | Out-Null

$sdkPath = Require-Dir (Resolve-Argument $AndroidHome)
$buildToolsDir = Require-Dir (Join-Path $sdkPath "build-tools\$expectedBuildTools")
$apksigner = Require-File (Join-Path $buildToolsDir 'apksigner.bat')
$zipalign  = Require-File (Join-Path $buildToolsDir 'zipalign.exe')
$aapt2     = Require-File (Join-Path $buildToolsDir 'aapt2.exe')
$ndkBin    = Require-Dir (Join-Path $sdkPath "ndk\$expectedNdk\toolchains\llvm\prebuilt\windows-x86_64\bin")
$readelf   = Require-File (Join-Path $ndkBin 'llvm-readelf.exe')
$llvmNm    = Require-File (Join-Path $ndkBin 'llvm-nm.exe')

# Clean git tree and commit.
$status = @(& git -C $repository status --porcelain)
if ($LASTEXITCODE) { throw 'git status failed' }
if ($status.Count -gt 0 -and -not $AllowDirty) { throw 'The git working tree is not clean; commit or stash first' }
$head = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -or $head -notmatch '^[0-9a-f]{40}$') { throw 'git rev-parse failed' }

# Version from HEAD (or working tree when -UseWorkingTree).
if ($UseWorkingTree) { $cmake = Get-Content -LiteralPath (Join-Path $repository 'CMakeLists.txt') -Raw }
else {
	$cmake = (@(& git -C $repository show 'HEAD:CMakeLists.txt')) -join "`n"
	if ($LASTEXITCODE) { throw 'git show HEAD:CMakeLists.txt failed' }
}
$version = ([regex]::Match($cmake, 'project\(PSModern VERSION ([0-9]+\.[0-9]+\.[0-9]+)')).Groups[1].Value
if ($VersionOverride) { $version = $VersionOverride }
if (-not $version) { throw 'Cannot read the version from CMakeLists.txt' }
$vm = [regex]::Match($version, '^(\d+)\.(\d+)\.(\d+)$')
if (-not $vm.Success) { throw "Invalid version: $version" }
$expectedVersionCode = [int]$vm.Groups[1].Value * 10000 + [int]$vm.Groups[2].Value * 100 + [int]$vm.Groups[3].Value

# Output paths.
$output = Resolve-Argument $OutputDir
if ($output.Equals($repository, [StringComparison]::OrdinalIgnoreCase) -or
	$output.StartsWith($repository + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'Output must be outside the repository'
}
$suffix = if ($AllowDirty) { '-dirty' } else { '' }
$name = "PSModern-$version-android-arm$suffix"
$apkOut = Join-Path $output "$name.apk"
$partial = "$apkOut.partial"
$work = Join-Path $output "PSModern-$version-android-build$suffix"
foreach ($p in @($apkOut, $partial, $work)) {
	if (Test-Path -LiteralPath $p) { throw "Output already exists: $p" }
}

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$succeeded = $false
try {
	New-Item -ItemType Directory -Force -Path $output | Out-Null
	New-Item -ItemType Directory -Path $work | Out-Null

	$src = Join-Path $work 'source'
	if ($UseWorkingTree) {
		New-Item -ItemType Directory -Path $src | Out-Null
		# Copy only tracked/clean source items needed for Android build.
		foreach ($item in @('CMakeLists.txt', 'LICENSE', 'cmake', 'src', 'android')) {
			$from = Join-Path $repository $item
			$to = Join-Path $src $item
			if (Test-Path -LiteralPath $from -PathType Container) {
				Copy-Item -LiteralPath $from -Destination $to -Recurse
				foreach ($junk in @('.gradle', '.cxx', 'build')) {
					$jp = Join-Path $to $junk
					if (Test-Path -LiteralPath $jp) { Remove-Item -LiteralPath $jp -Recurse -Force }
				}
			} else {
				Copy-Item -LiteralPath $from -Destination $to
			}
		}
	} else {
		$archive = Join-Path $work 'head.zip'
		& git -C $repository archive --format=zip -o $archive HEAD
		if ($LASTEXITCODE) { throw 'git archive failed' }
		[IO.Compression.ZipFile]::ExtractToDirectory($archive, $src)
		Remove-Item -LiteralPath $archive
	}

	$androidDir = Join-Path $src 'android'
	$gradlew = Require-File (Join-Path $androidDir 'gradlew.bat')
	$gradleArgs = @('assembleRelease', '--no-daemon', "-PPSMODERN_COMMIT=$head")
	if ($VersionOverride) { $gradleArgs += "-PPSMODERN_VERSION_OVERRIDE=$VersionOverride" }

	$buildEnv = @{
		ANDROID_HOME               = $sdkPath
		PSMODERN_SDL2              = $sdlPath
		PSMODERN_KEYSTORE          = $keystorePath
		PSMODERN_KEY_ALIAS         = $KeyAlias
		PSMODERN_KEYSTORE_PASSWORD = $keystorePassword
		PSMODERN_KEY_PASSWORD      = $keyPassword
	}
	Write-Output "Building Android release APK ($version, versionCode $expectedVersionCode)..."
	$gradleRun = Invoke-Tool $gradlew $gradleArgs $androidDir $buildEnv
	if ($gradleRun.ExitCode -ne 0) {
		Write-Output $gradleRun.StdOut
		Write-Output $gradleRun.StdErr
		throw "Gradle assembleRelease failed with exit code $($gradleRun.ExitCode)"
	}

	$builtApk = Require-File (Join-Path $androidDir 'build\outputs\apk\release\PSModern-release.apk')
	Copy-Item -LiteralPath $builtApk -Destination $partial

	# 1. Verify APK signature and ensure it is NOT the Android Debug certificate.
	$sigRun = Invoke-Tool 'cmd.exe' @('/c', $apksigner, 'verify', '--verbose', '--print-certs', $partial)
	if ($sigRun.ExitCode -ne 0) {
		throw "apksigner verify failed:`n$($sigRun.StdOut)`n$($sigRun.StdErr)"
	}
	if ($sigRun.StdOut -match 'CN=Android Debug') {
		throw 'APK is signed with the Android Debug certificate; refusing release package'
	}
	$certShaMatch = [regex]::Match($sigRun.StdOut, 'Signer #1 certificate SHA-256 digest:\s*([0-9a-fA-F]+)')
	$certSha = if ($certShaMatch.Success) { $certShaMatch.Groups[1].Value } else { 'unknown' }

	# 2. Verify 16 KiB zipalign (Android 15+ requirement).
	$alignRun = Invoke-Tool $zipalign @('-c', '-P', '16', '4', $partial)
	if ($alignRun.ExitCode -ne 0) {
		throw "zipalign -c -P 16 4 failed:`n$($alignRun.StdOut)`n$($alignRun.StdErr)"
	}

	# 3. Verify manifest metadata via aapt2 dump badging.
	$badgeRun = Invoke-Tool $aapt2 @('dump', 'badging', $partial)
	if ($badgeRun.ExitCode -ne 0) {
		throw "aapt2 dump badging failed:`n$($badgeRun.StdErr)"
	}
	if ($badgeRun.StdOut -notmatch "package: name='org\.psmodern\.game' versionCode='$expectedVersionCode' versionName='$version'") {
		throw "Unexpected APK badging header: $(($badgeRun.StdOut -split "`n")[0])"
	}
	if ($badgeRun.StdOut -match '(?m)^application-debuggable\b') {
		throw 'APK is marked application-debuggable; refusing release package'
	}

	# 4. Inspect APK contents: required files, notices, and zero forbidden files.
	$requiredEntries = @(
		'assets/PSModern-LICENSE.txt',
		'assets/SDL2-LICENSE.txt',
		'assets/LIBCXX-LICENSE.txt',
		'assets/THIRD-PARTY.txt',
		'lib/arm64-v8a/libmain.so',
		'lib/armeabi-v7a/libmain.so',
		'lib/arm64-v8a/libSDL2.so',
		'lib/armeabi-v7a/libSDL2.so',
		'lib/arm64-v8a/libc++_shared.so',
		'lib/armeabi-v7a/libc++_shared.so'
	)
	$verifyDir = Join-Path $work 'verify'
	New-Item -ItemType Directory -Path $verifyDir | Out-Null
	$zip = [IO.Compression.ZipFile]::OpenRead($partial)
	try {
		$entryMap = @{}
		foreach ($e in $zip.Entries) {
			$entryMap[$e.FullName] = $e
			if ($e.FullName -match '\.(sms|gg|sav|srm|options|lock|psrec|pdb)$') {
				throw "Forbidden file in APK: $($e.FullName)"
			}
		}
		foreach ($req in $requiredEntries) {
			if (-not $entryMap.ContainsKey($req)) { throw "Missing required APK entry: $req" }
		}
		$reader = New-Object System.IO.StreamReader($entryMap['assets/THIRD-PARTY.txt'].Open())
		try { $thirdParty = $reader.ReadToEnd() } finally { $reader.Dispose() }
		if ($thirdParty -notmatch [regex]::Escape("PSModern $version (Android)") -or
			$thirdParty -notmatch [regex]::Escape("commit $head")) {
			throw 'assets/THIRD-PARTY.txt does not match expected version and commit'
		}
		foreach ($abi in @('arm64-v8a', 'armeabi-v7a')) {
			$destSo = Join-Path $verifyDir "packaged-$abi-libmain.so"
			[IO.Compression.ZipFileExtensions]::ExtractToFile($entryMap["lib/$abi/libmain.so"], $destSo, $true)
		}
	} finally { $zip.Dispose() }

	# 5. For both ABIs, match Build ID with unstripped libmain.so and verify no Z80/oracle symbols.
	$cxxIntermediates = Require-Dir (Join-Path $androidDir 'build\intermediates\cxx')
	foreach ($abi in @('arm64-v8a', 'armeabi-v7a')) {
		$packagedSo = Join-Path $verifyDir "packaged-$abi-libmain.so"
		$unstrippedCandidates = @(Get-ChildItem -LiteralPath $cxxIntermediates -Recurse -Filter 'libmain.so' |
			Where-Object { $_.FullName -match ([regex]::Escape("\obj\$abi\libmain.so") + '$') })
		if ($unstrippedCandidates.Count -ne 1) {
			throw "Expected 1 unstripped libmain.so for $abi under $cxxIntermediates, found $($unstrippedCandidates.Count)"
		}
		$unstrippedSo = $unstrippedCandidates[0].FullName

		$pkgNotes = Invoke-Tool $readelf @('-n', $packagedSo)
		$unstrNotes = Invoke-Tool $readelf @('-n', $unstrippedSo)
		if ($pkgNotes.ExitCode -ne 0 -or $unstrNotes.ExitCode -ne 0) { throw "llvm-readelf failed for $abi" }
		$pkgId = ([regex]::Match($pkgNotes.StdOut, 'Build ID:\s*([0-9a-fA-F]+)')).Groups[1].Value
		$unstrId = ([regex]::Match($unstrNotes.StdOut, 'Build ID:\s*([0-9a-fA-F]+)')).Groups[1].Value
		if (-not $pkgId -or $pkgId -ne $unstrId) {
			throw "Build ID mismatch for ${abi}: packaged='$pkgId', unstripped='$unstrId'"
		}

		$nmRun = Invoke-Tool $llvmNm @('--demangle', $unstrippedSo)
		if ($nmRun.ExitCode -ne 0) { throw "llvm-nm failed for $abi" }
		if ($nmRun.StdOut -notmatch '\bSDL_main\b') { throw "SDL_main missing in $abi libmain.so" }
		if ($nmRun.StdOut -match '\bz80_[a-zA-Z0-9_]+|psmodern::oracle') {
			throw "Forbidden Z80/oracle symbol found in $abi libmain.so"
		}
		Write-Output "Verified ${abi}: Build ID $pkgId, SDL_main present, 0 Z80/oracle symbols"
	}

	Move-Item -LiteralPath $partial -Destination $apkOut
	$succeeded = $true
} finally {
	if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
	if (-not $KeepBuild -and (Test-Path -LiteralPath $work)) { Remove-Item -LiteralPath $work -Recurse -Force }
}

$apkSha = (Get-FileHash -LiteralPath $apkOut -Algorithm SHA256).Hash
$apkSize = (Get-Item -LiteralPath $apkOut).Length
Write-Output "HEAD:             $head"
Write-Output "Cert SHA-256:     $certSha"
Write-Output "APK SHA-256:      $apkSha"
Write-Output "Package:          $apkOut ($apkSize bytes)"
Write-Output "Reminder: the tag v$version must point to $head before the release is published."
