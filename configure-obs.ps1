Param(
    [String]$BuildDirectory = "build",
    #[String]$ObsBuildBranch = "master",
    [ValidateSet('x86', 'x64')]
    [String]$BuildArch = $(if (Test-Path variable:BuildArch) { "${BuildArch}" } else { ('x86', 'x64')[[System.Environment]::Is64BitOperatingSystem] }),
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel", "Debug")]
    [String]$BuildConfiguration = $(if (Test-Path variable:BuildConfiguration) { "${BuildConfiguration}" } else { "MinSizeRel" })
)

$ProgressPreference = "SilentlyContinue" # progress bar in powershell is slow af
$ErrorActionPreference = "Stop"

# This variable can be null in github actions
if ($PSScriptRoot -eq $null) {
    $PSScriptRoot = Resolve-Path -Path "."
} else {
    Set-Location $PSScriptRoot
}

#git clone -b master --single-branch --recursive https://github.com/obsproject/obs-studio.git
$CheckoutDir = Resolve-Path -Path "obs-studio"

. ${CheckoutDir}/CI/windows/01_install_dependencies.ps1 -BuildArch $BuildArch -Quiet

. ${CheckoutDir}/CI/include/build_support_windows.ps1

Set-Location $PSScriptRoot

# Configure multi-threading
$NumProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
if ( $NumProcessors -gt 1 ) {
    $env:UseMultiToolTask = $true
    $env:EnforceProcessCountAcrossBuilds = $true
}

# Configure CMAKE
$CmakePrefixPath = Resolve-Path -Path "obs-build-dependencies/windows-deps-${WindowsDepsVersion}-${BuildArch}"
$CefDirectory = Resolve-Path -Path "obs-build-dependencies/cef_binary_${WindowsCefVersion}_windows_${BuildArch}"
$MyVlcDirectory = Resolve-Path -Path "obs-build-dependencies/vlc-${WindowsVlcVersion}"
$BuildDirectoryActual = "${BuildDirectory}$(if (${BuildArch} -eq "x64") { "64" } else { "32" })"
$GeneratorPlatform = "$(if (${BuildArch} -eq "x64") { "x64" } else { "Win32" })"
$CmakeGenerator = "Visual Studio 17 2022"

$CmakeCommand = @(
    "-G", ${CmakeGenerator}
    "-DCMAKE_GENERATOR_PLATFORM=`"${GeneratorPlatform}`"",
    "-DCMAKE_SYSTEM_VERSION=`"${CmakeSystemVersion}`"",
    "-DCMAKE_PREFIX_PATH:PATH=`"${CmakePrefixPath}`"",
    "-DCEF_ROOT_DIR:PATH=`"${CefDirectory}`"",
    "-DVLC_PATH:PATH=`"${MyVlcDirectory}`"",
    "-DCMAKE_INSTALL_PREFIX=`"${BuildDirectoryActual}/install`"",
    "-DVIRTUALCAM_GUID=`"${Env:VIRTUALCAM-GUID}`"",
    "-DCOPIED_DEPENDENCIES=OFF",
    "-DCOPY_DEPENDENCIES=ON",

    # "-DENABLE_VLC=ON",
    # "-DENABLE_BROWSER=ON",
    # "-DTWITCH_CLIENTID=`"${Env:TWITCH_CLIENTID}`"",
    # "-DTWITCH_HASH=`"${Env:TWITCH_HASH}`"",
    # "-DRESTREAM_CLIENTID=`"${Env:RESTREAM_CLIENTID}`"",
    # "-DRESTREAM_HASH=`"${Env:RESTREAM_HASH}`"",
    # "-DYOUTUBE_CLIENTID=`"${Env:YOUTUBE_CLIENTID}`"",
    # "-DYOUTUBE_CLIENTID_HASH=`"${Env:YOUTUBE_CLIENTID_HASH}`"",
    # "-DYOUTUBE_SECRET=`"${Env:YOUTUBE_SECRET}`"",
    # "-DYOUTUBE_SECRET_HASH=`"${Env:YOUTUBE_SECRET_HASH}`"",
    # "-DBUILD_FOR_DISTRIBUTION=`"$(if (Test-Path Env:BUILD_FOR_DISTRIBUTION) { "ON" } else { "OFF" })`"",
    # "$(if (Test-Path Env:CI) { "-DOBS_BUILD_NUMBER=${Env:GITHUB_RUN_ID}" })",
    # "$(if (Test-Path Variable:$Quiet) { "-Wno-deprecated -Wno-dev --log-level=ERROR" })",

    "-DENABLE_AJA=OFF",
    "-DENABLE_UI=OFF",
    "-DENABLE_SCRIPTING=OFF",
    "-DENABLE_DECKLINK=OFF",
    "-DENABLE_VIRTUALCAM=OFF",
    "-DENABLE_BROWSER=OFF",
    "-DENABLE_VLC=OFF",
    "-DENABLE_VST=OFF"
)

Invoke-External cmake -S ${CheckoutDir} -B "${BuildDirectoryActual}" @CmakeCommand

# Build OBS
Invoke-External cmake --build "${BuildDirectoryActual}" --config ${BuildConfiguration}

# Copy required files into output directory
$BinDir = Resolve-Path -Path "${BuildDirectoryActual}/rundir/${BuildConfiguration}/bin/64bit"
$DependenciesZip = Resolve-Path -Path "${CmakePrefixPath}.zip"

# OBS dependencies
Invoke-Expression "7z x `"$DependenciesZip`" -y -o`"$BinDir`" bin/*" 
Move-Item -Path "$BinDir/bin/*" -Destination $BinDir -ErrorAction Ignore
Remove-Item -Path "$BinDir/bin" -Recurse -ErrorAction Ignore
Remove-Item -Path "$BinDir/*.lib" -ErrorAction Ignore

# OBS-express dependencies
Copy-Item ./tracker.png -Destination $BinDir