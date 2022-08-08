# search for msbuild, the loaction of vswhere is guarenteed to be consistent
$MSBuildPath = (&"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe) | Out-String
$SevenZipPath = Resolve-Path -Path "7za.exe"

Set-Alias msbuild $MSBuildPath.Trim()
Set-Alias seven  $SevenZipPath

msbuild ObsExpressCpp.sln -p:Configuration=Release -p:Platform=x64

$ReleaseDir = Resolve-Path -Path "build64/rundir/MinSizeRel"
$BinDir = Resolve-Path -Path "$ReleaseDir/bin/64bit"

# remove non en-US locale's
$localeFiles = Get-ChildItem -Path $ReleaseDir -Filter *.ini -Recurse -ErrorAction SilentlyContinue -Force
foreach($file in $localeFiles)
{
    if ($file.FullName -notmatch 'en-US.ini$') {
        Remove-Item -Path $file.FullName -ErrorAction Ignore
    }
}


# create final zip
Remove-Item -Path "$BinDir/*.pdb" -ErrorAction Ignore
Remove-Item -Path "obs-express.zip" -ErrorAction Ignore
seven a obs-express.zip -y -mx9 `"${ReleaseDir}/*`"
