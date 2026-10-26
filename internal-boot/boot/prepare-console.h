/* GPL-2.0-only. Stage2A: two RTX console consumers only, never GOP owner. */
#pragma once
#include <Uefi.h>
typedef EFI_STATUS (*GU_PREPARE_LOG)(VOID *Context,const char *Event,EFI_STATUS Status);
EFI_STATUS GuPrepareConsole(EFI_SYSTEM_TABLE *Table,GU_PREPARE_LOG Log,VOID *Context);
/* Recheck after final capture/log/file close, immediately before StartImage. */
EFI_STATUS GuVerifyPreparedConsole(EFI_SYSTEM_TABLE *Table);
