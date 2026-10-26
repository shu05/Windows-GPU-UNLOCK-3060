/* Windows GPU UNLOCK internal boot experiment, 2026-10-20. GPL-2.0-only. */
#include "flow.h"
EFI_STATUS GuHandoffFlow(const GU_HANDOFF_OPS *o) {
  BOOLEAN skip=FALSE;EFI_STATUS s;
  s=o->Prepare(o->Context);if(s!=EFI_SUCCESS)return s;
  s=o->Record(o->Context,"windows-preloaded",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  s=o->Select(o->Context,&skip);if(s!=EFI_SUCCESS)return s;
  s=o->Record(o->Context,skip?"skip-unlock":"unlock-start",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  if(!skip) {
    s=o->Unlock(o->Context);
    EFI_STATUS logged=o->Record(o->Context,"unlock-return",s);
    if(s!=EFI_SUCCESS)return s;
    if(logged!=EFI_SUCCESS)return logged;
  }
  s=o->Record(o->Context,"windows-start",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  /* Successful Windows transfer never returns. Every return must stop. */
  s=o->Windows(o->Context);
  (void)o->Record(o->Context,"windows-return-unexpected",s);
  return s==EFI_SUCCESS?EFI_ABORTED:s;
}
