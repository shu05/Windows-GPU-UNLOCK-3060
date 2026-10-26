/* GPL-2.0-only. Automatic R2 experiment with pre-detach Esc escape.
 * Post-detach input is a protocol probe, not human keyboard liveness proof. */
#include "research-input.h"
#include <Protocol/SimpleTextIn.h>
EFI_STATUS GuResearchChooseInput(EFI_SYSTEM_TABLE *t,BOOLEAN *skip) {
  if(!skip)return EFI_INVALID_PARAMETER;
  *skip=TRUE;
  if(!t || !t->BootServices || !t->BootServices->Stall || !t->ConIn || !t->ConIn->Reset || !t->ConIn->ReadKeyStroke)return EFI_INVALID_PARAMETER;
  EFI_STATUS s=t->ConIn->Reset(t->ConIn,FALSE);if(s!=EFI_SUCCESS)return s;
  for(UINTN i=0;i<50;i++) {
    EFI_INPUT_KEY key;s=t->ConIn->ReadKeyStroke(t->ConIn,&key);
    if(s==EFI_SUCCESS && (key.ScanCode==0x17 || key.UnicodeChar==27))return EFI_SUCCESS;
    if(s!=EFI_SUCCESS && s!=EFI_NOT_READY)return s;
    s=t->BootServices->Stall(100000);if(s!=EFI_SUCCESS)return s;
  }
  *skip=FALSE;return EFI_SUCCESS; /* No key required to run this F8 experiment. */
}
EFI_STATUS GuResearchConfirmInput(EFI_SYSTEM_TABLE *t,GU_PREPARE_LOG log,VOID *context) {
  if(!t || !t->BootServices || !t->BootServices->Stall || !t->ConIn || !t->ConIn->Reset || !t->ConIn->ReadKeyStroke || !log)return EFI_INVALID_PARAMETER;
  EFI_STATUS s=log(context,"r2-input-protocol-probe-no-human-key-required",EFI_SUCCESS);
  if(s!=EFI_SUCCESS)return s;
  s=t->ConIn->Reset(t->ConIn,FALSE);if(s!=EFI_SUCCESS)return s;
  EFI_INPUT_KEY key;s=t->ConIn->ReadKeyStroke(t->ConIn,&key);
  if(s!=EFI_SUCCESS && s!=EFI_NOT_READY)return s;
  return log(context,"r2-input-protocol-probe-passed-not-human-liveness",EFI_SUCCESS);
}
