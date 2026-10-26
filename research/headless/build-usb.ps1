# GPL-2.0-only. Headless wrapper/child package for a USB stick (-DGU_USB_MEDIA).
# Same exact-profile child and hash contract as build-unlock.ps1, but the wrapper and
# child accept only the USB device they were loaded from instead of a pinned internal
# ESP GUID. Builds files only: no staging, boot entries or firmware settings change.
param([Parameter(Mandatory)][guid]$WindowsEspGuid,[string]$Clang='clang',[string]$LldLink='lld-link',[string]$Python='python',
 [ValidateSet('b650m-3090fe','b660m-3070ti','x870-3090ti','msi-z390-3060')][string]$HardwareProfile='b650m-3090fe')
$profiles=@{
 'b650m-3090fe'=@{Defines=@('-DGU_PROFILE_B650M_3090FE');Name='ASUS-TUF-B650M-PLUS-WIFI-3057-RTX3090-FE'}
 'b660m-3070ti'=@{Defines=@('-DGU_PROFILE_B660M_3070TI');Name='ASUS-TUF-GAMING-B660M-E-D4-4003-RTX3070TI'}
 'x870-3090ti'=@{Defines=@('-DGU_PROFILE_X870_3090TI');Name='GIGABYTE-X870-AORUS-ELITE-WIFI7-F13d-RTX3090TI'}
 'msi-z390-3060'=@{Defines=@('-DGU_PROFILE_MSI_Z390_3060');Name='MSI-MPG-Z390-GAMING-PLUS-MS-7B51-1.70-RTX3060-GA106'}
}
$hw=$profiles[$HardwareProfile]
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$boot=Join-Path $repo 'internal-boot/boot'
$unlock=Join-Path $repo 'internal-boot/unlock'
function GuidC([guid]$Value){
 $b=$Value.ToByteArray();$tail=($b[8..15] | ForEach-Object {'0x{0:x2}' -f $_}) -join ','
 return ('{{0x{0:x8},0x{1:x4},0x{2:x4},{{{3}}}}}' -f [BitConverter]::ToUInt32($b,0),[BitConverter]::ToUInt16($b,4),[BitConverter]::ToUInt16($b,6),$tail)
}
$windows=@(Get-Partition | Where-Object {$_.Guid -and [guid]$_.Guid -eq $WindowsEspGuid})
if($windows.Count -ne 1 -or !$windows[0].IsSystem){throw 'WindowsEspGuid must identify the current system EFI partition'}
$config=Join-Path $boot 'platform-config.h';$marker='/* Local USB configuration (build-usb.ps1). Internal partition unused under GU_USB_MEDIA. */'
if((Test-Path -LiteralPath $config) -and (Get-Content -LiteralPath $config -TotalCount 1) -ne $marker){throw 'platform-config.h belongs to an internal Configure.ps1 setup; move it aside before a USB build'}
@($marker,('#define GU_INTERNAL_PARTITION_INITIALIZER '+(GuidC ([guid]::Empty))),('#define GU_WINDOWS_PARTITION_INITIALIZER '+(GuidC $WindowsEspGuid))) | Set-Content -LiteralPath $config -Encoding ascii
& (Join-Path $unlock 'build.ps1') -ExperimentalUnlock -StockProfile entryLatchedSs1Full -InternalBoot -AutomaticBoot -HeadlessResearch -UsbMedia -HardwareProfile $HardwareProfile -Clang $Clang -LldLink $LldLink -Python $Python
if(!$?){throw 'Headless USB child build failed'}
$childDir=Join-Path $unlock 'build/experimental/entryLatchedSs1Full/internal/automatic/headless-research/usb'
$child=Join-Path $childDir 'RTX3090-EXPERIMENTAL-entryLatchedSs1Full-INTERNAL-AUTO-HEADLESS-RESEARCH-USB.EFI'
$cm=Get-Content (Join-Path $childDir 'build-manifest.json') -Raw | ConvertFrom-Json
$hash=(Get-FileHash -LiteralPath $child).Hash.ToLowerInvariant()
if($cm.Mode -ne 'headless-research-automatic-internal-exact-card-unlock' -or $cm.EfiSha256 -ne $hash -or $cm.StockProfile -ne 'entryLatchedSs1Full' -or !$cm.CanArm -or
   $cm.HardwareProfile -ne $hw.Name -or $cm.Defines -notcontains '-DGU_USB_MEDIA'){throw 'Headless USB child contract mismatch'}
Push-Location -LiteralPath $boot
try {
 [void][IO.Directory]::CreateDirectory((Join-Path $PWD 'build/headless'))
 @('/* Local headless child contract; GPL-2.0-only. */',('#define GU_UNLOCK_SHA256 "'+$hash+'"'),('#define GU_UNLOCK_BYTES '+(Get-Item $child).Length)) | Set-Content build/headless/child-contract.h -Encoding ascii
 $sources=@('handoff.c','full-release.c','full-flow.c','diagnostic.c','../unlock/checks.c','../unlock/sha256.c','../efi_runtime.c')
 $objects=@()
 foreach($source in $sources) {
  $obj='build/headless/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -DGU_HEADLESS_RESEARCH -DGU_SINGLE_RTX_CONSOLE -DGU_FULL_UNLOCK -DGU_AUTOMATIC_BOOT -DGU_USB_MEDIA @($hw.Defines) -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 -c $source -o $obj
  if($LASTEXITCODE -ne 0){throw "Headless USB wrapper compile failed: $source"};$objects+=$obj
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/headless/HEADLESS.map /out:build/headless/HEADLESS.EFI @objects
 if($LASTEXITCODE -ne 0){throw 'Headless USB wrapper link failed'}
 & $Python ../unlock/verify_pe.py build/headless/HEADLESS.EFI build/headless/HEADLESS.map --output build/headless/pe-verification.json
 if($LASTEXITCODE -ne 0){throw 'Headless USB wrapper PE verification failed'}
 [ordered]@{Format=2;HardwareProfile=$hw.Name;Mode='single-rtx-headless-unlock-then-windows-usb';CanUnlock=$true;RequiresHumanKey=$false;HardwareTested=$false;
  ChildSha256=$hash;ChildBytes=(Get-Item $child).Length;EfiSha256=(Get-FileHash -LiteralPath build/headless/HEADLESS.EFI).Hash.ToLowerInvariant();EfiBytes=(Get-Item build/headless/HEADLESS.EFI).Length;
  BuiltUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content build/headless/usb-manifest.json -Encoding utf8
 Get-Content build/headless/usb-manifest.json
} finally {Pop-Location}
