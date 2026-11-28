param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('client', 'server')]
  [string]$Role,

  [Parameter(Mandatory = $true)]
  [string]$BinaryDirectory,

  [Parameter(Mandatory = $true)]
  [string]$StageDirectory,

  [Parameter(Mandatory = $true)]
  [string]$ManifestPath,

  [string]$RepositoryRoot,
  [string]$ArchivePath,
  [string]$ChecksumPath,
  [string]$SevenZipPath,

  [ValidateRange(1, 1992294400)]
  [long]$VolumeSizeBytes = 1992294400
)

$ErrorActionPreference = 'Stop'
if (-not $RepositoryRoot) {
  $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
$manifestName = 'content-manifest.txt'
$expectedBikeyHash = 'C1B027CCCC9136767AC11E66AEDB347030CAEB444502BF5CC300F17D7C544B22'
$expectedMachine = 0x8664
$expectedExecutable = if ($Role -eq 'client') { 'DayZ.exe' } else { 'DayZServer.exe' }

function Get-FirstBytesText([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $buffer = New-Object byte[] 256
    $count = $stream.Read($buffer, 0, $buffer.Length)
    return [System.Text.Encoding]::ASCII.GetString($buffer, 0, $count)
  }
  finally {
    $stream.Dispose()
  }
}

function Get-FullPath([string]$Path) {
  if (-not [System.IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $PWD.Path $Path }
  return [System.IO.Path]::GetFullPath($Path)
}

function Resolve-LfsFile([string]$Path) {
  $header = Get-FirstBytesText $Path
  if ($header -notmatch '^version https://git-lfs\.github\.com/spec/v1') {
    return $Path
  }
  $oidMatch = [regex]::Match($header, '(?m)^oid sha256:([0-9a-f]{64})\r?$')
  $sizeMatch = [regex]::Match($header, '(?m)^size ([0-9]+)\r?$')
  if (-not $oidMatch.Success -or -not $sizeMatch.Success) {
    throw "Malformed Git LFS pointer: $Path"
  }

  $oid = $oidMatch.Groups[1].Value
  $objectSize = [long]$sizeMatch.Groups[1].Value
  $gitPath = & git -C $RepositoryRoot rev-parse --git-path lfs/objects
  if ($LASTEXITCODE -ne 0) { throw 'Could not locate the Git LFS object cache.' }
  $cacheRoot = [string]$gitPath
  if (-not [System.IO.Path]::IsPathRooted($cacheRoot)) {
    $cacheRoot = Join-Path $RepositoryRoot $cacheRoot
  }
  $objectPath = Join-Path (Join-Path (Join-Path $cacheRoot $oid.Substring(0, 2)) $oid.Substring(2, 2)) $oid
  if (-not (Test-Path -LiteralPath $objectPath -PathType Leaf)) {
    throw "Git LFS object $oid is not in the local cache. Fetch release-assets with 'git lfs fetch' first."
  }
  $object = Get-Item -LiteralPath $objectPath
  if ($object.Length -ne $objectSize) { throw "Git LFS object size mismatch: $oid" }
  $actualOid = (Get-FileHash -LiteralPath $objectPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualOid -ne $oid) { throw "Git LFS object hash mismatch: $oid" }
  return $objectPath
}

function Add-Tree([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "Missing package input directory: $Source"
  }
  $sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\', '/')
  foreach ($item in Get-ChildItem -LiteralPath $Source -Force -Recurse) {
    $relative = $item.FullName.Substring($sourceRoot.Length).TrimStart('\', '/')
    $target = Join-Path $Destination $relative
    if ($item.PSIsContainer) {
      if (Test-Path -LiteralPath $target -PathType Leaf) { throw "File/directory collision: $target" }
      New-Item -ItemType Directory -Path $target -Force | Out-Null
      continue
    }

    $resolvedSource = Resolve-LfsFile $item.FullName
    $parent = Split-Path -Parent $target
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    if (Test-Path -LiteralPath $target) { throw "Package file collision: $target" }
    try {
      New-Item -ItemType HardLink -Path $target -Target $resolvedSource -ErrorAction Stop | Out-Null
    }
    catch {
      throw "Could not hard-link '$target' from '$resolvedSource': $($_.Exception.Message) Place the stage directory on the same volume as the checkout and LFS cache."
    }
  }
}

function Get-PeMachine([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  $reader = New-Object System.IO.BinaryReader($stream)
  try {
    if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE file: $Path" }
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    if ($peOffset -lt 64 -or $peOffset -gt ($stream.Length - 6)) { throw "Invalid PE header: $Path" }
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
    return [int]$reader.ReadUInt16()
  }
  finally {
    $reader.Dispose()
    $stream.Dispose()
  }
}

function Test-PathWithin([string]$Path, [string]$Root) {
  $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
  return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $fullPath -ieq $fullRoot.TrimEnd('\', '/')
}

function Assert-Package([string]$Stage) {
  $files = @(Get-ChildItem -LiteralPath $Stage -File -Force -Recurse)
  if (-not $files) { throw 'The staged package is empty.' }
  foreach ($file in $files) {
    $relative = $file.FullName.Substring($Stage.TrimEnd('\', '/').Length).TrimStart('\', '/')
    if ($relative -match '(?i)battleye|steam_api(?:64)?\.dll|steamclient(?:64)?\.dll|steam_appid\.txt') {
      throw "Prohibited Steam/BattlEye package content: $relative"
    }
    if ($file.Name -match '(?i)^d3d(?:x)?1[01](?:_[0-9]+)?\.dll$|^dxgi\.dll$') {
      throw "Unsupported graphics runtime content: $relative"
    }
    if ($file.Extension -ieq '.shdc') {
      throw "Generated shader cache must not be packaged: $relative"
    }
    if ($file.Extension -ieq '.pbo' -and (Get-FirstBytesText $file.FullName) -match '^version https://git-lfs\.github\.com/spec/v1') {
      throw "PBO is still a Git LFS pointer: $relative"
    }
  }

  foreach ($directory in @('Addons', 'dta', 'Keys')) {
    $path = Join-Path $Stage $directory
    if (-not (Test-Path -LiteralPath $path -PathType Container) -or
        -not (Get-ChildItem -LiteralPath $path -File -Recurse | Select-Object -First 1)) {
      throw "Required $Role package directory is missing or empty: $directory"
    }
  }
  if (Test-Path -LiteralPath (Join-Path $Stage $manifestName)) {
    throw "Runtime assets may not contain the reserved $manifestName filename."
  }

  $bikey = Join-Path $Stage 'Keys\dayz.bikey'
  if (-not (Test-Path -LiteralPath $bikey -PathType Leaf) -or
      (Get-FileHash -LiteralPath $bikey -Algorithm SHA256).Hash -ne $expectedBikeyHash) {
    throw 'Keys/dayz.bikey is not the expected public DayZ key.'
  }

  $pbos = @($files | Where-Object { $_.Extension -ieq '.pbo' })
  if (-not $pbos) { throw 'The package contains no PBO files.' }
  foreach ($pbo in $pbos) {
    if (-not (Test-Path -LiteralPath ($pbo.FullName + '.dayz.bisign') -PathType Leaf)) {
      throw "PBO signature is missing: $($pbo.FullName)"
    }
  }
  foreach ($signature in @($files | Where-Object { $_.Name -match '(?i)\.pbo\.dayz\.bisign$' })) {
    $pboPath = $signature.FullName -replace '(?i)\.dayz\.bisign$', ''
    if (-not (Test-Path -LiteralPath $pboPath -PathType Leaf)) {
      throw "PBO signature has no matching PBO: $($signature.FullName)"
    }
  }

  $executable = Join-Path $Stage $expectedExecutable
  if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing $expectedExecutable." }
  if ($Role -eq 'client') {
    if (Test-Path -LiteralPath (Join-Path $Stage 'dll')) {
      throw 'The client package must not contain a dll directory.'
    }
    $shaderRoot = Join-Path $Stage 'bin\ShaderSources_4_0'
    foreach ($shader in @('VS.hlsl', 'PS.hlsl', 'FPShaders.hlsl')) {
      $path = Join-Path $shaderRoot $shader
      if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Missing or empty Direct3D 11 shader source: $shader"
      }
    }
  }
  if ($Role -eq 'server') {
    $mission = Join-Path $Stage 'MPMissions\dayz_Auto.ChernarusPlus'
    if (-not (Test-Path -LiteralPath $mission -PathType Container) -or
        -not (Get-ChildItem -LiteralPath $mission -File -Recurse | Select-Object -First 1)) {
      throw 'Server mission MPMissions/dayz_Auto.ChernarusPlus is missing or empty.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Stage 'serverDZ.cfg') -PathType Leaf)) {
      throw 'Server root serverDZ.cfg is missing.'
    }
    foreach ($dataFile in @('spawnpoints_players.bin', 'proxyexport.bin')) {
      $path = Join-Path $Stage $dataFile
      if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Missing or empty ChernarusPlus data: $dataFile"
      }
    }
  }

  foreach ($file in $files | Where-Object { $_.Extension -match '(?i)^\.(exe|dll)$' }) {
    $machine = Get-PeMachine $file.FullName
    if ($machine -ne $expectedMachine) {
      throw "Wrong PE architecture in $($file.FullName): machine 0x$('{0:X4}' -f $machine), expected 0x$('{0:X4}' -f $expectedMachine)."
    }
  }
}

function Write-ContentManifest([string]$Stage, [string]$Path) {
  $lineByPath = @{}
  foreach ($file in Get-ChildItem -LiteralPath $Stage -File -Force -Recurse) {
    $relative = $file.FullName.Substring($Stage.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
    if ($relative -ceq $manifestName) { continue }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $lineByPath[$relative] = "$hash`t$($file.Length)`t$relative"
  }
  $paths = [string[]]@($lineByPath.Keys)
  [Array]::Sort($paths, [System.StringComparer]::Ordinal)
  $lines = @($paths | ForEach-Object { $lineByPath[$_] })
  $text = if ($lines.Count) { ($lines -join "`n") + "`n" } else { '' }
  $utf8 = [System.Text.UTF8Encoding]::new($false)
  [System.IO.File]::WriteAllText($Path, $text, $utf8)
  return $text
}

function Test-Archive([string]$Path, [string]$Stage, [string]$SevenZip) {
  $listOutput = & $SevenZip l -slt "$Path.001"
  if ($LASTEXITCODE -ne 0) { throw "7-Zip could not list archive $Path." }
  $actual = @()
  $record = @{}
  foreach ($line in $listOutput) {
    if ([string]::IsNullOrWhiteSpace($line) -or $line -match '^-{5,}\s*$') {
      if ($record.ContainsKey('Path') -and $record.ContainsKey('Size') -and $record['Folder'] -ne '+') {
        $entryPath = ([string]$record['Path']).Replace('\', '/')
        if ($entryPath -ine [System.IO.Path]::GetFileName($Path) -and
            $entryPath -ine ([System.IO.Path]::GetFileName($Path) + '.001') -and
            $entryPath -ine ([System.IO.Path]::GetFullPath("$Path.001").Replace('\', '/'))) {
          while ($entryPath.StartsWith('./')) { $entryPath = $entryPath.Substring(2) }
          $actual += "$entryPath`t$([long]$record['Size'])"
        }
      }
      $record = @{}
      continue
    }
    if ($line -match '^([^=]+?)\s*=\s*(.*)$') { $record[$matches[1].Trim()] = $matches[2] }
  }
  if ($record.ContainsKey('Path') -and $record.ContainsKey('Size') -and $record['Folder'] -ne '+') {
    $entryPath = ([string]$record['Path']).Replace('\', '/')
    if ($entryPath -ine [System.IO.Path]::GetFileName($Path) -and
        $entryPath -ine ([System.IO.Path]::GetFileName($Path) + '.001') -and
        $entryPath -ine ([System.IO.Path]::GetFullPath("$Path.001").Replace('\', '/'))) {
      while ($entryPath.StartsWith('./')) { $entryPath = $entryPath.Substring(2) }
      $actual += "$entryPath`t$([long]$record['Size'])"
    }
  }

  $expected = @()
  foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $Stage $manifestName), [System.Text.Encoding]::UTF8)) {
    $parts = $line -split "`t", 3
    if ($parts.Count -ne 3) { throw 'Malformed content manifest.' }
    $expected += "$($parts[2])`t$([long]$parts[1])"
  }
  $manifestFile = Get-Item -LiteralPath (Join-Path $Stage $manifestName)
  $expected += "$manifestName`t$($manifestFile.Length)"
  $actualSorted = [string[]]$actual
  $expectedSorted = [string[]]$expected
  [Array]::Sort($actualSorted, [System.StringComparer]::Ordinal)
  [Array]::Sort($expectedSorted, [System.StringComparer]::Ordinal)
  if (Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actualSorted -CaseSensitive) {
    throw '7-Zip archive listing does not match the staged content manifest.'
  }
}

try {
  $repoRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
  $roleAssets = Join-Path $repoRoot "release-assets\$Role"
  $binaryRoot = (Resolve-Path -LiteralPath $BinaryDirectory).Path
  $stageFullPath = Get-FullPath $StageDirectory
  $StageDirectory = $stageFullPath
  if (-not (Test-Path -LiteralPath $roleAssets -PathType Container)) { throw "Missing role-specific assets: $roleAssets" }
  foreach ($directory in @('Addons', 'dta', 'Keys')) {
    if (-not (Test-Path -LiteralPath (Join-Path $roleAssets $directory) -PathType Container)) {
      throw "Missing role-specific runtime directory: $Role/$directory"
    }
  }
  if ($Role -eq 'server' -and
      (-not (Test-Path -LiteralPath (Join-Path $roleAssets 'MPMissions\dayz_Auto.ChernarusPlus') -PathType Container) -or
       -not (Test-Path -LiteralPath (Join-Path $roleAssets 'serverDZ.cfg') -PathType Leaf))) {
    throw 'Server assets must include MPMissions/dayz_Auto.ChernarusPlus/, and root serverDZ.cfg.'
  }
  $binaryItems = @(Get-ChildItem -LiteralPath $binaryRoot -Force -Recurse)
  if (@($binaryItems | Where-Object { $_.PSIsContainer -or $_.Extension -notmatch '(?i)^\.(exe|dll)$' }).Count -gt 0) {
    throw 'Candidate binary input may contain only root-level executables and runtime DLLs.'
  }
  if (@($binaryItems | Where-Object { $_.Extension -ieq '.exe' -and $_.Name -cne $expectedExecutable }).Count -gt 0) {
    throw "Candidate binary input contains an unexpected executable for $Role."
  }
  if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot $expectedExecutable) -PathType Leaf)) {
    throw "Candidate binaries do not contain $expectedExecutable."
  }
  foreach ($inputRoot in @($roleAssets, $binaryRoot)) {
    if ((Test-PathWithin $stageFullPath $inputRoot) -or (Test-PathWithin $inputRoot $stageFullPath)) {
      throw 'Stage directory must not overlap role assets or candidate binaries.'
    }
  }
  $manifestFullPath = Get-FullPath $ManifestPath
  if (Test-Path -LiteralPath $manifestFullPath) { throw "Manifest output already exists: $manifestFullPath" }
  foreach ($inputRoot in @($roleAssets, $binaryRoot, $stageFullPath)) {
    if (Test-PathWithin $manifestFullPath $inputRoot) { throw 'Manifest output must be separate from all package inputs and staging.' }
  }
  if (Test-Path -LiteralPath $StageDirectory) {
    if (Get-ChildItem -LiteralPath $StageDirectory -Force | Select-Object -First 1) {
      throw "Stage directory must be new or empty: $StageDirectory"
    }
  }
  else {
    New-Item -ItemType Directory -Path $StageDirectory -Force | Out-Null
  }
  Add-Tree $roleAssets $StageDirectory
  Add-Tree $binaryRoot $StageDirectory
  if ($Role -eq 'client') {
    Add-Tree (Join-Path $repoRoot 'lib\d3d11\ShaderSources_4_0') (Join-Path $StageDirectory 'bin\ShaderSources_4_0')
  }
  Assert-Package $StageDirectory

  $manifestStagePath = Join-Path $StageDirectory $manifestName
  $manifestText = Write-ContentManifest $StageDirectory $manifestStagePath
  $manifestParent = Split-Path -Parent $manifestFullPath
  if ($manifestParent) { New-Item -ItemType Directory -Path $manifestParent -Force | Out-Null }
  [System.IO.File]::WriteAllText($manifestFullPath, $manifestText, [System.Text.UTF8Encoding]::new($false))
  $manifestHash = (Get-FileHash -LiteralPath $manifestStagePath -Algorithm SHA256).Hash.ToLowerInvariant()

  if ($ArchivePath) {
    $ArchivePath = Get-FullPath $ArchivePath
    if (-not $ChecksumPath) { $ChecksumPath = "$ArchivePath.parts.sha256" }
    $ChecksumPath = Get-FullPath $ChecksumPath
    if ((Test-PathWithin $ArchivePath $stageFullPath) -or
        (Test-PathWithin $ArchivePath $roleAssets) -or
        (Test-PathWithin $ArchivePath $binaryRoot)) {
      throw 'Archive output must be separate from package inputs and staging.'
    }
    $archiveLeaf = [System.IO.Path]::GetFileName($ArchivePath)
    $checksumLeaf = [System.IO.Path]::GetFileName($ChecksumPath)
    if ($checksumLeaf -ieq $archiveLeaf -or $checksumLeaf -match ('^' + [regex]::Escape($archiveLeaf) + '\.\d{3}$')) {
      throw 'Checksum output cannot overwrite the archive or one of its split-volume files.'
    }
    if ((Test-Path -LiteralPath $ArchivePath) -or (Test-Path -LiteralPath "$ArchivePath.001")) {
      throw "Archive output already exists: $ArchivePath"
    }
    $archiveParent = Split-Path -Parent $ArchivePath
    if ($archiveParent -and (Test-Path -LiteralPath $archiveParent -PathType Container)) {
      $archiveLeaf = Split-Path -Leaf $ArchivePath
      if (Get-ChildItem -LiteralPath $archiveParent -File -Filter "$archiveLeaf.*" |
          Where-Object { $_.Name -match '\.\d{3}$' } | Select-Object -First 1) {
        throw "Split archive parts already exist: $ArchivePath"
      }
    }
    if (Test-Path -LiteralPath $ChecksumPath) { throw "Checksum output already exists: $ChecksumPath" }
    foreach ($inputRoot in @($roleAssets, $binaryRoot, $stageFullPath)) {
      if (Test-PathWithin $ChecksumPath $inputRoot) { throw 'Checksum output must be separate from package inputs and staging.' }
    }
    if (-not $SevenZipPath) {
      $sevenZipCommand = Get-Command '7z.exe' -ErrorAction Stop
      $SevenZipPath = $sevenZipCommand.Source
    }
    if (-not (Test-Path -LiteralPath $SevenZipPath -PathType Leaf)) { throw "7-Zip executable not found: $SevenZipPath" }
    if ($archiveParent) { New-Item -ItemType Directory -Path $archiveParent -Force | Out-Null }
    Push-Location $StageDirectory
    try {
      & $SevenZipPath a -tzip -mx=1 -mmt=on "-v${VolumeSizeBytes}b" $ArchivePath '.\*' | Out-Host
      if ($LASTEXITCODE -ne 0) { throw "7-Zip archive creation failed: $LASTEXITCODE" }
    }
    finally {
      Pop-Location
    }
    & $SevenZipPath t "$ArchivePath.001" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw '7-Zip archive integrity test failed.' }
    Test-Archive $ArchivePath $StageDirectory $SevenZipPath
    $parts = @(Get-ChildItem -LiteralPath $archiveParent -File -Filter ((Split-Path -Leaf $ArchivePath) + '.*') |
      Where-Object { $_.Name -match '\.\d{3}$' } | Sort-Object Name)
    if (-not $parts -or @($parts | Where-Object { $_.Length -ge 2147483648 -or $_.Length -gt $VolumeSizeBytes }).Count -gt 0) {
      throw 'Split ZIP parts are missing or exceed the 1900 MiB / 2 GiB limit.'
    }
    $checksumLines = @()
    foreach ($part in $parts) {
      $partHash = (Get-FileHash -LiteralPath $part.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
      $checksumLines += "$partHash  $($part.Name)"
    }
    $checksumParent = Split-Path -Parent $ChecksumPath
    if ($checksumParent) { New-Item -ItemType Directory -Path $checksumParent -Force | Out-Null }
    [System.IO.File]::WriteAllText($ChecksumPath, ($checksumLines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
  }

  Write-Output $manifestHash
}
catch {
  throw
}
