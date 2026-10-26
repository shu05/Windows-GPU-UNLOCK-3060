/* GPL-2.0-only. Replay private single-RTX capture only in host RAM. */
#define main LegacyConsoleTestMain
#include "test_console.c"
#undef main
#include "research-input.h"
static int KeyScenario,Reads,Stalls,Resets,InputLogs;
static EFI_STATUS EFIAPI ResetKeys(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *in,BOOLEAN extended) {
  (void)in;(void)extended;Resets++;Reads=0;
  return KeyScenario==1?EFI_DEVICE_ERROR:KeyScenario==2?EFI_WARN_UNKNOWN_GLYPH:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NewKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *in,EFI_INPUT_KEY *key) {
  (void)in;Reads++;*key=(EFI_INPUT_KEY){0};
  if(KeyScenario==3)return EFI_DEVICE_ERROR;
  if(KeyScenario==4)return EFI_WARN_UNKNOWN_GLYPH;
  if(KeyScenario==5)return EFI_NOT_READY;
  if(KeyScenario==7 || KeyScenario==8)return EFI_NOT_READY;
  if(KeyScenario==6){key->ScanCode=0x17;return EFI_SUCCESS;}
  if(Reads==1){key->UnicodeChar='T';return EFI_SUCCESS;}
  if(Reads<3)return EFI_NOT_READY;
  key->UnicodeChar='\r';return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI StallKeys(UINTN us) {
  Check(us==100000,"bounded poll interval");Stalls++;
  return KeyScenario==7?EFI_DEVICE_ERROR:KeyScenario==8?EFI_WARN_UNKNOWN_GLYPH:EFI_SUCCESS;
}
static EFI_STATUS InputLog(VOID *ctx,const char *event,EFI_STATUS s) {
  (void)ctx;(void)event;(void)s;InputLogs++;
  return (KeyScenario==9 || (KeyScenario==10 && InputLogs==2))?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS FlowRealChoose(VOID *v,BOOLEAN *skip) {
  (void)v;return GuResearchChooseInput(LastTable,skip);
}
static void InputTests(void) {
  for(KeyScenario=0;KeyScenario<=10;KeyScenario++) {
    Reads=Stalls=Resets=InputLogs=0;
    EFI_BOOT_SERVICES bs={0};EFI_SIMPLE_TEXT_INPUT_PROTOCOL keys={0};EFI_SYSTEM_TABLE t={0};
    bs.Stall=StallKeys;keys.Reset=ResetKeys;keys.ReadKeyStroke=NewKey;t.BootServices=&bs;t.ConIn=&keys;
    EFI_STATUS s=GuResearchConfirmInput(&t,InputLog,NULL);
    int success=KeyScenario==0 || KeyScenario==5 || KeyScenario==6 || KeyScenario==7 || KeyScenario==8;
    Check(success==(s==EFI_SUCCESS),"input protocol probe accepts no key, rejects actual read/reset/log failures");
    Check(Resets==(KeyScenario==9?0:1),"queued input always reset before post-detach reads");
    if(success)Check(Reads==1 && !Stalls && InputLogs==2,"one probe without waiting for human input");
  }
  int scenarios[]={0,1,2,3,4,5,6,7,8};
  for(UINTN i=0;i<sizeof(scenarios)/sizeof(scenarios[0]);i++) {
    Check(Run(0)==EFI_SUCCESS,"host table for actual selection policy");
    KeyScenario=scenarios[i];Reads=Stalls=Resets=0;
    LastTable->BootServices->Stall=StallKeys;Keys.Reset=ResetKeys;Keys.ReadKeyStroke=NewKey;
    FLOW_TEST f={0};
    GU_CONSOLE_FLOW_OPS o={&f,FlowPrepare,FlowRealChoose,FlowCapture,FlowDetach,FlowReady,FlowVerify,FlowWindows,FlowRecord};
    EFI_STATUS s=GuConsoleFlow(&o);
    if(KeyScenario==0 || KeyScenario==5)Check(s==EFI_ABORTED && f.detaches==1 && f.boots==1 && Stalls==50,"automatic path with or without keys chooses exactly one transaction");
    else if(KeyScenario==6)Check(s==EFI_ABORTED && !f.detaches && f.boots==1,"optional initial Esc boots without detach");
    else Check(s!=EFI_SUCCESS && !f.detaches && !f.boots,"actual selection error/warning cannot detach or boot");
  }
}
int main(void) {
  Check(Run(0)==EFI_SUCCESS && Calls==2 && !OutputCalls,"single physical RTX survives console removal without output calls");
  Check(Live.Count+3==sizeof(FixtureEdges)/sizeof(FixtureEdges[0]),"only three reviewed RTX consumer edges removed");
  Check(GuVerifyPreparedConsole(LastTable)==EFI_SUCCESS,"fresh final graph passes");
  int bad[]={1,2,3,4,7,8,9,10,11,12,13,14,16,19,20,25,27,28,29};
  for(UINTN i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
    Check(Run(bad[i])!=EFI_SUCCESS,"identity/driver/graph/disconnect/log/route failures stop");
    int n=bad[i],expected=(n==3 || n==4 || n==10 || n==12 || n==16 || n==19 || n==20 || n==25)?1:n==7?2:0;
    Check(Calls==expected,"no further disconnection after first failure");
  }
  for(int late=0;late<7;late++) {
    Check(Run(0)==EFI_SUCCESS,"new session for protected-state drift");
    if(late==0)RtxMode.FrameBufferBase+=4096;
    if(late==1)RtxInfo.HorizontalResolution--;
    if(late==2)Keys.WaitForKey=NULL;
    if(late==3){int found=0;for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Handle==FX_RtxPci && Live.Edges[i].Agent==FX_NvAgent){Live.Edges[i].Count++;found=1;break;}Check(found,"real NV ownership edge exists");}
    if(late==4)Live.Edges[0].Count++;
    if(late==5){int found=0;for(UINTN i=0;i<Live.Count;i++)if(Live.Edges[i].Agent==FX_EdidAgent){Live.Edges[i].Count++;found=1;break;}Check(found,"real shared EDID edge exists");}
    if(late==6)Scenario=30;
    EFI_STATUS verified=GuVerifyPreparedConsole(LastTable);
    if(verified==EFI_SUCCESS)fprintf(stderr,"late drift case=%d\n",late);
    Check(verified!=EFI_SUCCESS,"late GOP/input/NV/protected graph drift stops");
  }
  FLOW_TEST f={.detach=EFI_TIMEOUT};
  Check(FlowRun(&f)==EFI_TIMEOUT && !f.boots && f.captures==2,"input timeout after mutation captures but never boots");
  f=(FLOW_TEST){.skip=TRUE};Check(FlowRun(&f)==EFI_ABORTED && f.boots==1 && !f.detaches,"pre-mutation skip boots unchanged");
  EFI_STATUS errors[]={EFI_DEVICE_ERROR,EFI_ACCESS_DENIED,EFI_WARN_UNKNOWN_GLYPH};
  for(UINTN i=0;i<3;i++) {
    f=(FLOW_TEST){.before=errors[i]};Check(FlowRun(&f)==errors[i] && !f.detaches && !f.boots,"pre-capture failure cannot mutate or boot");
    f=(FLOW_TEST){.detach=errors[i]};Check(FlowRun(&f)==errors[i] && f.detaches==1 && f.captures==2 && !f.boots,"partial detach failure cannot boot");
    f=(FLOW_TEST){.after=errors[i]};Check(FlowRun(&f)==errors[i] && !f.boots,"post-capture failure cannot boot");
    f=(FLOW_TEST){.record=errors[i]};Check(FlowRun(&f)==errors[i] && !f.boots,"log failure cannot boot");
    f=(FLOW_TEST){.ready=errors[i]};Check(FlowRun(&f)==errors[i] && !f.boots,"file close failure cannot boot");
    f=(FLOW_TEST){.verify=errors[i]};Check(FlowRun(&f)==errors[i] && !f.boots,"final verification failure cannot boot");
  }
  for(int late=1;late<=3;late++) {
    Check(Run(0)==EFI_SUCCESS,"new session before final-callback guard");
    f=(FLOW_TEST){.realVerify=TRUE,.lateChange=late};
    Check(FlowRun(&f)!=EFI_SUCCESS && !f.boots,"final verifier blocks handoff on late mutation");
  }
  InputTests();
  printf("PASS R2 checks: %d; mock/replay only, no GPU/firmware access\n",Checks);
  return 0;
}
