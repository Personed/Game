param(
  [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
  [string]$PublicKeyPath = (Join-Path $PSScriptRoot '..\..\release-assets\client\Keys\dayz.bikey')
)

$ErrorActionPreference = 'Stop'
$expectedKeyHash = 'C1B027CCCC9136767AC11E66AEDB347030CAEB444502BF5CC300F17D7C544B22'
$keyHash = (Get-FileHash -LiteralPath $PublicKeyPath -Algorithm SHA256).Hash
if ($keyHash -ne $expectedKeyHash) { throw 'Self-check needs the real 157-byte Keys/dayz.bikey file.' }

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("dayz-package-test-" + [guid]::NewGuid().ToString('N'))
$packager = Join-Path $PSScriptRoot 'package-release.ps1'
$sevenZip = Get-Command '7z.exe' -ErrorAction SilentlyContinue
$payloadHashes = @{}

function Write-FakePe([string]$Path, [int]$Machine) {
  $bytes = New-Object byte[] 512
  $bytes[0] = 0x4d
  $bytes[1] = 0x5a
  [System.Buffer]::BlockCopy([BitConverter]::GetBytes([int]0x80), 0, $bytes, 0x3c, 4)
  [System.Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]0x00004550), 0, $bytes, 0x80, 4)
  [System.Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]$Machine), 0, $bytes, 0x84, 2)
  [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Write-PackageFixture([string]$Role) {
  $assetRoot = Join-Path $fixture "release-assets\$Role"
  foreach ($directory in @('Addons', 'dta', 'Keys')) {
    New-Item -ItemType Directory -Path (Join-Path $assetRoot $directory) -Force | Out-Null
  }
  Copy-Item -LiteralPath $PublicKeyPath -Destination (Join-Path $assetRoot 'Keys\dayz.bikey')
  foreach ($directory in @('Addons', 'dta')) {
    $pbo = Join-Path (Join-Path $assetRoot $directory) 'fixture.pbo'
    [System.IO.File]::WriteAllBytes($pbo, (New-Object byte[] 256))
    [System.IO.File]::WriteAllBytes("$pbo.dayz.bisign", (New-Object byte[] 32))
  }
  $payload = [System.Text.Encoding]::UTF8.GetBytes("tiny-lfs-payload-$Role")
  $oid = [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($payload)).Replace('-', '').ToLowerInvariant()
  $payloadPath = Join-Path (Join-Path $assetRoot 'dta') 'product.bin'
  $pointer = "version https://git-lfs.github.com/spec/v1`noid sha256:$oid`nsize $($payload.Length)`n"
  [System.IO.File]::WriteAllText($payloadPath, $pointer, [System.Text.Encoding]::ASCII)
  $objectPath = Join-Path (Join-Path (Join-Path (Join-Path $fixture '.git\lfs\objects') $oid.Substring(0, 2)) $oid.Substring(2, 2)) $oid
  New-Item -ItemType Directory -Path (Split-Path -Parent $objectPath) -Force | Out-Null
  [System.IO.File]::WriteAllBytes($objectPath, $payload)
  $payloadHashes[$Role] = $oid

  $binaryRoot = Join-Path $fixture "binaries\$Role"
  New-Item -ItemType Directory -Path $binaryRoot -Force | Out-Null
  if ($Role -eq 'client') {
    Write-FakePe (Join-Path $binaryRoot 'DayZ.exe') 0x8664
    $shaderRoot = Join-Path $fixture 'lib\d3d11\ShaderSources_4_0'
    New-Item -ItemType Directory -Path $shaderRoot -Force | Out-Null
    foreach ($shader in @('VS.hlsl', 'PS.hlsl', 'FPShaders.hlsl')) {
      [System.IO.File]::WriteAllText((Join-Path $shaderRoot $shader), 'fixture')
    }
  }
  else {
    Write-FakePe (Join-Path $binaryRoot 'DayZServer.exe') 0x8664
    $mission = Join-Path $assetRoot 'MPMissions\dayz_Auto.ChernarusPlus'
    New-Item -ItemType Directory -Path $mission -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $mission 'mission.sqm'), 'fixture')
    [System.IO.File]::WriteAllText((Join-Path $assetRoot 'serverDZ.cfg'), 'class Missions {};')
    foreach ($dataFile in @('spawnpoints_players.bin', 'proxyexport.bin')) {
      [System.IO.File]::WriteAllText((Join-Path $assetRoot $dataFile), 'fixture')
    }
  }
}

try {
  New-Item -ItemType Directory -Path $fixture -Force | Out-Null
  git -C $fixture init -q
  if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the temporary LFS cache fixture.' }
  foreach ($role in @('client', 'server')) {
    Write-PackageFixture $role
    $stage = Join-Path $fixture "stage\$role"
    $manifest = Join-Path $fixture "manifests\$role.txt"
    $binaryRoot = Join-Path $fixture "binaries\$role"
    $arguments = @{
      Role = $role
      BinaryDirectory = $binaryRoot
      StageDirectory = ".\stage\$role"
      ManifestPath = ".\manifests\$role.txt"
      RepositoryRoot = $fixture
    }
    if ($sevenZip) {
      $archive = Join-Path $fixture "archives\DayZ-$role-test.zip"
      $arguments.ArchivePath = $archive
      $arguments.ChecksumPath = "$archive.parts.sha256"
      $arguments.SevenZipPath = $sevenZip.Source
      $arguments.VolumeSizeBytes = 512
    }
    Push-Location $fixture
    try {
      $result = & $packager @arguments
    }
    finally {
      Pop-Location
    }
    if ($result -notmatch '^[0-9a-f]{64}$') { throw "$role manifest hash was not returned." }
    if ((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $result) {
      throw "$role returned manifest hash does not match its file."
    }
    if ((Get-FileHash -LiteralPath (Join-Path $stage 'dta\product.bin') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $payloadHashes[$role]) {
      throw "$role Git LFS pointer did not resolve to its cached object."
    }
    $stagedManifest = [System.IO.File]::ReadAllBytes((Join-Path $stage 'content-manifest.txt'))
    $externalManifest = [System.IO.File]::ReadAllBytes($manifest)
    if ([Convert]::ToBase64String($stagedManifest) -ne [Convert]::ToBase64String($externalManifest)) {
      throw "$role staged and separate manifests differ."
    }
    $lines = [System.IO.File]::ReadAllLines($manifest, [System.Text.Encoding]::UTF8)
    if ($lines.Count -eq 0) { throw "$role manifest is unexpectedly empty." }
    if ($stagedManifest.Length -ge 3 -and $stagedManifest[0] -eq 0xef -and $stagedManifest[1] -eq 0xbb -and $stagedManifest[2] -eq 0xbf) {
      throw "$role manifest must be UTF-8 without a BOM."
    }
    if ([System.Text.Encoding]::UTF8.GetString($stagedManifest).Contains("`r")) { throw "$role manifest must use LF line endings." }
    $paths = [string[]]@($lines | ForEach-Object { ($_ -split "`t", 3)[2] })
    $sortedPaths = [string[]]$paths.Clone()
    [Array]::Sort($sortedPaths, [System.StringComparer]::Ordinal)
    if ([string]::Join("`n", $paths) -cne [string]::Join("`n", $sortedPaths)) {
      throw "$role manifest entries are not ordinally sorted."
    }
    if ($paths -contains 'content-manifest.txt') { throw 'Manifest must exclude itself.' }
    if ($sevenZip) {
      $parts = @(Get-ChildItem -LiteralPath (Split-Path -Parent $archive) -File -Filter ((Split-Path -Leaf $archive) + '.*') |
        Where-Object { $_.Name -match '\.\d{3}$' })
      if (-not $parts -or -not (Test-Path -LiteralPath "$archive.parts.sha256" -PathType Leaf)) {
        throw "$role archive/checksum self-check failed."
      }
      $checksums = [System.IO.File]::ReadAllLines("$archive.parts.sha256")
      if ($checksums.Count -ne $parts.Count) { throw "$role checksum count does not match its ZIP parts." }
      foreach ($line in $checksums) {
        $match = [regex]::Match($line, '^([0-9a-f]{64})  (.+)$')
        if (-not $match.Success) { throw "$role checksum file is malformed." }
        $partPath = Join-Path (Split-Path -Parent $archive) $match.Groups[2].Value
        if ((Get-FileHash -LiteralPath $partPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $match.Groups[1].Value) {
          throw "$role ZIP part checksum does not match."
        }
      }
    }
  }

  $negativeArguments = @{
    RepositoryRoot = $fixture
    Role = 'server'
    BinaryDirectory = (Join-Path $fixture 'binaries\server')
    StageDirectory = (Join-Path $fixture 'stage\wrong-architecture')
    ManifestPath = (Join-Path $fixture 'manifests\wrong-architecture.txt')
  }
  $serverExe = Join-Path $negativeArguments.BinaryDirectory 'DayZServer.exe'
  Write-FakePe $serverExe 0x014c
  try {
    & $packager @negativeArguments | Out-Null
    throw 'The packager accepted a 32-bit server.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'Wrong PE architecture.*0x014C') { throw }
  }
  Write-FakePe $serverExe 0x8664

  $shader = Join-Path $fixture 'lib\d3d11\ShaderSources_4_0\PS.hlsl'
  [System.IO.File]::WriteAllBytes($shader, (New-Object byte[] 0))
  $negativeArguments.Role = 'client'
  $negativeArguments.BinaryDirectory = Join-Path $fixture 'binaries\client'
  $negativeArguments.StageDirectory = Join-Path $fixture 'stage\empty-shader'
  $negativeArguments.ManifestPath = Join-Path $fixture 'manifests\empty-shader.txt'
  try {
    & $packager @negativeArguments | Out-Null
    throw 'The packager accepted an empty shader source.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'Missing or empty Direct3D 11 shader source: PS\.hlsl') { throw }
  }
  [System.IO.File]::WriteAllText($shader, 'fixture')

  $looseCache = Join-Path $fixture 'release-assets\client\bin\Shaders_DefVS.shdc'
  New-Item -ItemType Directory -Path (Split-Path -Parent $looseCache) -Force | Out-Null
  [System.IO.File]::WriteAllText($looseCache, 'fixture')
  $negativeArguments.StageDirectory = Join-Path $fixture 'stage\loose-cache'
  $negativeArguments.ManifestPath = Join-Path $fixture 'manifests\loose-cache.txt'
  try {
    & $packager @negativeArguments | Out-Null
    throw 'The packager accepted a generated shader cache.'
  }
  catch {
    if ($_.Exception.Message -notmatch 'Generated shader cache must not be packaged') { throw }
  }
  Remove-Item -LiteralPath $looseCache

  if ($sevenZip) {
    $badArchive = Join-Path $fixture 'archives\checksum-collision.zip'
    $badArguments = @{
      Role = 'client'
      BinaryDirectory = (Join-Path $fixture 'binaries\client')
      StageDirectory = '.\stage\checksum-collision'
      ManifestPath = '.\manifests\checksum-collision.txt'
      RepositoryRoot = $fixture
      ArchivePath = $badArchive
      ChecksumPath = "$badArchive.001"
      SevenZipPath = $sevenZip.Source
      VolumeSizeBytes = 512
    }
    $collisionRejected = $false
    Push-Location $fixture
    try {
      & $packager @badArguments | Out-Null
    }
    catch {
      if ($_.Exception.Message -notmatch 'Checksum output cannot overwrite') { throw }
      $collisionRejected = $true
    }
    finally {
      Pop-Location
    }
    if (-not $collisionRejected) { throw 'The packager accepted a checksum path that would overwrite a ZIP volume.' }
    if (Test-Path -LiteralPath "$badArchive.001") { throw 'The checksum collision check created an archive volume.' }
  }

  Write-Output 'Packaging self-check passed for client and server fixtures.'
}
finally {
  if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
