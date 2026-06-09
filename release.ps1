<#
.SYNOPSIS
    Build, package, and publish an F1 FFB release to GitHub in one step.

.DESCRIPTION
    Stamps the version into src/types.h, repoints the README download link,
    builds Release, commits + pushes, creates and pushes the git tag, zips the
    self-contained exe (+ README + LICENSE), and publishes a GitHub Release with
    the zip attached.

    Auth: uses the GitHub CLI (gh). The token is pulled from Git Credential
    Manager at run time and passed via GH_TOKEN, so nothing is stored in
    plaintext. Requires: gh (winget install GitHub.cli), cmake, and a build/
    folder already configured (run build.bat once first).

.PARAMETER Version
    The git tag for the release, e.g. v1.3-beta.

.PARAMETER Title
    Display name / APP_VERSION value, e.g. "v1.3 Beta". Derived from -Version
    if omitted (v1.3-beta -> "v1.3 Beta").

.PARAMETER NotesFile
    Path to a Markdown file for the release notes. If omitted, gh auto-generates
    notes from the commits since the last tag.

.PARAMETER FullRelease
    Publish as the "Latest" release instead of a pre-release (default is pre-release).

.EXAMPLE
    ./release.ps1 -Version v1.3-beta
.EXAMPLE
    ./release.ps1 -Version v1.3-beta -NotesFile notes.md -FullRelease
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Title,
    [string]$NotesFile,
    [switch]$FullRelease
)

$ErrorActionPreference = 'Stop'
$repo = 'therealbeno82/F125-Telemetry-FFB'
$root = $PSScriptRoot

# Derive a display title from the tag if not supplied: v1.3-beta -> "v1.3 Beta"
if (-not $Title) {
    $Title = (($Version -split '-') | ForEach-Object {
        if ($_ -match '^v?\d') { $_ } else { (Get-Culture).TextInfo.ToTitleCase($_) }
    }) -join ' '
}

Write-Host "== F1 FFB release  tag=$Version  title=`"$Title`" ==" -ForegroundColor Cyan

# 1. Make sure gh is reachable (winget adds it to PATH; refresh this session).
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI not found. Install it with:  winget install GitHub.cli"
}

# 2. Pull the cached GitHub token from Git Credential Manager (kept in-memory).
$tmp = [IO.Path]::GetTempFileName()
[IO.File]::WriteAllText($tmp, "protocol=https`nhost=github.com`n`n")
$cred = cmd /c "git credential fill < `"$tmp`" 2>nul"
Remove-Item $tmp -Force
$env:GH_TOKEN = ($cred | Where-Object { $_ -like 'password=*' }) -replace '^password=',''
if (-not $env:GH_TOKEN) { throw "Could not read a GitHub token from Git Credential Manager." }

# 3. Stamp APP_VERSION in src/types.h.
$typesPath = Join-Path $root 'src/types.h'
# -Encoding UTF8 is REQUIRED: Windows PowerShell 5.1 defaults Get-Content to the
# system ANSI codepage, which misreads UTF-8 (em-dash, box-drawing chars) and,
# once written back as UTF-8, double-encodes the whole file into mojibake.
$types = Get-Content $typesPath -Raw -Encoding UTF8
$types = $types -replace '(#define APP_VERSION ")[^"]*(")', "`${1}$Title`${2}"
[IO.File]::WriteAllText($typesPath, $types, (New-Object System.Text.UTF8Encoding($false)))

# 4. Repoint the README download link to this version.
$readmePath = Join-Path $root 'README.md'
$readme = Get-Content $readmePath -Raw -Encoding UTF8   # UTF8 required (see note above)
$readme = $readme -replace 'releases/download/[^/)]+/F1FFB-[^)]+\.zip',
                           "releases/download/$Version/F1FFB-$Version.zip"
[IO.File]::WriteAllText($readmePath, $readme, (New-Object System.Text.UTF8Encoding($false)))

# 5. Build Release.
Write-Host "Building Release..." -ForegroundColor Cyan
$buildDir = Join-Path $root 'build'
if (-not (Test-Path $buildDir)) { throw "No build/ folder. Run build.bat once to configure CMake first." }
cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }
$exe = Join-Path $root 'F1FFB.exe'
if (-not (Test-Path $exe)) { throw "Build did not produce F1FFB.exe at the project root." }

# 6. Commit the stamp + README change (if any) and push.
Push-Location $root
try {
    git add src/types.h README.md
    git diff --cached --quiet
    if ($LASTEXITCODE -ne 0) {
        git commit -m "Release $Title"
        if ($LASTEXITCODE -ne 0) { throw "git commit failed." }
    } else {
        Write-Host "No version/README changes to commit." -ForegroundColor Yellow
    }
    git push origin HEAD
    if ($LASTEXITCODE -ne 0) { throw "git push failed." }

    # 7. Tag + push the tag.
    if (git tag --list $Version) {
        Write-Host "Tag $Version already exists locally; reusing it." -ForegroundColor Yellow
    } else {
        git tag -a $Version -m $Title
        if ($LASTEXITCODE -ne 0) { throw "git tag failed." }
    }
    git push origin $Version
    if ($LASTEXITCODE -ne 0) { throw "git push tag failed." }
}
finally { Pop-Location }

# 8. Package the self-contained exe (+ README + LICENSE) into dist/.
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "F1FFB-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
$files = @('F1FFB.exe','README.md','LICENSE') | ForEach-Object { Join-Path $root $_ }
Compress-Archive -Path $files -DestinationPath $zip -CompressionLevel Optimal
Write-Host ("Packaged {0} ({1:N0} bytes)" -f (Split-Path $zip -Leaf), (Get-Item $zip).Length) -ForegroundColor Cyan

# 9. Publish the GitHub Release with the zip attached.
$ghArgs = @('release','create', $Version, $zip, '--repo', $repo, '--title', $Title)
if ($NotesFile -and (Test-Path $NotesFile)) { $ghArgs += @('--notes-file', $NotesFile) }
else { $ghArgs += '--generate-notes' }
if (-not $FullRelease) { $ghArgs += '--prerelease' }
gh @ghArgs
if ($LASTEXITCODE -ne 0) { throw "gh release create failed." }

Write-Host "Done -> https://github.com/$repo/releases/tag/$Version" -ForegroundColor Green
