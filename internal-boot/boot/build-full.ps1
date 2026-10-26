param([string]$Clang='clang',[string]$LldLink='lld-link',[string]$Python='python')
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 New-Item -ItemType Directory -Path build/full -Force | Out-Null
 $sources=@('handoff.c','full-release.c','full-flow.c','diagnostic.c','../unlock/checks.c','../unlock/sha256.c','../efi_runtime.c')
 $objects=@()
 foreach($source in $sources) {
  $obj='build/full/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -DGU_FULL_UNLOCK -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 -c $source -o $obj
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"};$objects+=$obj
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/full/FULL.map /out:build/full/FULL.EFI @objects
 if($LASTEXITCODE -ne 0){throw 'Full handoff link failed'}
 & $Python ../unlock/verify_pe.py build/full/FULL.EFI build/full/FULL.map --output build/full/pe-verification.json
 if($LASTEXITCODE -ne 0){throw 'Full handoff PE verification failed'}
 $tracked=@($sources+@('prepare-console.c','prepare-console.h','full-release.h','full-flow.h','decode-policy.h','media.h','generated.h'))
 $sourceManifest=@($tracked | ForEach-Object {[ordered]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
 $manual=Get-Content ../unlock/build/experimental/entryLatchedSs1Full/internal/build-manifest.json -Raw | ConvertFrom-Json
 [ordered]@{HardwareTested=$false;Mode='guarded-gop-release-decode-restore-original-unlock';CanUnlock=$true;UnlockIsOriginal=$true;UnlockSha256=$manual.EfiSha256;RestoreMask='missing original IO/MEM only; never BME';Input='original manual U once AFTER driver release';EfiSha256=(Get-FileHash -LiteralPath build/full/FULL.EFI -Algorithm SHA256).Hash;BuiltUtc=[DateTime]::UtcNow.ToString('o');Sources=$sourceManifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath build/full/manifest.json -Encoding utf8
} finally {Pop-Location}
