# search for msbuild, the loaction of vswhere is guarenteed to be consistent
$MSBuildPath = (&"${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe) | Out-String
$SevenZipPath = Resolve-Path -Path "7za.exe"
$RcEditPath = Resolve-Path -Path "rcedit.exe"

Set-Alias msbuild $MSBuildPath.Trim()
Set-Alias seven $SevenZipPath
Set-Alias rce $RcEditPath

$verHeader = Get-Content -Path .\version.h
$verStart = $verHeader.IndexOf('"') + 1
$verEnd = $verHeader.IndexOf('"', $verStart)
$version = $verHeader.Substring($verStart, $verEnd - $verStart)

Write-Host "Creating release for obs-express v$version"

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

rce "$BinDir/obs-express.exe" `
--set-file-version $version `
--set-product-version $version `
--set-version-string "ProductName" "obs-express" `
--set-version-string "CompanyName" "Caelan Sayler" `
--set-version-string "LegalCopyright" "Copyright 2022-2023 Caelan Sayler" `
--set-version-string "FileDescription" "obs-express command line screen recording utility"

# create final zip
Remove-Item -Path "$BinDir/*.pdb" -ErrorAction Ignore
Remove-Item -Path "obs-express.zip" -ErrorAction Ignore
seven a obs-express.zip -y -mx9 `"${ReleaseDir}/*`"
