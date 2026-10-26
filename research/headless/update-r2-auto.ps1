# GPL-2.0-only. Upgrade only the attributed R2 entry; keep manual EFI and order.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'internal-boot/install/automatic-policy.ps1')
$stateFile=Join-Path $PSScriptRoot 'r2-registration.local.json'
$state=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
$config=Get-Content -LiteralPath (Join-Path $repo 'internal-boot/local-config.json') -Raw | ConvertFrom-Json
$oldHash='a4363159b2ed4088ed84f96cd0a0e0d424843d4dd992c50c58cf732fd7854aad'
$newHash='dada135fbaf8fa19ac1e627668e63387ce5387bf63054d3e3adfa7ca76824c67'
$oldPath='\EFI\WindowsGPUUnlock\R2-CONSOLE.EFI';$newPath='\EFI\WindowsGPUUnlock\R2-AUTO.EFI'
$oldName='GPU R2 Console Test - NO UNLOCK';$newName='GPU R2 Auto Test - NO UNLOCK'
function Bcd([string[]]$a){$v=@(& bcdedit @a);if($LASTEXITCODE -ne 0){throw 'BCDEdit failed'};return ($v -join "`r`n")}
function Target {
 $p=Get-Partition -DriveLetter $config.UnlockDriveLetter;$d=Get-Disk -Number $p.DiskNumber;$v=$p | Get-Volume
 $w=@(Get-Partition | Where-Object {$_.Guid -eq $config.WindowsPartitionGuid})
 if($w.Count -ne 1 -or !$w[0].IsSystem -or $p.IsSystem -or $p.Guid -ne $config.UnlockPartitionGuid -or $p.Guid -eq $config.WindowsPartitionGuid -or $p.Offset -ne $config.UnlockOffset -or $p.Size -ne $config.UnlockSize -or $d.UniqueId -ne $config.UnlockDiskUniqueId -or $p.GptType -ne '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' -or $v.FileSystem -ne 'FAT32' -or !$v.Path){throw 'ESP identity changed'}
 return $v.Path
}
if($state.EntryId -notmatch '^\{[0-9a-f-]{36}\}$' -or $state.Status -ne 'registered' -or $state.Name -ne $oldName -or $state.Path -ne $oldPath -or $state.EfiSha256 -ne $oldHash -or $state.CanUnlock -ne $false){throw 'Expected previously registered manual R2 state'}
if(Confirm-SecureBootUEFI){throw 'Secure Boot must remain disabled'}
$enc=Get-BitLockerVolume -MountPoint C:
if([int]$enc.VolumeStatus -ne 0 -or [int]$enc.ProtectionStatus -ne 0){throw 'Encryption state changed'}
$root=Target;$oldFile=Join-Path $root $oldPath.TrimStart('\');$newFile=Join-Path $root $newPath.TrimStart('\')
$source=Join-Path $repo 'internal-boot/boot/build/r2/R2-CONSOLE.EFI'
$manifest=Get-Content (Join-Path $repo 'internal-boot/boot/build/r2/manifest.json') -Raw | ConvertFrom-Json
if($manifest.Mode -ne 'single-rtx-console-consumers-only' -or $manifest.RequiresHumanKey -ne $false -or $manifest.CanUnlock -ne $false -or $manifest.CanDetachNvOwner -ne $false -or $manifest.CanDetachRuntimeEdid -ne $false -or $manifest.CanRemoveGop -ne $false -or $manifest.EfiSha256 -ne $newHash -or (Get-FileHash -LiteralPath $source).Hash -ne $newHash -or (Get-FileHash -LiteralPath $oldFile).Hash -ne $oldHash){throw 'R2 build/manual backup contract mismatch'}
$fw=Bcd @('/enum','{fwbootmgr}');$all=Bcd @('/enum','firmware');$entry=Bcd @('/enum',$state.EntryId)
if($fw -match '(?mi)^bootsequence\s+' -or $entry -notmatch ('(?mi)^path\s+'+[regex]::Escape($oldPath)+'\s*$') -or $entry -notmatch ('(?mi)^description\s+'+[regex]::Escape($oldName)+'\s*$') -or $entry -notmatch ('(?mi)^device\s+partition='+[regex]::Escape($config.UnlockDriveLetter)+':\s*$') -or $fw -notmatch [regex]::Escape($state.EntryId)){throw 'R2 entry changed or BootNext pending'}
$others=@{}
foreach($m in [regex]::Matches($all,'(?mi)^identifier\s+(\{(?:bootmgr|[0-9a-f-]{36})\})')){if($m.Groups[1].Value -ne $state.EntryId){$others[$m.Groups[1].Value]=Bcd @('/enum',$m.Groups[1].Value)}}
$backup=Join-Path $env:ProgramData ('WindowsGPUUnlock\ResearchBackups\R2AUTO-'+(Get-Date -Format 'yyyyMMdd-HHmmssfff'))
[void][IO.Directory]::CreateDirectory($backup)
[void](Bcd @('/export',(Join-Path $backup 'BCD-export')))
[IO.File]::Copy($stateFile,(Join-Path $backup 'manual-registration.json'),$false)
[IO.File]::Copy($oldFile,(Join-Path $backup 'R2-CONSOLE.EFI'),$false)
[IO.File]::WriteAllText((Join-Path $backup 'firmware-before.txt'),$all)
if(Test-Path -LiteralPath $newFile){if((Get-FileHash -LiteralPath $newFile).Hash -ne $newHash){throw 'Different R2-AUTO exists; refusing overwrite'}}
else{[IO.File]::Copy($source,$newFile,$false)}
function CheckFiles {
 if((Target) -ne $root -or (Get-FileHash -LiteralPath $newFile).Hash -ne $newHash -or (Get-FileHash -LiteralPath $oldFile).Hash -ne $oldHash){throw 'R2 files/partition changed'}
}
function CheckOthers {
 if((Bcd @('/enum','{fwbootmgr}')) -cne $fw){throw 'Boot order or BootNext changed'}
 foreach($id in $others.Keys){if((Bcd @('/enum',$id)) -cne $others[$id]){throw 'Unrelated firmware entry changed'}}
}
CheckFiles;CheckOthers
if((Bcd @('/enum',$state.EntryId)) -cne $entry){throw 'R2 entry changed before update'}
$expected=[regex]::Replace($entry,'(?mi)^(path\s+)[^\r\n]*','$1'+$newPath)
$expected=[regex]::Replace($expected,'(?mi)^(description\s+)[^\r\n]*','$1'+$newName)
Invoke-AutomaticTransition -Steps @(
 {[void](Bcd @('/set',$state.EntryId,'path',$newPath))},
 {[void](Bcd @('/set',$state.EntryId,'description',$newName))}
) -Verify {
 CheckFiles;CheckOthers
 if((Bcd @('/enum',$state.EntryId)) -cne $expected){throw 'R2 updated entry mismatch'}
 $state.Path=$newPath;$state.Name=$newName;$state.EfiSha256=$newHash;$state.HardwareTested=$false
 $state | Add-Member -NotePropertyName RequiresHumanKey -NotePropertyValue $false -Force
 $state | Add-Member -NotePropertyName UpdatedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
 $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $stateFile -Encoding utf8
 $saved=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
 if($saved.EntryId -ne $state.EntryId -or $saved.Path -ne $newPath -or $saved.EfiSha256 -ne $newHash -or $saved.RequiresHumanKey -ne $false){throw 'Updated state readback failed'}
 [IO.File]::WriteAllText((Join-Path $backup 'firmware-after.txt'),(Bcd @('/enum','firmware')))
} -Rollback {
 Invoke-RecoveryActions -Actions @(
  {[void](Bcd @('/set',$state.EntryId,'path',$oldPath))},
  {[void](Bcd @('/set',$state.EntryId,'description',$oldName))}
 )
 if((Bcd @('/enum',$state.EntryId)) -cne $entry){throw "R2 rollback incomplete; inspect $backup"}
 CheckOthers
 $current=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
 if($current.EntryId -ne $state.EntryId){throw 'State ownership changed; refusing overwrite'}
 [IO.File]::Copy((Join-Path $backup 'manual-registration.json'),$stateFile,$true)
}
Write-Output "Updated R2 entry to $newName. No keys required after F8 selection; default order unchanged. Manual EFI preserved. Backup: $backup"
