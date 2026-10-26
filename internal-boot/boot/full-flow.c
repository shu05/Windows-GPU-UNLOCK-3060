/* GPL-2.0-only. No retry, fallback or Windows after a release/unlock failure. */
#include "full-flow.h"
EFI_STATUS GuFullFlow(const GU_FULL_FLOW_OPS *o) {
  if(!o || !o->Prepare || !o->Choose || !o->Capture || !o->Release || !o->VerifyCold || !o->Unlock || !o->WindowsSkip || !o->WindowsAfter || !o->Record)return EFI_INVALID_PARAMETER;
  EFI_STATUS s=o->Prepare(o->Context);if(s!=EFI_SUCCESS)return s;
  BOOLEAN skip=FALSE;s=o->Choose(o->Context,&skip);if(s!=EFI_SUCCESS)return s;
  if(skip) {
    s=o->Record(o->Context,"full-unlock-skipped-no-driver-release",EFI_SUCCESS);
    if(s==EFI_SUCCESS)s=o->WindowsSkip(o->Context);
    return s==EFI_SUCCESS?EFI_ABORTED:s;
  }
  s=o->Capture(o->Context);if(s!=EFI_SUCCESS)return s;
  EFI_STATUS released=o->Release(o->Context);
  EFI_STATUS captured=o->Capture(o->Context);
  s=o->Record(o->Context,"full-preparation-result",released);
  if(released!=EFI_SUCCESS)return released;
  if(captured!=EFI_SUCCESS)return captured;
  if(s!=EFI_SUCCESS)return s;
#ifdef GU_AUTOMATIC_BOOT
  s=o->Record(o->Context,"automatic-unlock-start-Esc-cancels",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
#else
  s=o->Record(o->Context,"original-unlock-start-manual-U-required",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
#endif
  s=o->VerifyCold(o->Context);if(s!=EFI_SUCCESS)return s;
  s=o->Unlock(o->Context);
  EFI_STATUS recorded=o->Record(o->Context,"original-unlock-return",s);
  if(s!=EFI_SUCCESS)return s;
  if(recorded!=EFI_SUCCESS)return recorded;
  s=o->Record(o->Context,"full-windows-start",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  s=o->WindowsAfter(o->Context);
  return s==EFI_SUCCESS?EFI_ABORTED:s;
}
