/* Windows GPU UNLOCK internal boot experiment, 2026-10-20. GPL-2.0-only. */
#pragma once
#include <Uefi.h>
typedef struct {
  VOID *Context;
  EFI_STATUS (*Prepare)(VOID *Context);
  EFI_STATUS (*Select)(VOID *Context,BOOLEAN *SkipUnlock);
  EFI_STATUS (*Record)(VOID *Context,const char *Event,EFI_STATUS Status);
  EFI_STATUS (*Unlock)(VOID *Context);
  EFI_STATUS (*Windows)(VOID *Context);
} GU_HANDOFF_OPS;
EFI_STATUS GuHandoffFlow(const GU_HANDOFF_OPS *Ops);
