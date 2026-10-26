/* Real app/stock transaction in host RAM. No real GPU or firmware calls. */
#include "test_entry_latched.c"
#undef main
#include "../../boot/media.h"
static UINT8 AutoMedia[46];
static int AutoCase,AutoReads;
static EFI_STATUS EFIAPI AutoHandle(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  if(h==H(2) && guid(g,&dg)){*p=AutoMedia;return EFI_SUCCESS;}
  return preflight_handle(h,g,p);
}
static EFI_STATUS EFIAPI AutoKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *k) {
  CHECK(p==&conin && key_reset);AutoReads++;
  if(AutoCase==1){k->ScanCode=0x17;k->UnicodeChar=0;return EFI_SUCCESS;}
  if(AutoCase==2)return EFI_DEVICE_ERROR;
  if(AutoCase==3)return EFI_WARN_UNKNOWN_GLYPH;
  if(AutoCase==4 && AutoReads==5)registers[SS0/4]^=1;
  if(AutoCase==5 && AutoReads==5)modes[0].FrameBufferBase=0;
  if(AutoCase==6 && AutoReads==5)config[16]^=1;
  return EFI_NOT_READY;
}
static EFI_STATUS EFIAPI AutoReset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,BOOLEAN extended) {
  if(AutoCase==7)return EFI_DEVICE_ERROR;
  if(AutoCase==8)return EFI_WARN_UNKNOWN_GLYPH;
  return input_reset(p,extended);
}
static EFI_STATUS EFIAPI AutoStall(UINTN n) {
  if(AutoCase==9 && AutoReads)return EFI_DEVICE_ERROR;
  return app_stall(n);
}
static VOID AutoSetup(int mode) {
  entry_setup(mode==10?APP_HANDOFF:mode==14?APP_FLUSH:APP_SUCCESS,0x03000062,6);
  AutoCase=mode;AutoReads=0;
  memset(AutoMedia,0,sizeof(AutoMedia));
  AutoMedia[0]=4;AutoMedia[1]=1;AutoMedia[2]=42;AutoMedia[4]=1;AutoMedia[16]=1;
  memcpy(AutoMedia+24,&GuInternalPartition,16);AutoMedia[40]=2;AutoMedia[41]=2;
  memcpy(AutoMedia+42,(UINT8[]){127,255,4,0},4);
  bs.HandleProtocol=AutoHandle;bs.Stall=AutoStall;
  conin.ReadKeyStroke=AutoKey;conin.Reset=AutoReset;
  if(mode==11)registers[0x300000/4]^=1;
  if(mode==12)config[4]|=4;
  if(mode==13)registers[0]^=1;
}
int main(int argc,char **argv) {
  CHECK(argc==4);exact_load(argv[1],argv[2]);load_fixture_assets(argv[1],argv[3]);
  memcpy(originals,GuAssets,sizeof(originals));
  for(int mode=0;mode<=14;mode++) {
    AutoSetup(mode);EFI_STATUS result=run_app();
    if(!mode) {
      CHECK(result==EFI_SUCCESS && AutoReads==30 && !key_sent && key_reset);
      CHECK(fake.flrs==1 && fake.gsp_starts==1 && fake.sec2_starts==1 && registers[SS0/4]==0x88888888 && registers[SS1/4]==8);
      CHECK(Gu.quiesced && !halted && !Gu.allocations && !fake.pool_live && !disconnect_calls);
    }else {
      CHECK(EFI_ERROR(result) || result==EFI_WARN_UNKNOWN_GLYPH);
      CHECK(!Gu.started && !fake.mmio_writes && !fake.master_enables && !fake.flrs && !disconnect_calls);
      if(mode==10)CHECK(!AutoReads && !key_reset);
    }
    dispose_host_fixture();
  }
  AutoSetup(0);AutoMedia[24]^=1;
  CHECK(EFI_ERROR(run_app()) && !Gu.started && !AutoReads);dispose_host_fixture();
  puts("PASS automatic real app: no-U stock transaction; Esc/read/reset/stall failures, cold/display/config drift, new owner and wrong partition blocked; no real GPU access");
  return 0;
}
