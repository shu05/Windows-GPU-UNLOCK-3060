/* GPL-2.0-only. No device APIs: failed detach/capture/log never boots Windows. */
#include "console-flow.h"
EFI_STATUS GuConsoleFlow(const GU_CONSOLE_FLOW_OPS *o) {
  if(!o || !o->Prepare || !o->Choose || !o->Capture || !o->Detach || !o->ReadyWindows || !o->Verify || !o->Windows || !o->Record)return EFI_INVALID_PARAMETER;
  EFI_STATUS s=o->Prepare(o->Context);if(s!=EFI_SUCCESS)return s;
  BOOLEAN skip=FALSE;s=o->Choose(o->Context,&skip);if(s!=EFI_SUCCESS)return s;
  if(skip) {
    s=o->Record(o->Context,"console-prepare-skipped",EFI_SUCCESS);
    if(s!=EFI_SUCCESS)return s;
  } else {
    s=o->Capture(o->Context);if(s!=EFI_SUCCESS)return s;
    EFI_STATUS detached=o->Detach(o->Context);
    /* Preserve evidence of partial failures, without reconnect or retry. */
    EFI_STATUS captured=o->Capture(o->Context);
    s=o->Record(o->Context,"console-prepare-result",detached);
    if(detached!=EFI_SUCCESS)return detached;
    if(captured!=EFI_SUCCESS)return captured;
    if(s!=EFI_SUCCESS)return s;
  }
  s=o->ReadyWindows(o->Context);if(s!=EFI_SUCCESS)return s;
  /* ReadyWindows completes all console/file I/O; Verify is the last call
   * before Windows. Skip has never changed device state, so needs no plan. */
  if(!skip){s=o->Verify(o->Context);if(s!=EFI_SUCCESS)return s;}
  s=o->Windows(o->Context);
  return s==EFI_SUCCESS?EFI_ABORTED:s; /* A real Windows entry never returns. */
}
