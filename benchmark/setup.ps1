$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = $PSScriptRoot
$manifestUrl = 'https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.6.0.json'
$manifest = Invoke-RestMethod $manifestUrl
$downloads = Join-Path $root 'downloads'
$packages = Join-Path $root 'packages'
New-Item -ItemType Directory -Force -Path $downloads,$packages | Out-Null
$records = @()
foreach ($component in @('cuda_nvcc','cuda_cudart','cuda_cccl','libcublas')) {
    $item = $manifest.$component
    $archive = $item.'windows-x86_64'
    $url = 'https://developer.download.nvidia.com/compute/cuda/redist/' + $archive.relative_path
    $zip = Join-Path $downloads ([IO.Path]::GetFileName($archive.relative_path))
    if (-not (Test-Path -LiteralPath $zip)) {
        Write-Output "Downloading $component $($item.version)"
        Invoke-WebRequest -Uri $url -OutFile $zip
    }
    if ((Get-Item -LiteralPath $zip).Length -ne [long]$archive.size -or
        (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archive.sha256) {
        throw "NVIDIA archive verification failed: $component"
    }
    $destination = Join-Path $packages $component
    if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite package directory: $destination" }
    Expand-Archive -LiteralPath $zip -DestinationPath $destination
    $records += [pscustomobject]@{Component=$component;Version=$item.version;Url=$url;SHA256=$archive.sha256;Bytes=[long]$archive.size}
    Write-Output "Verified and extracted $component"
}
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'packages-manifest.json') -Encoding utf8
$sourceUrl = 'https://raw.githubusercontent.com/rough2515-hub/rtx3080-unlock-public/a08254d269af30ace9e57237abd3d8e6c261e8f5/analysis/compute-benchmark.cu'
Invoke-WebRequest -Uri $sourceUrl -OutFile (Join-Path $root 'upstream-compute-benchmark.cu')
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/rough2515-hub/rtx3080-unlock-public/a08254d269af30ace9e57237abd3d8e6c261e8f5/LICENSE' -OutFile (Join-Path $root 'LICENSE')
Write-Output 'Isolated CUDA components prepared; no installer or GPU workload was run.'
