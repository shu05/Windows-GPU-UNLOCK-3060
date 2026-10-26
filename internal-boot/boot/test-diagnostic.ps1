param([string]$Clang='clang',[string]$VcVars='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 $lines=& cmd.exe /d /c "call `"$VcVars`" >nul && set"
 if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
 foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
 New-Item -ItemType Directory -Path build/diagnostic -Force | Out-Null
 & $Clang -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 test_diagnostic.c -o build/diagnostic/test-diagnostic.exe
 if($LASTEXITCODE -ne 0){throw 'Diagnostic tests compile failed'}
 & ./build/diagnostic/test-diagnostic.exe
 if($LASTEXITCODE -ne 0){throw 'Diagnostic tests failed'}
 # Probe regression tests: the real probe.c behind simulated EFI services for
 # both reviewed profiles. No GPU, firmware or driver access is available.
 foreach($profile in @('GU_PROFILE_B650M_3090FE','GU_PROFILE_B660M_3070TI')) {
  & $Clang -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -DGU_DIAGNOSTIC_PROBE "-D$profile" -I ../../deps/edk2/MdePkg/Include -I ../../deps/edk2/MdePkg/Include/X64 test_probe.c probe.c ../unlock/sha256.c -o "build/diagnostic/test-probe-$profile.exe"
  if($LASTEXITCODE -ne 0){throw "Probe tests compile failed: $profile"}
  & "./build/diagnostic/test-probe-$profile.exe"
  if($LASTEXITCODE -ne 0){throw "Probe tests failed: $profile"}
 }
} finally {Pop-Location}
