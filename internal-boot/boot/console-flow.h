/* GPL-2.0-only. State transitions shared by EFI entry and host negative tests. */
#pragma once
#include "prepare-console.h"
typedef struct {
  VOID *Context;
  EFI_STATUS (*Prepare)(VOID *Context);
  EFI_STATUS (*Choose)(VOID *Context,BOOLEAN *Skip);
  EFI_STATUS (*Capture)(VOID *Context);
  EFI_STATUS (*Detach)(VOID *Context);
  EFI_STATUS (*ReadyWindows)(VOID *Context);
  EFI_STATUS (*Verify)(VOID *Context);
  EFI_STATUS (*Windows)(VOID *Context);
  GU_PREPARE_LOG Record;
} GU_CONSOLE_FLOW_OPS;
EFI_STATUS GuConsoleFlow(const GU_CONSOLE_FLOW_OPS *Ops);
