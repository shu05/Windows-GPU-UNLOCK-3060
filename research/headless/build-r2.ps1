# GPL-2.0-only. Separate local R2 experiment; no installation or firmware writes.
param([string]$Clang='clang',[string]$LldLink='lld-link',[string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat',[string]$Capture='U:/RTX3090-UNLOCK/DIAG02.TXT')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$boot=Join-Path $repo 'internal-boot/boot'
Push-Location -LiteralPath $boot
try {
 & python (Join-Path $PSScriptRoot 'analyze_capture.py') --capture $Capture
 if($LASTEXITCODE -ne 0){throw 'Capture analysis failed'}
 # The offline result gates fixture choice, never hardware mutation authority.
 $analysis=(& python (Join-Path $PSScriptRoot 'analyze_capture.py') --capture $Capture) | ConvertFrom-Json
 if(!$analysis.single_rtx_display_topology_observed){throw 'Need one observed physical RTX GOP'}
 if(!(Test-Path -LiteralPath 'platform-config.h')){throw 'Configure exact local partitions first'}
 [void][IO.Directory]::CreateDirectory((Join-Path $boot 'build/r2'))
 & python make-console-fixture.py $Capture --single-rtx
 if($LASTEXITCODE -ne 0){throw 'Single RTX fixture failed'}
 $lines=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
 if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
 foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
 $defines=@('-DGU_SINGLE_RTX_CONSOLE','-DGU_DIAGNOSTIC_ONLY','-DGU_CONSOLE_PREPARE')
 $includes=@('-I','../../deps/edk2/MdePkg/Include','-I','../../deps/edk2/MdePkg/Include/X64')
 & $Clang -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS @defines @includes test_r2.c research-input.c console-flow.c -o build/r2/test-r2.exe
 if($LASTEXITCODE -ne 0){throw 'R2 host tests compile failed'}
 & ./build/r2/test-r2.exe
 if($LASTEXITCODE -ne 0){throw 'R2 host tests failed'}
 $sources=@('handoff.c','prepare-console.c','console-flow.c','research-input.c','diagnostic.c','../efi_runtime.c')
 $objects=@()
 foreach($source in $sources){
  $obj='build/r2/'+[IO.Path]::GetFileNameWithoutExtension($source)+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror @defines @includes -c $source -o $obj
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"};$objects+=$obj
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/r2/R2-CONSOLE.map /out:build/r2/R2-CONSOLE.EFI @objects
 if($LASTEXITCODE -ne 0){throw 'R2 EFI link failed'}
 & python ../unlock/verify_pe.py build/r2/R2-CONSOLE.EFI build/r2/R2-CONSOLE.map --output build/r2/pe-verification.json
 if($LASTEXITCODE -ne 0){throw 'R2 PE verification failed'}
 [ordered]@{Mode='single-rtx-console-consumers-only';RequiresHumanKey=$false;CanUnlock=$false;CanDetach=$true;CanDetachNvOwner=$false;CanDetachRuntimeEdid=$false;CanRemoveGop=$false;HardwareTested=$false;EfiSha256=(Get-FileHash -LiteralPath build/r2/R2-CONSOLE.EFI).Hash;BuiltUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath build/r2/manifest.json -Encoding utf8
} finally {Pop-Location}
