# GPL-2.0-only. Host replay; private fixture inputs remain untracked.
param([string]$Clang='clang',[string]$FixtureRoot='C:/AI/Tools/windows-gpu-unlock-internal/boot/build',[string]$Prom='C:/AI/Backups/GPU-UNLOCK-USB-20261020-005607/files/RTX3090-PROBE/RUN00/PROM.BIN')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location (Join-Path $repo 'internal-boot/boot')
try {
 [void][IO.Directory]::CreateDirectory((Join-Path $PWD 'build/headless'))
 & python make-console-fixture.py U:/RTX3090-UNLOCK/DIAG02.TXT --single-rtx
 if($LASTEXITCODE -ne 0){throw 'Headless graph fixture failed'}
 $lines=& cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && set'
 if($LASTEXITCODE -ne 0){throw 'VC environment failed'}
 foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
 & $Clang -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -DGU_HEADLESS_RESEARCH -DGU_SINGLE_RTX_CONSOLE -DGU_FULL_UNLOCK -DGU_AUTOMATIC_BOOT -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 test_headless_full.c console-flow.c ../unlock/checks.c ../unlock/sha256.c -o build/headless/test-full.exe
 if($LASTEXITCODE -ne 0){throw 'Headless full host compile failed'}
 & ./build/headless/test-full.exe (Join-Path $FixtureRoot 'reference/AMI-shared-driver-pe/body.bin') (Join-Path $FixtureRoot 'reference/NV-GOP.efi') $Prom (Join-Path $FixtureRoot 'full/config.bin')
 if($LASTEXITCODE -ne 0){throw 'Headless full host tests failed'}
 & $Clang -std=c11 -O1 -Wall -Wextra -Werror -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 test_full_flow.c full-flow.c -o build/headless/test-flow.exe
 if($LASTEXITCODE -ne 0){throw 'Full flow compile failed'}
 & ./build/headless/test-flow.exe
 if($LASTEXITCODE -ne 0){throw 'Full flow tests failed'}
} finally {Pop-Location}
