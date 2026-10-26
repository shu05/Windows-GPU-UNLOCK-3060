/* GPL-2.0-only. Host-only simulated EFI services for probe.c.
 *
 * Every EFI protocol used here is a C function that records calls in memory.
 * No firmware is executed and no PCI configuration, MMIO, PROM, GPU, boot
 * variable, driver or filesystem operation is possible: the PCI I/O
 * protocol has no write method, the file protocol refuses create/write
 * access and no driver binding protocol exists. Tests exercise the real
 * probe through GuCaptureGraph and directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include "diagnostic.c" /* Real Text/Hex/End/Problem and GuCaptureGraph. */

#define PROM_DWORDS (0x100000U/4U)
static EFI_GUID WantPci=EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID WantSfs=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID WantBind=EFI_DRIVER_BINDING_PROTOCOL_GUID;
static EFI_GUID WantGop=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID Dummy={0x12345678,0,0,{0}};

static EFI_HANDLE PciHandle=(VOID *)1,SfsHandle=(VOID *)2,GraphHandle=(VOID *)3;

enum {SC_OK,SC_CFG_FAIL,SC_BAR_FAIL,SC_ALLOC_FAIL,SC_WRITE_FAIL,SC_NO_TARGET,SC_NO_SFS};

typedef struct {
  int Scenario;
  int IdReads,CfgReads,SubReads,CapReads,RegReads,PromReads;
  int PromAllocs,WriteCalls,WriteFailures,CreateAttempts;
  char Output[128*1024];
  UINTN OutputBytes;
} SIM;

static SIM S;
static EFI_PCI_IO_PROTOCOL Pci;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL RootFile,LogFile;
static EFI_BOOT_SERVICES Bs;
static EFI_RUNTIME_SERVICES Rt;
static EFI_SYSTEM_TABLE Table;

static int Checks;
static void Check(int ok,const char *name) {
  Checks++;
  if(!ok){fprintf(stderr,"FAIL %s scenario=%d\n",name,S.Scenario);exit(1);}
}
static int Logged(const char *needle) {return strstr(S.Output,needle)!=NULL;}

static EFI_STATUS EFIAPI LogWrite(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *data) {
  (void)f;S.WriteCalls++;
  if(S.Scenario==SC_WRITE_FAIL){S.WriteFailures++;return EFI_DEVICE_ERROR;}
  if(S.OutputBytes+*n>=sizeof(S.Output))return EFI_VOLUME_FULL;
  memcpy(S.Output+S.OutputBytes,data,*n);S.OutputBytes+=*n;S.Output[S.OutputBytes]=0;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI LogFlush(EFI_FILE_PROTOCOL *f) {(void)f;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI FileOpen(EFI_FILE_PROTOCOL *f,EFI_FILE_PROTOCOL **out,CHAR16 *name,
                                  UINT64 mode,UINT64 attrs) {
  (void)f;(void)name;(void)attrs;
  /* Read-only directory handle: a create or write request is refused, so the
     harness has no way to produce a real file or an image dump. */
  if(mode&(EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE)){S.CreateAttempts++;return EFI_WRITE_PROTECTED;}
  *out=f;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI FileClose(EFI_FILE_PROTOCOL *f) {(void)f;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI OpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs,EFI_FILE_PROTOCOL **root) {
  (void)fs;*root=&RootFile;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Allocate(EFI_MEMORY_TYPE type,UINTN n,VOID **out) {
  (void)type;
  if(n==0x100000U) {
    if(S.Scenario==SC_ALLOC_FAIL){*out=NULL;return EFI_OUT_OF_RESOURCES;}
    S.PromAllocs++;
  }
  *out=malloc(n);return *out?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI Free(VOID *p) {free(p);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE search,EFI_GUID *g,VOID *key,
                                UINTN *n,EFI_HANDLE **out) {
  (void)key;
  if(search==AllHandles||(g&&!memcmp(g,&WantSfs,sizeof(*g)))||(g&&!memcmp(g,&WantPci,sizeof(*g)))) {
    if(g&&!memcmp(g,&WantSfs,sizeof(*g))&&S.Scenario==SC_NO_SFS)return EFI_NOT_FOUND;
    EFI_HANDLE *h=malloc(sizeof(*h));
    if(!h)return EFI_OUT_OF_RESOURCES;
    h[0]=(search==AllHandles)?GraphHandle:((g&&!memcmp(g,&WantSfs,sizeof(*g)))?SfsHandle:PciHandle);
    *out=h;*n=1;return EFI_SUCCESS;
  }
  /* No driver binding protocol and no graphics output protocol exist here. */
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  if(h==PciHandle&&!memcmp(g,&WantPci,sizeof(*g))){*out=&Pci;return EFI_SUCCESS;}
  if(h==SfsHandle&&!memcmp(g,&WantSfs,sizeof(*g))){*out=&Fs;return EFI_SUCCESS;}
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
  (void)h;EFI_GUID **p=malloc(sizeof(*p));
  if(!p)return EFI_OUT_OF_RESOURCES;
  p[0]=&Dummy;*out=p;*n=1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,
                               UINTN *n) {
  (void)h;(void)g;*out=NULL;*n=0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Variable(CHAR16 *name,EFI_GUID *g,UINT32 *attrs,UINTN *n,VOID *data) {
  (void)name;(void)g;(void)attrs;(void)n;(void)data;return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI PciRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH width,
                                 UINT32 at,UINTN n,VOID *data) {
  (void)io;
  if(width==EfiPciIoWidthUint8){S.CapReads++;*(UINT8 *)data=0;return EFI_SUCCESS;}
  if(width!=EfiPciIoWidthUint32)return EFI_UNSUPPORTED;
  if(at==0&&n==1) {
    S.IdReads++;
    *(UINT32 *)data=(S.Scenario==SC_NO_TARGET)?(GU_BOARD_ID^0x00100000U):GU_BOARD_ID;
    return EFI_SUCCESS;
  }
  if(at==0&&n==64) {
    S.CfgReads++;UINT32 *cfg=data;
    /* Firmware can leave data behind when the read fails, as the supplied
       reproducer models; the probe must not consume it. */
    for(UINTN j=0;j<n;j++)cfg[j]=0x1000U;
    if(S.Scenario==SC_CFG_FAIL)return EFI_DEVICE_ERROR;
    memset(cfg,0,n*sizeof(*cfg));
    cfg[0]=GU_BOARD_ID;cfg[0x10/4]=((UINT32)GU_BAR0_BASE)|0xfU;cfg[0x2c/4]=GU_BOARD_SUBSYSTEM;
    return EFI_SUCCESS;
  }
  if(at==0x2c&&n==1){S.SubReads++;*(UINT32 *)data=GU_BOARD_SUBSYSTEM;return EFI_SUCCESS;}
  return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI MemRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH width,
                                 UINT8 bar,UINT64 at,UINTN n,VOID *data) {
  (void)io;(void)bar;(void)n;
  if(width!=EfiPciIoWidthUint32)return EFI_UNSUPPORTED;
  if(at>=0x300000ULL&&at<0x400000ULL)S.PromReads++;else S.RegReads++;
  if(S.Scenario==SC_BAR_FAIL)return EFI_DEVICE_ERROR;
  *(UINT32 *)data=(UINT32)at;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *io,UINTN *seg,UINTN *bus,UINTN *dev,
                                  UINTN *fun) {
  (void)io;*seg=0;*bus=1;*dev=0;*fun=0;return EFI_SUCCESS;
}

static void Setup(int scenario) {
  memset(&S,0,sizeof(S));S.Scenario=scenario;
  memset(&Pci,0,sizeof(Pci));memset(&Fs,0,sizeof(Fs));
  memset(&RootFile,0,sizeof(RootFile));memset(&LogFile,0,sizeof(LogFile));
  memset(&Bs,0,sizeof(Bs));memset(&Rt,0,sizeof(Rt));memset(&Table,0,sizeof(Table));
  LogFile.Open=FileOpen;LogFile.Close=FileClose;LogFile.Write=LogWrite;LogFile.Flush=LogFlush;
  RootFile=LogFile;
  Fs.OpenVolume=OpenVolume;
  Pci.Pci.Read=PciRead;Pci.Mem.Read=MemRead;Pci.GetLocation=Location;
  Bs.AllocatePool=Allocate;Bs.FreePool=Free;Bs.LocateHandleBuffer=Locate;
  Bs.HandleProtocol=Handle;Bs.ProtocolsPerHandle=Protocols;Bs.OpenProtocolInformation=Opens;
  Rt.GetVariable=Variable;
  Table.BootServices=&Bs;Table.RuntimeServices=&Rt;
  Table.ConsoleOutHandle=(VOID *)7;Table.StandardErrorHandle=(VOID *)7;Table.ConsoleInHandle=(VOID *)7;
}
static EFI_STATUS RunGraph(int scenario) {
  Setup(scenario);
  EFI_STATUS s=GuCaptureGraph(&Table,&LogFile);
  printf("graph scenario=%d cfg=%d sub=%d cap=%d reg=%d prom=%d prom-alloc=%d writes=%d/%d status=0x%llx\n",
         scenario,S.CfgReads,S.SubReads,S.CapReads,S.RegReads,S.PromReads,S.PromAllocs,
         S.WriteFailures,S.WriteCalls,(unsigned long long)s);
  return s;
}
static EFI_STATUS RunProbe(int scenario,BOOLEAN *complete) {
  Setup(scenario);
  static CAPTURE c;
  memset(&c,0,sizeof(c));
  c.Table=&Table;c.File=&LogFile;c.Complete=TRUE;c.Io=EFI_SUCCESS;
  EFI_STATUS s=GuProbeGpu(&Table,&c);
  printf("probe scenario=%d cfg=%d reg=%d prom=%d complete=%d status=0x%llx\n",
         scenario,S.CfgReads,S.RegReads,S.PromReads,c.Complete,(unsigned long long)s);
  if(complete)*complete=c.Complete;
  return s;
}

int main(void) {
  char expected[64];BOOLEAN complete=FALSE;
  EFI_HANDLE *none=NULL;UINTN count=0;EFI_FILE_PROTOCOL *created=NULL;

  /* Structural guarantee: this process holds no mutation service at all. */
  Setup(SC_OK);
  Check(Pci.Pci.Write==NULL&&Pci.Mem.Write==NULL&&Pci.Io.Write==NULL,
        "no PCI configuration or MMIO write method is installed");
  Check(Bs.ExitBootServices==NULL&&Rt.SetVariable==NULL&&Rt.ResetSystem==NULL,
        "no boot-service exit, variable write or reset method is installed");
  Check(Bs.LocateHandleBuffer(ByProtocol,&WantBind,NULL,&count,&none)==EFI_NOT_FOUND,
        "no driver binding protocol is reachable, so no driver can be released");
  Check(Bs.LocateHandleBuffer(ByProtocol,&WantGop,NULL,&count,&none)==EFI_NOT_FOUND,
        "no graphics output protocol is reachable, so no console is touched");
  Check(LogFile.Open(&LogFile,&created,L"IMG00.BIN",
        EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0)==EFI_WRITE_PROTECTED,
        "image dump creation is refused by the harness");

  /* Control: a complete configuration read keeps the intended capture. */
  Check(RunGraph(SC_OK)==EFI_SUCCESS,"successful capture returns success");
  Check(S.CfgReads==1&&S.SubReads==1&&S.CapReads==1,"successful capture reads config space once");
  Check(S.RegReads==16,"successful capture reads all 16 registers");
  Check(S.PromReads==PROM_DWORDS,"successful capture reads the whole PROM window");
  Check(S.PromAllocs==1&&S.CreateAttempts==0,"successful capture allocates one PROM buffer and creates no file");
  Check(Logged("capture-complete=1"),"successful capture is marked complete");
  Check(Logged("cfg dwords="),"successful capture records configuration dwords");
  snprintf(expected,sizeof(expected),"probe-bar0 base=0x%016llx",
           (unsigned long long)(((UINT32)GU_BAR0_BASE)&~0xfU));
  Check(Logged(expected),"successful capture probes BAR0");
  Check(Logged("prom sha256="),"successful capture hashes the PROM window");
  Check(!Logged("incomplete reason"),"successful capture records no failure");

  /* Regression: a failed full configuration read must stop dependent probing. */
  EFI_STATUS s=RunGraph(SC_CFG_FAIL);
  Check(S.CfgReads==1,"failed configuration read attempted once");
  Check(S.RegReads==0&&S.PromReads==0,"failed configuration read performs no register or PROM reads");
  Check(S.PromAllocs==0&&S.SubReads==0&&S.CapReads==0,"failed configuration read skips dependent captures");
  Check(s==EFI_NOT_READY,"failed configuration read is an incomplete capture");
  Check(Logged("capture-complete=0"),"failed configuration read is not marked complete");
  Check(Logged("incomplete reason=cfg256"),"failed configuration read records its reason");
  Check(Logged("cfg-incomplete=0001"),"failed configuration read counts the skipped target");
  Check(!Logged("cfg dwords=")&&!Logged("probe-bar0 base="),"failed configuration read logs no dependent data");
  Check(RunProbe(SC_CFG_FAIL,&complete)==EFI_DEVICE_ERROR,"failed configuration read reports its error status");
  Check(!complete,"failed configuration read clears CAPTURE.Complete directly");
  Check(S.RegReads==0,"direct probe performs no register reads after failure");
  Check(RunProbe(SC_OK,&complete)==EFI_SUCCESS&&complete,"direct probe still succeeds after a good read");
  Check(S.RegReads==16,"direct probe still reads all registers after a good read");
  Check(RunProbe(SC_NO_TARGET,&complete)==EFI_NOT_FOUND&&complete,"an absent target is not a capture failure");
  Check(S.IdReads==1&&S.CfgReads==0,"an absent target is identified but never captured");

  /* Lower-level failures keep their existing incomplete handling. */
  Check(RunGraph(SC_BAR_FAIL)==EFI_NOT_READY,"failed BAR reads are an incomplete capture");
  Check(S.RegReads==16&&S.PromReads==1,"failed BAR reads stop the PROM window");
  Check(Logged("incomplete reason=prom-read"),"failed BAR reads record their reason");
  Check(RunGraph(SC_ALLOC_FAIL)==EFI_NOT_READY,"failed allocation is an incomplete capture");
  Check(S.PromAllocs==0&&S.PromReads==0,"failed allocation performs no PROM reads");
  Check(Logged("incomplete reason=prom-alloc"),"failed allocation records its reason");
  Check(RunGraph(SC_WRITE_FAIL)==EFI_DEVICE_ERROR,"a failed log write is reported");
  Check(S.WriteFailures>0&&!Logged("capture-complete=1"),"a failed log write never claims a complete capture");
  Check(RunGraph(SC_NO_SFS)==EFI_NOT_READY,"an absent dump volume is an incomplete capture");
  Check(S.RegReads==16,"an absent dump volume still probes the target");
  Check(Logged("incomplete reason=dump-volume"),"an absent dump volume records its reason");

  printf("PASS probe host checks: %d; simulated EFI services only; profile=%s\n",Checks,GU_PROFILE_NAME);
  return 0;
}
