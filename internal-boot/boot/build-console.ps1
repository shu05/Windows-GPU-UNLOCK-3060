param([string]$Clang='clang',[string]$LldLink='lld-link',[string]$Python='python')
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 New-Item -ItemType Directory -Path build/console -Force | Out-Null
 $sources=@('handoff.c','prepare-console.c','console-flow.c','diagnostic.c','../efi_runtime.c')
 $objects=@()
 foreach($source in $sources) {
  $obj='build/console/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -DGU_DIAGNOSTIC_ONLY -DGU_CONSOLE_PREPARE -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 -c $source -o $obj
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"};$objects+=$obj
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/console/CONSOLE.map /out:build/console/CONSOLE.EFI @objects
 if($LASTEXITCODE -ne 0){throw 'Console prepare link failed'}
 & $Python ../unlock/verify_pe.py build/console/CONSOLE.EFI build/console/CONSOLE.map --output build/console/pe-verification.json
 if($LASTEXITCODE -ne 0){throw 'Console prepare PE verification failed'}
 $sourceManifest=@(($sources+@('prepare-console.h','console-flow.h','media.h')) | ForEach-Object {[ordered]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
 [ordered]@{HardwareTested=$false;Mode='two-rtx-console-consumers-then-windows';CanUnlock=$false;CanDetachNvOwner=$false;CanDetachRuntimeEdid=$false;CanDetachConsoleConsumers=$true;EfiSha256=(Get-FileHash -LiteralPath build/console/CONSOLE.EFI -Algorithm SHA256).Hash;BuiltUtc=[DateTime]::UtcNow.ToString('o');Sources=$sourceManifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath build/console/manifest.json -Encoding utf8
} finally {Pop-Location}
