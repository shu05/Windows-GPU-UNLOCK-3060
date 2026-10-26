# GPL-2.0-only. Build an observational EFI only; never install or edit BootOrder.
param([Parameter(Mandatory)][ValidatePattern('^[D-Zd-z]$')][string]$UnlockDriveLetter,[Parameter(Mandatory)][guid]$WindowsEspGuid,[string]$Clang='clang',[string]$LldLink='lld-link')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& (Join-Path $repo 'internal-boot/Configure.ps1') -UnlockDriveLetter $UnlockDriveLetter -WindowsEspGuid $WindowsEspGuid
if(!$?){throw 'Local partition configuration failed'}
$generated=Join-Path $repo 'internal-boot/boot/generated.h'
if(!(Test-Path -LiteralPath $generated)){'/* Observation build: GU_DIAGNOSTIC_ONLY excludes all unlock loading. */' | Set-Content -LiteralPath $generated -Encoding ascii}
& (Join-Path $repo 'internal-boot/boot/build-diagnostic.ps1') -Clang $Clang -LldLink $LldLink
if(!$?){throw 'Observation build failed'}
$built=Join-Path $repo 'internal-boot/boot/build/diagnostic'
$m=Get-Content -LiteralPath (Join-Path $built 'manifest.json') -Raw | ConvertFrom-Json
if($m.CanUnlock -ne $false -or $m.CanDetach -ne $false -or $m.WritesFirmwareVariables -ne $false -or $m.Mode -ne 'graph-capture-then-normal-windows'){throw 'Unexpected observation capabilities'}
$output=Join-Path $PSScriptRoot 'build'
[void][IO.Directory]::CreateDirectory($output)
$source=Join-Path $built 'DIAGNOSTIC.EFI';$target=Join-Path $output 'OBSERVE.EFI'
if((Get-FileHash -LiteralPath $source).Hash -ne $m.EfiSha256){throw 'Diagnostic build hash mismatch'}
if(Test-Path -LiteralPath $target){if((Get-FileHash -LiteralPath $target).Hash -ne $m.EfiSha256){throw 'Different observation EFI exists; preserve it before building a new profile'}}
else{[IO.File]::Copy($source,$target,$false)}
if((Get-FileHash -LiteralPath $target).Hash -ne $m.EfiSha256){throw 'Observation copy hash mismatch'}
[ordered]@{Mode='headless-research-observation-only';CanUnlock=$false;CanDetach=$false;HardwareTested=$false;HeadlessBootVerified=$false;EfiSha256=$m.EfiSha256;BuiltUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $output 'manifest.json') -Encoding utf8
Write-Output "Built $target. No installation, driver release, unlock or boot-order change performed."
