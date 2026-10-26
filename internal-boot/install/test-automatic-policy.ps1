$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'automatic-policy.ps1')
for($fault=1;$fault -le 5;$fault++){
 $test=[pscustomobject]@{Path='manual';Name='manual';Hiber=1;Order='windows-first';Calls=0;Recovered=0}
 $steps=@(
  {$test.Path='auto';$test.Calls++;if($fault -eq 1){throw 'path failure'}},
  {$test.Name='auto';$test.Calls++;if($fault -eq 2){throw 'name failure'}},
  {$test.Hiber=0;$test.Calls++;if($fault -eq 3){throw 'registry failure'}},
  {$test.Order='auto-first';$test.Calls++;if($fault -eq 4){throw 'order failure'}}
 )
 $caught=$false
 try{Invoke-AutomaticTransition -Steps $steps -Verify {throw 'verification failure'} -Rollback {
  $test.Recovered++
  Invoke-RecoveryActions -Actions @({$test.Order='windows-first'},{$test.Path='manual'},{$test.Name='manual'},{$test.Hiber=1})
 }}catch{$caught=$true}
 if(!$caught -or $test.Path -ne 'manual' -or $test.Name -ne 'manual' -or $test.Hiber -ne 1 -or $test.Order -ne 'windows-first' -or $test.Recovered -ne 1 -or $test.Calls -ne [Math]::Min($fault,4)){throw 'Failed transition did not stop and recover'}
}
$test=[pscustomobject]@{Calls=0;Verified=$false;Rollback=$false}
Invoke-AutomaticTransition -Steps @({$test.Calls++},{$test.Calls++}) -Verify {$test.Verified=$true} -Rollback {$test.Rollback=$true}
if($test.Calls -ne 2 -or !$test.Verified -or $test.Rollback){throw 'Successful transition incorrect'}
$test=[pscustomobject]@{Calls=0};$caught=$false
try{Invoke-RecoveryActions -Actions @({throw 'first recovery fails'},{$test.Calls++},{throw 'third fails'},{$test.Calls++})}catch{$caught=$true}
if(!$caught -or $test.Calls -ne 2){throw 'Recovery must attempt all independent actions'}
Write-Output 'PASS automatic transition: four partial mutation failures + verification failure recover; success path; recovery continues after independent failure. No registry/BCD calls.'
$source="Windows Boot Manager`r`n--------------------`r`nidentifier              {bootmgr}`r`ndevice                  partition=C:`r`npath                    \EFI\Microsoft\Boot\bootmgfw.efi`r`ndescription             Windows Boot Manager`r`n"
$new='{11111111-2222-3333-4444-555555555555}'
$copy=$source.Replace('{bootmgr}',$new).Replace('description             Windows Boot Manager','description             Windows GPU UNLOCK Manual')
if((Resolve-NewBootCopy -BeforeIds @() -AfterEntries @{$new=$copy} -Source $source -Name 'Windows GPU UNLOCK Manual') -ne $new){throw 'Boot copy attribution failed'}
foreach($entries in @(@{},@{$new=$copy;'{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}'=$copy},@{$new=$copy.Replace('partition=C:','partition=D:')},@{$new=$copy.Replace('description             Windows GPU UNLOCK Manual','description             Other')})){
 $caught=$false;try{[void](Resolve-NewBootCopy -BeforeIds @() -AfterEntries $entries -Source $source -Name 'Windows GPU UNLOCK Manual')}catch{$caught=$true}
 if(!$caught){throw 'Ambiguous or changed copy was attributed for deletion'}
}
Write-Output 'PASS boot-copy attribution: exact clone accepted; none, multiple, different device and wrong label refused. No firmware access.'
