# search for msbuild, the loaction of vswhere is guarenteed to be consistent
$MSBuildPath = (&"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe) | Out-String
$SevenZipPath = Resolve-Path -Path "7za.exe"

Set-Alias msbuild $MSBuildPath.Trim()
Set-Alias seven  $SevenZipPath

msbuild ObsExpressCpp.sln -p:Configuration=Release -p:Platform=x64

$ReleaseDir = Resolve-Path -Path "build64/rundir/MinSizeRel"
$BinDir = Resolve-Path -Path "$ReleaseDir/bin/64bit"

Remove-Item -Path "$BinDir/*.pdb"
Remove-Item -Path "release.zip"
seven a release.zip -y -mx9 `"${ReleaseDir}/*`"