/* GPL-2.0-only. Pure policy: restore ONLY missing original IO/MEM bits. */
#pragma once
#include "../unlock/machine-profile.h"
#include <Uefi.h>
#include <Protocol/PciIo.h>
static inline BOOLEAN GuDecodeRestoreMask(const UINT32 *before,const UINT32 *after,UINT64 *mask) {
  if(!before || !after || !mask)return FALSE;
  *mask=0;
  if(before[0]!=GU_BOARD_ID || before[11]!=GU_BOARD_SUBSYSTEM || !(before[1]&2) || (!GU_MSI_INT10_CONSOLE && (before[1]&4)) || (!GU_MSI_INT10_CONSOLE && (after[1]&4)))return FALSE;
  UINT32 missing=before[1]&~after[1]&3;
  for(UINTN i=0;i<64;i++) {
    UINT32 a=after[i];
#if GU_MSI_INT10_CONSOLE
    /* The CSM firmware entered with bus mastering set; releasing the
     * firmware driver chain may or may not clear it through PciIo
     * attribute revert. Either outcome is tolerated: the unlock
     * transaction owns bus mastering from here and clears it itself.
     * IO and memory decode may only be restored, never left missing. */
    if(i==1 && (before[1]&4))a|=4;
#endif
    if((a|(i==1?missing:0))!=before[i])return FALSE;
  }
  *mask=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  return TRUE;
}
