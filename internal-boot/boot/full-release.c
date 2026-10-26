/* GPL-2.0-only. Exact-card driver release + strictly bounded decode restore.
 * Includes Stage2A unchanged in this separate build. Never writes SS selectors,
 * BME, BARs, driver globals, firmware variables, or a GPU reset register.
 * The original UNLOCK.EFI owns all unlock/DMA/FLR behavior and manual U.
 */
#include "full-release.h"
static BOOLEAN OwnRecord(EFI_HANDLE Handle,const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *Entry);
#define GU_SNAPSHOT_OWN_RECORD OwnRecord
#include "prepare-console.c"
#include "decode-policy.h"
#include "../unlock/checks.h"
#include "../unlock/sha256.h"
#include <Protocol/EdidDiscovered.h>
#include <IndustryStandard/Acpi.h>

#define GU_HEADLESS_MAX_DISPLAY_CHILDREN 4

static EFI_GUID EdidGuid=EFI_EDID_DISCOVERED_PROTOCOL_GUID;
static const EFI_GUID MouseFile={0xc7a7030c,0xc3d8,0x45ee,{0xbe,0xd9,0x5d,0x9e,0x76,0x76,0x29,0x53}};
#if GU_NVGOP_PATH_PIWG
static const EFI_GUID NvGopFile=GU_NVGOP_FILE_GUID;
#endif
typedef struct {
  EFI_SYSTEM_TABLE *Table;
  EFI_HANDLE Image,EdidAgent,NvAgent;
  GU_PREPARE_LOG Log;
  VOID *Context;
  UINT32 Config[64],Cold[9];
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR Bar;
  UINT64 Attributes;
  BOOLEAN Captured,Released,Owned,Restored;
#ifdef GU_HEADLESS_RESEARCH
  EFI_HANDLE Children[GU_HEADLESS_MAX_DISPLAY_CHILDREN];
  UINT8 ChildPaths[GU_HEADLESS_MAX_DISPLAY_CHILDREN][36];
  UINTN ChildCount;
  UINTN EdidDetached;
#endif
} FULL;
static FULL Full;
static BOOLEAN OwnRecord(EFI_HANDLE h,const EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e) {
  return Full.Owned && h==PreparedPlan.RtxPci && e->AgentHandle==Full.Image && !e->ControllerHandle &&
    e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE && e->OpenCount==1;
}
static const UINT32 ColdOffsets[]={0,0x118128,0x118234,0x110100,0x1fa828,0x823804,0x82381c,0x823820,0x1183a4};
static EFI_STATUS Note(const char *event,EFI_STATUS s) {
  EFI_STATUS written=Full.Log(Full.Context,event,s);return s==EFI_SUCCESS?written:s;
}
static EFI_STATUS LogConfig(const char *label,const UINT32 *cfg) {
  for(UINTN row=0;row<16;row++) {
    char event[100];UINTN n=0;
    for(UINTN i=0;label[i] && n<30;i++)event[n++]=label[i];
    event[n++]='-';event[n++]="0123456789abcdef"[row];event[n++]='=';
    for(UINTN word=0;word<4;word++) {
      for(int digit=7;digit>=0;digit--)event[n++]="0123456789abcdef"[(cfg[row*4+word]>>(digit*4))&15];
      event[n++]=',';
    }
    event[n]=0;EFI_STATUS s=Note(event,EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS ReadConfigFull(UINT32 *cfg) {
  EFI_PCI_IO_PROTOCOL *io=NULL;
  EFI_STATUS s=Interface(Full.Table->BootServices,PreparedPlan.RtxPci,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS || io!=PreparedPlan.RtxIo || !io->GetLocation || !io->Pci.Read)return EFI_NOT_READY;
  UINTN seg=0,bus=0,dev=0,fun=0;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS || seg || bus!=1 || dev || fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,cfg);
  if(s==EFI_SUCCESS && (cfg[0]!=GU_BOARD_ID || cfg[11]!=GU_BOARD_SUBSYSTEM || cfg[2]!=0x030000a1 || cfg[4]!=(UINT32)GU_BAR0_BASE
#if !GU_MSI_INT10_CONSOLE
     || (cfg[1]&4)
#endif
     ))s=EFI_UNSUPPORTED;
  return s;
}
/* Shared descriptor policy for both pre-release reads and the later permit. */
static EFI_STATUS BarDescription(const EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d) {
  if(!d || d->Desc!=ACPI_ADDRESS_SPACE_DESCRIPTOR || d->Len!=sizeof(*d)-3 || d->ResType || d->AddrSpaceGranularity!=32 ||
     d->AddrRangeMin!=GU_BAR0_BASE || d->AddrLen!=0x1000000 || d->AddrTranslationOffset ||
     (d->AddrRangeMax!=GU_BAR0_END && d->AddrRangeMax!=0x00ffffff) || ((const UINT8 *)(d+1))[0]!=ACPI_END_TAG_DESCRIPTOR)return EFI_UNSUPPORTED;
  UINT8 sum=0;for(UINTN i=0;i<sizeof(*d)+2;i++)sum=(UINT8)(sum+((const UINT8 *)d)[i]);
  return ((const UINT8 *)d)[sizeof(*d)+1] && sum?EFI_CRC_ERROR:EFI_SUCCESS;
}
static EFI_STATUS CheckBar(BOOLEAN first) {
  EFI_PCI_IO_PROTOCOL *io=PreparedPlan.RtxIo;VOID *raw=NULL;
  if(!io->GetBarAttributes)return EFI_UNSUPPORTED;
  EFI_STATUS s=io->GetBarAttributes(io,0,NULL,&raw);
  if(s==EFI_SUCCESS) {
    EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=raw;
    s=BarDescription(d);
    if(s==EFI_SUCCESS) {
      if(first)Full.Bar=*d;
      else if(!Bytes(d,&Full.Bar,sizeof(*d)))s=EFI_NOT_READY;
    }
  }
  if(raw && Full.Table->BootServices->FreePool(raw)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static EFI_STATUS ColdRead(EFI_PCI_IO_PROTOCOL *io,UINT32 *values) {
  if(!io || !io->Mem.Read)return EFI_UNSUPPORTED;
  for(UINTN i=0;i<9;i++) {
    EFI_STATUS s=io->Mem.Read(io,EfiPciIoWidthUint32,0,ColdOffsets[i],1,&values[i]);
    if(s!=EFI_SUCCESS)return s;
  }
  if(values[0]!=GU_CHIP_ID || values[1]!=0x8b8f || values[2]!=0x3ff || values[3]!=0x10 || values[4] ||
     values[5]!=0xffffff8f || values[6]==0xffffffff || values[6]==0x88888888 || (values[6]&0xffff0000)==0xbadf0000 ||
     values[7]>15 || values[8]!=GU_DEVICE_MEMORY_SIZE)return EFI_NOT_READY;
  return EFI_SUCCESS;
}
static EFI_STATUS Cold(UINT32 *values) {return ColdRead(PreparedPlan.RtxIo,values);}
/* No global Full/PreparedPlan authority is created by this read-only path. */
static EFI_STATUS PeekConfig(EFI_BOOT_SERVICES *bs,EFI_HANDLE handle,EFI_PCI_IO_PROTOCOL *io,const UINT32 *expected) {
  UINTN seg=0,bus=0,dev=0,fun=0;UINT32 cfg[16];
  EFI_PCI_IO_PROTOCOL *current=NULL;
  EFI_STATUS s=Interface(bs,handle,&PciGuid,(VOID **)&current);
  if(s!=EFI_SUCCESS)return s;
  if(current!=io)return EFI_NOT_READY;
  if(!io->GetLocation || !io->Pci.Read)return EFI_UNSUPPORTED;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS)return s;
  if(seg || bus!=1 || dev || fun)return EFI_UNSUPPORTED;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg);
  if(s!=EFI_SUCCESS)return s;
  if(!Bytes(cfg,expected,sizeof(cfg)))return EFI_NOT_READY;
  return EFI_SUCCESS;
}
static EFI_STATUS PeekBar(EFI_BOOT_SERVICES *bs,EFI_PCI_IO_PROTOCOL *io) {
  if(!io->GetBarAttributes)return EFI_UNSUPPORTED;
  VOID *raw=NULL;EFI_STATUS s=io->GetBarAttributes(io,0,NULL,&raw);
  if(s==EFI_SUCCESS)s=BarDescription(raw);
  if(raw && bs->FreePool(raw)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
/* Read-only check before any driver release: the exact RTX at 0000:01:00.0 is
 * read through the PciIo its firmware driver still owns; nothing is opened,
 * written or reconfigured. Any non-cold state (warm restart, already unlocked,
 * initialized WPR2) returns EFI_NOT_READY so the caller can boot unchanged. */
EFI_STATUS GuPeekCold(EFI_SYSTEM_TABLE *t,UINT32 *values) {
  if(!t || !t->BootServices || !values)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *bs=t->BootServices;EFI_HANDLE *handles=NULL,targetHandle=NULL;UINTN n=0,found=0;EFI_PCI_IO_PROTOCOL *target=NULL;
  UINT32 targetConfig[16];
  for(UINTN i=0;i<9;i++)values[i]=0;
  if(!bs->LocateHandleBuffer || !bs->HandleProtocol || !bs->FreePool)return EFI_UNSUPPORTED;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&n,&handles);
  if(s==EFI_SUCCESS && (!handles || !n))s=EFI_NOT_FOUND;
  for(UINTN i=0;s==EFI_SUCCESS && i<n;i++) {
    EFI_PCI_IO_PROTOCOL *io=NULL;UINTN seg=0,bus=0,dev=0,fun=0;UINT32 cfg[16];
    if(Interface(bs,handles[i],&PciGuid,(VOID **)&io)!=EFI_SUCCESS || !io->GetLocation || !io->Pci.Read)continue;
    if(io->GetLocation(io,&seg,&bus,&dev,&fun)!=EFI_SUCCESS || seg || bus!=1 || dev || fun)continue;
    if(io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg)!=EFI_SUCCESS)continue;
    if(cfg[0]!=GU_BOARD_ID || cfg[11]!=GU_BOARD_SUBSYSTEM || (cfg[2]>>24)!=3)continue;
    found++;target=io;targetHandle=handles[i];for(UINTN j=0;j<16;j++)targetConfig[j]=cfg[j];
  }
  if(handles && bs->FreePool(handles)!=EFI_SUCCESS && s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s!=EFI_SUCCESS)return s;
  if(found!=1)return EFI_NOT_FOUND;
  /* Require memory decode, no bus mastering and the exact 32-bit memory BAR
   * before asking for its descriptor, then recheck after allocation/free calls.
   * Firmware callbacks are not atomic; this guards service-return boundaries.
   * The MSI CSM firmware leaves IO and bus-master decode enabled at entry;
   * only the memory-decode bit is provable there, and the unlock transaction
   * itself clears bus mastering before any write. */
#if GU_MSI_INT10_CONSOLE
  if((targetConfig[1]&2U)!=2U || targetConfig[4]!=(UINT32)GU_BAR0_BASE)return EFI_UNSUPPORTED;
#else
  if((targetConfig[1]&6U)!=2U || targetConfig[4]!=(UINT32)GU_BAR0_BASE)return EFI_UNSUPPORTED;
#endif
  s=PeekConfig(bs,targetHandle,target,targetConfig);
  if(s==EFI_SUCCESS)s=PeekBar(bs,target);
  if(s==EFI_SUCCESS)s=PeekConfig(bs,targetHandle,target,targetConfig);
  if(s!=EFI_SUCCESS)return s;
  return ColdRead(target,values);
}
static EFI_STATUS Prom(VOID) {
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;EFI_PCI_IO_PROTOCOL *io=PreparedPlan.RtxIo;
  UINT8 *data=NULL;EFI_STATUS s=bs->AllocatePool(EfiBootServicesData,1048576,(VOID **)&data);
  if(s!=EFI_SUCCESS || !data)return EFI_OUT_OF_RESOURCES;
  if(!io->Mem.Read)s=EFI_UNSUPPORTED;
  for(UINTN at=0;s==EFI_SUCCESS && at<1048576;at+=65536)s=io->Mem.Read(io,EfiPciIoWidthUint8,0,0x300000+at,65536,data+at);
  if(s==EFI_SUCCESS && !GuHashMatches(data,1048576,GU_PROM_SHA256))s=EFI_CRC_ERROR;
  if(bs->FreePool(data)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
}
static EFI_STATUS Profile(EFI_HANDLE agent,BOOLEAN mouse) {
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Interface(bs,agent,&BindingGuid,(VOID **)&d);
  if(s!=EFI_SUCCESS || !d || !d->Stop || d->DriverBindingHandle!=agent || d->ImageHandle!=agent || d->Version!=(mouse?1:GU_NVGOP_VERSION))return EFI_UNSUPPORTED;
  s=Interface(bs,d->ImageHandle,&ImageGuid,(VOID **)&im);
  UINTN size=mouse?GU_EDID_IMAGE_SIZE:GU_NVGOP_IMAGE_SIZE,offset=mouse?GU_EDID_STOP:GU_NVGOP_STOP;
  if(s!=EFI_SUCCESS || !im || !im->ImageBase || im->ImageSize!=size || im->ImageCodeType!=(mouse?EfiRuntimeServicesCode:GU_DRIVER_CODE_TYPE) ||
     (UINTN)d->Stop!=(UINTN)im->ImageBase+offset || !im->FilePath)return EFI_UNSUPPORTED;
  UINTN n=0;
  if(!GuInspectPath(im->FilePath,NULL,&n,NULL,NULL))return EFI_UNSUPPORTED;
  if(mouse) {
    if(n!=24 || im->FilePath->Type!=MEDIA_DEVICE_PATH || im->FilePath->SubType!=MEDIA_PIWG_FW_FILE_DP ||
       im->FilePath->Length[0]!=20 || im->FilePath->Length[1] || !GuGuidEqual((UINT8 *)im->FilePath+4,&MouseFile))return EFI_UNSUPPORTED;
  } else if(GU_NVGOP_PATH_PIWG) {
    /* BIOS-resident firmware video driver: identified by its PIWG firmware
     * volume file GUID instead of the OptionROM FILEPATH_DP name. */
    if(n!=24 || im->FilePath->Type!=MEDIA_DEVICE_PATH || im->FilePath->SubType!=MEDIA_PIWG_FW_FILE_DP ||
       im->FilePath->Length[0]!=20 || im->FilePath->Length[1] || !GuGuidEqual((UINT8 *)im->FilePath+4,&NvGopFile))return EFI_UNSUPPORTED;
  } else {
    const CHAR16 name[]=L"nvgop-ga1xx";
    if(im->DeviceHandle!=PreparedPlan.RtxPci || n!=4+sizeof(name)+4 || im->FilePath->Type!=MEDIA_DEVICE_PATH || im->FilePath->SubType!=MEDIA_FILEPATH_DP ||
       im->FilePath->Length[0]!=4+sizeof(name) || im->FilePath->Length[1] || !Bytes((UINT8 *)im->FilePath+4,name,sizeof(name)))return EFI_UNSUPPORTED;
  }
  /* Both original PE relocation tables contain zero relocations in .text.
   * Therefore hash the loaded code range directly; never hash relocated data. */
  UINTN rva=mouse?0x1000:GU_NVGOP_TEXT_RVA,bytes=mouse?GU_EDID_TEXT_BYTES:GU_NVGOP_TEXT_BYTES;
  const char *hash=mouse?GU_EDID_TEXT_SHA256:GU_NVGOP_TEXT_SHA256;
  return GuHashMatches((UINT8 *)im->ImageBase+rva,bytes,hash)?EFI_SUCCESS:EFI_CRC_ERROR;
}
static EFI_STATUS FullConsumers(const GRAPH *g) {
#if defined(GU_HEADLESS_RESEARCH) && GU_CONSOLE_AGGREGATE
  UINTN owners=0,children=0,edid=0,combo=0;
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];if(e->Handle!=PreparedPlan.RtxPci)continue;
    if(e->Count!=1 || !e->Agent)return EFI_ACCESS_DENIED;
    if(GuGuidEqual(&e->Protocol,&PathGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER){children++;Full.Children[0]=e->Controller;}
    else if(e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==PreparedPlan.RtxPci) {
      if(e->Agent!=PreparedPlan.NvAgent)return EFI_ACCESS_DENIED;
      if(GuGuidEqual(&e->Protocol,&PciGuid)){Full.NvAgent=e->Agent;owners++;}
    } else return EFI_ACCESS_DENIED;
  }
  if(owners!=1 || children!=1 || Full.NvAgent==Full.Image || Full.NvAgent!=PreparedPlan.NvAgent ||
     Full.Children[0]!=PreparedPlan.Rtx)return EFI_ACCESS_DENIED;
  {
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;VOID *edid=NULL;
    EFI_STATUS s=Interface(bs,Full.Children[0],&PathGuid,(VOID **)&path);
    if(s!=EFI_SUCCESS || !DisplayRoute(path,RtxPath))return EFI_UNSUPPORTED;
    UINT32 cfg[16];EFI_HANDLE pci=NULL;EFI_PCI_IO_PROTOCOL *io=NULL;
    s=PciFor(bs,path,1,GU_BOARD_ID,GU_BOARD_SUBSYSTEM,&pci,&io,cfg);
    if(s!=EFI_SUCCESS || pci!=PreparedPlan.RtxPci || io!=PreparedPlan.RtxIo || !Bytes(cfg,PreparedPlan.RtxConfig,sizeof(cfg)))return EFI_NOT_READY;
    for(UINTN j=0;j<36;j++)Full.ChildPaths[0][j]=((UINT8 *)path)[j];
    if(Interface(bs,Full.Children[0],&EdidGuid,&edid)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
  }
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];if(e->Handle!=Full.Children[0])continue;
    if(e->Count!=1 || !e->Agent)return EFI_ACCESS_DENIED;
    if(GuGuidEqual(&e->Protocol,&EdidGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==Full.Children[0] && e->Agent==PreparedPlan.EdidAgent){Full.EdidAgent=e->Agent;edid++;}
    else if(GuGuidEqual(&e->Protocol,&GopGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==Full.Children[0] && e->Agent==PreparedPlan.ComboAgent)combo++;
    else if(GuGuidEqual(&e->Protocol,&GopGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->Controller==PreparedPlan.Agg && e->Agent==PreparedPlan.ComboAgent)combo++;
    else return EFI_ACCESS_DENIED;
  }
  if(edid!=1 || combo!=2 || Full.EdidAgent!=PreparedPlan.EdidAgent || Full.EdidAgent==Full.NvAgent || Full.EdidAgent==Full.Image)return EFI_ACCESS_DENIED;
  /* The aggregate topology has exactly one display child; record it for the
   * shared stage-3 EDID detach loop (v0.3.1 runtime child discovery). */
  Full.ChildCount=1;
  return EFI_SUCCESS;
#elif defined(GU_HEADLESS_RESEARCH) && GU_MSI_INT10_CONSOLE
  /* MSI Z390 AMI CSM INT10 topology: the firmware video driver records its
   * display child through a PciIo BY_CHILD_CONTROLLER open (not the parent
   * device path), the child carries the console text-out driver chain and a
   * GOP consumer instead of an EDID consumer, and no separate EDID/mouse
   * runtime agent exists on this firmware. */
  UINTN owners=0,children=0;
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];if(e->Handle!=PreparedPlan.RtxPci)continue;
    if(e->Count!=1)return EFI_ACCESS_DENIED;
    if(GuGuidEqual(&e->Protocol,&PciGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==PreparedPlan.RtxPci){Full.NvAgent=e->Agent;owners++;}
    else if(GuGuidEqual(&e->Protocol,&PciGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
      if(children>=GU_HEADLESS_MAX_DISPLAY_CHILDREN)return EFI_ACCESS_DENIED;
      Full.Children[children++]=e->Controller;
    }
    else return EFI_ACCESS_DENIED;
  }
  if(owners!=1 || !children || Full.NvAgent==Full.Image)return EFI_ACCESS_DENIED;
  Full.ChildCount=children;
  UINTN consoleChild=children;
  for(UINTN c=0;c<children;c++) {
    for(UINTN prior=0;prior<c;prior++)if(Full.Children[prior]==Full.Children[c])return EFI_ACCESS_DENIED;
    if(Full.Children[c]==PreparedPlan.Rtx)consoleChild=c;
  }
  if(consoleChild==children)return EFI_ACCESS_DENIED;
  if(consoleChild){EFI_HANDLE h=Full.Children[0];Full.Children[0]=Full.Children[consoleChild];Full.Children[consoleChild]=h;}
  for(UINTN c=0;c<Full.ChildCount;c++) {
    if(Full.Children[c]==PreparedPlan.Console || Full.Children[c]==PreparedPlan.Input || Full.Children[c]==PreparedPlan.Error || Full.Children[c]==PreparedPlan.RtxPci)return EFI_ACCESS_DENIED;
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;VOID *edid=NULL,*other=NULL;
    EFI_STATUS s=Interface(bs,Full.Children[c],&PathGuid,(VOID **)&path);
    if(s!=EFI_SUCCESS || !DisplayRoute(path,RtxPath))return EFI_UNSUPPORTED;
    UINT32 cfg[16];EFI_HANDLE pci=NULL;EFI_PCI_IO_PROTOCOL *io=NULL;
    s=PciFor(bs,path,1,GU_BOARD_ID,GU_BOARD_SUBSYSTEM,&pci,&io,cfg);
    if(s!=EFI_SUCCESS || pci!=PreparedPlan.RtxPci || io!=PreparedPlan.RtxIo || !Bytes(cfg,PreparedPlan.RtxConfig,sizeof(cfg)))return EFI_NOT_READY;
    for(UINTN j=0;j<36;j++)Full.ChildPaths[c][j]=((UINT8 *)path)[j];
    for(UINTN prior=0;prior<c;prior++)if(Bytes(Full.ChildPaths[prior],path,36))return EFI_UNSUPPORTED;
    if(Interface(bs,Full.Children[c],&EdidGuid,&edid)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
    if(c) {
      s=Interface(bs,Full.Children[c],&GopGuid,&other);
      if(!((s==EFI_NOT_FOUND || s==EFI_UNSUPPORTED) && !other))return EFI_UNSUPPORTED;
      s=Interface(bs,Full.Children[c],&TextGuid,&other);
      if(!((s==EFI_NOT_FOUND || s==EFI_UNSUPPORTED) && !other))return EFI_UNSUPPORTED;
    }
    /* This preflight runs after the stage-2a console detach (verified by
     * GuVerifyPreparedConsole just above), so the text-out driver chain and
     * the GOP consumer edges that the probe recorded on the console child
     * are already closed; the child must now carry no active consumer edge
     * at all. The pinned OutAgent/GfxAgent identities stay in the plan for
     * the stage-4 driver disconnect and the child release. */
    UINTN text=0,gfx=0;
    for(UINTN i=0;i<g->Count;i++) {
      const EDGE *e=&g->Edges[i];
      if(e->Handle==PreparedPlan.RtxPci && e->Agent!=Full.NvAgent)return EFI_ACCESS_DENIED;
      if(e->Handle!=Full.Children[c])continue;
      if(e->Count!=1 || !e->Agent)return EFI_ACCESS_DENIED;
      if(GuGuidEqual(&e->Protocol,&TextGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==Full.Children[c] && e->Agent==PreparedPlan.OutAgent)text++;
      else if(GuGuidEqual(&e->Protocol,&TextGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->Controller==PreparedPlan.Console && e->Agent==PreparedPlan.OutAgent)text++;
      else if(GuGuidEqual(&e->Protocol,&GopGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==Full.Children[c] && e->Agent==PreparedPlan.GfxAgent)gfx++;
      else return EFI_ACCESS_DENIED;
    }
    if(c)continue;
    if(text!=0 || gfx!=0 || !PreparedPlan.OutAgent || !PreparedPlan.GfxAgent || PreparedPlan.OutAgent==PreparedPlan.GfxAgent)return EFI_ACCESS_DENIED;
  }
  return Full.NvAgent!=PreparedPlan.OutAgent && Full.NvAgent!=PreparedPlan.GfxAgent &&
    PreparedPlan.OutAgent!=Full.Image && PreparedPlan.GfxAgent!=Full.Image?EFI_SUCCESS:EFI_ACCESS_DENIED;
#elif defined(GU_HEADLESS_RESEARCH)
  UINTN owners=0,children=0;
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];if(e->Handle!=PreparedPlan.RtxPci)continue;
    if(e->Count!=1)return EFI_ACCESS_DENIED;
    if(GuGuidEqual(&e->Protocol,&PciGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==PreparedPlan.RtxPci){Full.NvAgent=e->Agent;owners++;}
    else if(GuGuidEqual(&e->Protocol,&PathGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
      if(children>=GU_HEADLESS_MAX_DISPLAY_CHILDREN)return EFI_ACCESS_DENIED;
      Full.Children[children++]=e->Controller;
    }
    else return EFI_ACCESS_DENIED;
  }
  if(owners!=1 || !children || Full.NvAgent==Full.Image)return EFI_ACCESS_DENIED;
  Full.ChildCount=children;
  UINTN consoleChild=children;
  for(UINTN c=0;c<children;c++) {
    for(UINTN prior=0;prior<c;prior++)if(Full.Children[prior]==Full.Children[c])return EFI_ACCESS_DENIED;
    if(Full.Children[c]==PreparedPlan.Rtx)consoleChild=c;
  }
  if(consoleChild==children)return EFI_ACCESS_DENIED;
  if(consoleChild){EFI_HANDLE h=Full.Children[0];Full.Children[0]=Full.Children[consoleChild];Full.Children[consoleChild]=h;}
  for(UINTN c=0;c<Full.ChildCount;c++) {
    if(Full.Children[c]==PreparedPlan.Console || Full.Children[c]==PreparedPlan.Input || Full.Children[c]==PreparedPlan.Error || Full.Children[c]==PreparedPlan.RtxPci)return EFI_ACCESS_DENIED;
    EFI_DEVICE_PATH_PROTOCOL *path=NULL;VOID *edid=NULL,*other=NULL;
    EFI_STATUS s=Interface(bs,Full.Children[c],&PathGuid,(VOID **)&path);
    if(s!=EFI_SUCCESS || !DisplayRoute(path,RtxPath))return EFI_UNSUPPORTED;
    UINT32 cfg[16];EFI_HANDLE pci=NULL;EFI_PCI_IO_PROTOCOL *io=NULL;
    s=PciFor(bs,path,1,GU_BOARD_ID,GU_BOARD_SUBSYSTEM,&pci,&io,cfg);
    if(s!=EFI_SUCCESS || pci!=PreparedPlan.RtxPci || io!=PreparedPlan.RtxIo || !Bytes(cfg,PreparedPlan.RtxConfig,sizeof(cfg)))return EFI_NOT_READY;
    for(UINTN j=0;j<36;j++)Full.ChildPaths[c][j]=((UINT8 *)path)[j];
    for(UINTN prior=0;prior<c;prior++)if(Bytes(Full.ChildPaths[prior],path,36))return EFI_UNSUPPORTED;
    if(Interface(bs,Full.Children[c],&EdidGuid,&edid)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
    if(c) {
      s=Interface(bs,Full.Children[c],&GopGuid,&other);
      if(!((s==EFI_NOT_FOUND || s==EFI_UNSUPPORTED) && !other))return EFI_UNSUPPORTED;
      s=Interface(bs,Full.Children[c],&TextGuid,&other);
      if(!((s==EFI_NOT_FOUND || s==EFI_UNSUPPORTED) && !other))return EFI_UNSUPPORTED;
    }
    UINTN edges=0;
    for(UINTN i=0;i<g->Count;i++) {
      const EDGE *e=&g->Edges[i];
      if(e->Handle==PreparedPlan.RtxPci && e->Agent!=Full.NvAgent)return EFI_ACCESS_DENIED;
      if(e->Handle!=Full.Children[c])continue;
      if(!GuGuidEqual(&e->Protocol,&EdidGuid) || e->Attributes!=EFI_OPEN_PROTOCOL_BY_DRIVER || e->Controller!=Full.Children[c] || e->Count!=1)return EFI_ACCESS_DENIED;
      if(!c)Full.EdidAgent=e->Agent;else if(e->Agent!=Full.EdidAgent)return EFI_ACCESS_DENIED;
      edges++;
    }
    if(edges!=1)return EFI_ACCESS_DENIED;
  }
  return Full.EdidAgent!=Full.NvAgent && Full.EdidAgent!=Full.Image?EFI_SUCCESS:EFI_ACCESS_DENIED;
#else
  UINTN edid=0,nv=0,nvChild=0,amdEdid=0;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];
    if(e->Handle==PreparedPlan.Rtx) {
      if(!GuGuidEqual(&e->Protocol,&EdidGuid) || e->Attributes!=EFI_OPEN_PROTOCOL_BY_DRIVER || e->Controller!=PreparedPlan.Rtx || e->Count!=1)return EFI_ACCESS_DENIED;
      Full.EdidAgent=e->Agent;edid++;
    }
    if(e->Handle==PreparedPlan.RtxPci) {
      if(e->Count!=1)return EFI_ACCESS_DENIED;
      if(GuGuidEqual(&e->Protocol,&PciGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e->Controller==PreparedPlan.RtxPci){Full.NvAgent=e->Agent;nv++;}
      else if(GuGuidEqual(&e->Protocol,&PathGuid) && e->Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e->Controller==PreparedPlan.Rtx)nvChild++;
      else return EFI_ACCESS_DENIED;
    }
  }
  if(edid!=1 || nv!=1 || nvChild!=1 || Full.EdidAgent==Full.NvAgent || Full.EdidAgent==Full.Image || Full.NvAgent==Full.Image)return EFI_ACCESS_DENIED;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];
    if(e->Handle==PreparedPlan.RtxPci && e->Agent!=Full.NvAgent)return EFI_ACCESS_DENIED;
    if(e->Handle==PreparedPlan.Amd && GuGuidEqual(&e->Protocol,&EdidGuid)) {
      if(e->Agent!=Full.EdidAgent || e->Attributes!=EFI_OPEN_PROTOCOL_BY_DRIVER || e->Controller!=PreparedPlan.Amd || e->Count!=1)return EFI_ACCESS_DENIED;
      amdEdid++;
    }
  }
  return amdEdid==1?EFI_SUCCESS:EFI_ACCESS_DENIED;
#endif
}
#if defined(GU_HEADLESS_RESEARCH) && GU_CONSOLE_AGGREGATE
/* 0 keep exact, 1 may disappear (tolerated orphan cascade), 2 must be gone. */
static UINTN EdgeClass(const EDGE *e,UINTN stage) {
  if(stage>=3 && e->Handle==PreparedPlan.Rtx && e->Agent==PreparedPlan.EdidAgent)return 2;
  if(e->Agent==PreparedPlan.EdidAgent)return 1;
  if(e->Handle==PreparedPlan.Agg)return 1;
  if(stage>=4 && ((e->Handle==PreparedPlan.RtxPci && (e->Agent==PreparedPlan.NvAgent || e->Agent==PreparedPlan.NvChildAgent)) ||
     e->Handle==PreparedPlan.Rtx || (e->Agent==PreparedPlan.ComboAgent && e->Handle!=PreparedPlan.RtxPci)))return 2;
  return 0;
}
#endif
static BOOLEAN FullExpected(const GRAPH *g,UINTN stage) {
#if defined(GU_HEADLESS_RESEARCH) && GU_CONSOLE_AGGREGATE
  UINTN wanted=0,own=0;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];BOOLEAN matched=FALSE;
    if(Full.Owned && e->Handle==PreparedPlan.RtxPci && e->Agent==Full.Image && !e->Controller &&
       e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE && e->Count==1 && GuGuidEqual(&e->Protocol,&PciGuid)){own++;continue;}
    for(UINTN j=0;j<PreparedGraph->Count;j++)if(SameEdge(e,&PreparedGraph->Edges[j])){matched=TRUE;break;}
    if(!matched || EdgeClass(e,stage)==2)return FALSE;
  }
  for(UINTN i=0;i<PreparedGraph->Count;i++) {
    const EDGE *e=&PreparedGraph->Edges[i];UINTN cls=EdgeClass(e,stage);UINTN found=0;
    if(cls==2)continue;
    for(UINTN j=0;j<g->Count;j++)if(SameEdge(e,&g->Edges[j]))found++;
    if(cls==0 && found!=1)return FALSE;
    if(cls==1 && found>1)return FALSE;
    wanted+=found;
  }
  return g->Count==wanted+own;
#else
  UINTN wanted=0,own=0;
  for(UINTN i=0;i<g->Count;i++) {
    const EDGE *e=&g->Edges[i];
    if(Full.Owned && e->Handle==PreparedPlan.RtxPci && e->Agent==Full.Image && !e->Controller &&
       e->Attributes==EFI_OPEN_PROTOCOL_EXCLUSIVE && e->Count==1 && GuGuidEqual(&e->Protocol,&PciGuid))own++;
  }
  if(own!=(Full.Owned?1U:0U))return FALSE;
  for(UINTN i=0;i<PreparedGraph->Count;i++) {
    const EDGE *e=&PreparedGraph->Edges[i];
    if(Removed(&PreparedPlan,e,2))continue;
#ifdef GU_HEADLESS_RESEARCH
    BOOLEAN removedEdid=FALSE;
    for(UINTN c=0;c<Full.EdidDetached;c++)if(e->Handle==Full.Children[c] && e->Agent==Full.EdidAgent && GuGuidEqual(&e->Protocol,&EdidGuid))removedEdid=TRUE;
    if(removedEdid)continue;
#else
    if(stage>=3 && e->Handle==PreparedPlan.Rtx && e->Agent==Full.EdidAgent && GuGuidEqual(&e->Protocol,&EdidGuid))continue;
#endif
    if(stage>=4 && e->Handle==PreparedPlan.RtxPci && e->Agent==Full.NvAgent)continue;
#if GU_MSI_INT10_CONSOLE
    /* Stage 4 disconnects the firmware video driver, which recursively
     * stops the console text-out and GOP consumer drivers bound to the
     * display child; every edge recorded by those three agents is gone. */
    if(stage>=4 && (e->Agent==Full.NvAgent || e->Agent==PreparedPlan.OutAgent || e->Agent==PreparedPlan.GfxAgent))continue;
#endif
    wanted++;UINTN found=0;
    for(UINTN j=0;j<g->Count;j++)if(SameEdge(e,&g->Edges[j]))found++;
    if(found!=1)return FALSE;
  }
  return g->Count==wanted+own;
#endif
}
static EFI_STATUS AmdOnly(VOID) {
#ifdef GU_HEADLESS_RESEARCH
  EFI_SYSTEM_TABLE *t=Full.Table;EFI_BOOT_SERVICES *bs=t->BootServices;
#if GU_MSI_INT10_CONSOLE
  /* The INT10 console chain (ConOut/ConIn/StdErr handles and protocols) is
   * the firmware driver's own child tree: releasing the driver destroys it,
   * leaving the system-table pointers dangling. They cannot be revalidated
   * safely. The only post-release proof available here is that no GOP of
   * any kind survives; the graph and PCI state are checked separately. */
  EFI_HANDLE *handles=NULL;UINTN count=0;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(s==EFI_NOT_FOUND && !count && !handles)return EFI_SUCCESS;
  if(handles)bs->FreePool(handles);
  return EFI_UNSUPPORTED;
#else
  PLAN *p=&PreparedPlan;
  if(!t->ConIn || (UINTN)t->ConIn->Reset!=p->KeyReset || (UINTN)t->ConIn->ReadKeyStroke!=p->KeyRead || t->ConIn->WaitForKey!=p->KeyWait)return EFI_NOT_READY;
  if(t->ConsoleOutHandle!=p->Console || t->ConsoleInHandle!=p->Input || t->StandardErrorHandle!=p->Error || t->ConOut!=p->ConsoleText || t->StdErr!=p->ErrorText || t->ConIn!=p->Keys || !t->ConIn || !t->ConIn->Reset || !t->ConIn->ReadKeyStroke || !t->ConIn->WaitForKey)return EFI_NOT_READY;
  VOID *out=NULL,*in=NULL,*err=NULL;
  if(Interface(bs,p->Console,&TextGuid,&out)!=EFI_SUCCESS || Interface(bs,p->Error,&TextGuid,&err)!=EFI_SUCCESS || Interface(bs,p->Input,&InputGuid,&in)!=EFI_SUCCESS || out!=p->ConsoleText || err!=p->ErrorText || in!=p->Keys)return EFI_NOT_READY;
  EFI_HANDLE *handles=NULL;UINTN count=0;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(s==EFI_NOT_FOUND && !count && !handles)return EFI_SUCCESS;
#if GU_CONSOLE_AGGREGATE
  /* The aggregate console GOP may already be gone; zero or one pathless
     zero-FB GOP is the only tolerated survivor. */
  if(s!=EFI_SUCCESS || !handles || count>1){if(handles)bs->FreePool(handles);return EFI_UNSUPPORTED;}
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  s=Interface(bs,handles[0],&GopGuid,(VOID **)&gop);
  if(s==EFI_SUCCESS && (!gop->Mode || gop->Mode->FrameBufferBase || gop->Mode->FrameBufferSize))s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS){EFI_STATUS ps=Interface(bs,handles[0],&PathGuid,(VOID **)&path);if(!((ps==EFI_UNSUPPORTED || ps==EFI_NOT_FOUND) && !path))s=EFI_UNSUPPORTED;}
  if(s==EFI_SUCCESS) {
    EFI_HANDLE agent=NULL;
    if(handles[0]!=p->Agg){s=EFI_UNSUPPORTED;}
    else{s=AggGopAgent(bs,handles[0],&agent);}
    if(s==EFI_SUCCESS && agent && agent!=p->GfxAgent)s=EFI_UNSUPPORTED;
  }
#else
  if(s!=EFI_SUCCESS || !handles || count!=1){if(handles)bs->FreePool(handles);return EFI_UNSUPPORTED;}
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  s=Interface(bs,handles[0],&GopGuid,(VOID **)&gop);
  if(s==EFI_SUCCESS && (handles[0]!=p->Console || !gop->Mode || gop->Mode->FrameBufferBase || gop->Mode->FrameBufferSize))s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS){EFI_STATUS ps=Interface(bs,handles[0],&PathGuid,(VOID **)&path);if(!((ps==EFI_UNSUPPORTED || ps==EFI_NOT_FOUND) && !path))s=EFI_UNSUPPORTED;}
#endif
  if(bs->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  return s;
#endif
#else
  EFI_SYSTEM_TABLE *t=Full.Table;PLAN *p=&PreparedPlan;EFI_BOOT_SERVICES *bs=t->BootServices;
  if(t->ConsoleOutHandle!=p->Console || t->ConsoleInHandle!=p->Input || t->StandardErrorHandle!=p->Error ||
     t->ConOut!=p->ConsoleText || t->ConIn!=p->Keys || t->StdErr!=p->ErrorText || !t->ConIn->ReadKeyStroke || !t->ConIn->WaitForKey)return EFI_NOT_READY;
  VOID *out=NULL,*in=NULL,*err=NULL,*text=NULL;
  EFI_STATUS s=Interface(bs,p->Console,&TextGuid,&out);
  if(s==EFI_SUCCESS)s=Interface(bs,p->Error,&TextGuid,&err);
  if(s==EFI_SUCCESS)s=Interface(bs,p->Input,&InputGuid,&in);
  if(s==EFI_SUCCESS)s=Interface(bs,p->Amd,&TextGuid,&text);
  if(s!=EFI_SUCCESS || out!=p->ConsoleText || err!=p->ErrorText || in!=p->Keys || text!=p->AmdText)return EFI_NOT_READY;
  EFI_HANDLE *handles=NULL;UINTN count=0,amd=0;
  s=bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  if(s==EFI_SUCCESS && (!handles || !count || count>2))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS && i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
    s=Interface(bs,handles[i],&GopGuid,(VOID **)&gop);if(s!=EFI_SUCCESS || !gop || !gop->Mode){s=EFI_UNSUPPORTED;break;}
    EFI_STATUS ps=Interface(bs,handles[i],&PathGuid,(VOID **)&path);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m=gop->Mode;
    if((ps==EFI_UNSUPPORTED || ps==EFI_NOT_FOUND) && !path && handles[i]==p->Console && !m->FrameBufferBase && !m->FrameBufferSize)continue;
    if(ps!=EFI_SUCCESS || handles[i]!=p->Amd || !PathMatches(path,p->AmdDisplayPath) || ++amd!=1 || !m->Info || m->SizeOfInfo<sizeof(*m->Info) ||
       !m->FrameBufferBase || !m->FrameBufferSize || m->Mode>=m->MaxMode || m->Info->PixelFormat>PixelBlueGreenRedReserved8BitPerColor ||
       !m->Info->HorizontalResolution || !m->Info->VerticalResolution || m->Info->PixelsPerScanLine<m->Info->HorizontalResolution ||
       (UINT64)m->Info->PixelsPerScanLine*m->Info->VerticalResolution>m->FrameBufferSize/4){s=EFI_UNSUPPORTED;break;}
  }
  if(handles && bs->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  UINT32 cfg[16];EFI_PCI_IO_PROTOCOL *io=NULL;
  if(s==EFI_SUCCESS)s=Interface(bs,p->AmdPci,&PciGuid,(VOID **)&io);
  if(s==EFI_SUCCESS && io!=p->AmdIo)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg);
  if(s==EFI_SUCCESS && !Bytes(cfg,p->AmdConfig,sizeof(cfg)))s=EFI_NOT_READY;
  return s==EFI_SUCCESS && amd!=1?EFI_UNSUPPORTED:s;
#endif
}
static EFI_STATUS CapturePermit(VOID) {
  GU_CAPS caps;EFI_STATUS s=ReadConfigFull(Full.Config);
  if(s==EFI_SUCCESS && (!GuCheckConfig(Full.Config,0,1,0,0,&caps) || Full.Config[4]!=(UINT32)GU_BAR0_BASE || Full.Config[2]!=0x030000a1))s=EFI_UNSUPPORTED;
  s=Note("capture-pci-config256",s);if(s!=EFI_SUCCESS)return s;
  s=LogConfig("pre-nv-pci",Full.Config);if(s!=EFI_SUCCESS)return s;
  s=Note("capture-bar0",CheckBar(TRUE));if(s!=EFI_SUCCESS)return s;
  s=Note("capture-prom",Prom());if(s!=EFI_SUCCESS)return s;
  s=Note("capture-cold",Cold(Full.Cold));if(s!=EFI_SUCCESS)return s;
  EFI_PCI_IO_PROTOCOL *io=PreparedPlan.RtxIo;
  if(s==EFI_SUCCESS && !io->Attributes)s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&Full.Attributes);
  UINT64 expected=(Full.Config[1]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(Full.Config[1]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
#if GU_MSI_INT10_CONSOLE
  /* The CSM firmware entered with IO, memory and bus-master decode set.
   * The cached PciIo attributes must agree on IO and memory; bus mastering
   * may or may not be reflected in the cache, but claiming it without the
   * command-register bit set is never tolerated. */
  if(s==EFI_SUCCESS && (Full.Attributes&(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY))!=expected)s=EFI_NOT_READY;
  if(s==EFI_SUCCESS && (Full.Attributes&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) && !(Full.Config[1]&4))s=EFI_NOT_READY;
#else
  if(s==EFI_SUCCESS && (Full.Attributes&(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))!=expected)s=EFI_NOT_READY;
#endif
  if(s==EFI_SUCCESS)Full.Captured=TRUE;
  return s;
}
static EFI_STATUS RestoreDecode(VOID) {
  if(!Full.Captured || !Full.Released || Full.Owned)return EFI_ACCESS_DENIED;
  EFI_BOOT_SERVICES *bs=Full.Table->BootServices;EFI_PCI_IO_PROTOCOL *io=NULL;
  static GRAPH claimGraph;UINT32 claimConfig[64]={0};UINT64 claimMask=0;
  EFI_STATUS s=ReadConfigFull(claimConfig);
  if(s==EFI_SUCCESS)s=LogConfig("post-nv-pci",claimConfig);
  if(s!=EFI_SUCCESS)return s;
  EFI_TPL old=bs->RaiseTPL(TPL_NOTIFY);
  s=ReadConfigFull(claimConfig);
  if(s==EFI_SUCCESS && !GuDecodeRestoreMask(Full.Config,claimConfig,&claimMask))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=Snapshot(bs,&claimGraph);
  if(s==EFI_SUCCESS && !FullExpected(&claimGraph,4))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=bs->OpenProtocol(PreparedPlan.RtxPci,&PciGuid,(VOID **)&io,Full.Image,NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE);
  if(s==EFI_SUCCESS)Full.Owned=TRUE;
  bs->RestoreTPL(old);
  if(s!=EFI_SUCCESS)return s;
  if(io!=PreparedPlan.RtxIo)s=EFI_COMPROMISED_DATA;
  UINT32 cfg[64]={0};UINT64 needed=0,supported=0,attrs=0;
  if(s==EFI_SUCCESS)s=ReadConfigFull(cfg);
  if(s==EFI_SUCCESS && !GuDecodeRestoreMask(Full.Config,cfg,&needed))s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
  UINT64 expected=Full.Attributes & ~((Full.Config[1]&~cfg[1]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(Full.Config[1]&~cfg[1]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0));
#if GU_MSI_INT10_CONSOLE
  /* The CSM firmware entered with bus mastering set, and the firmware video
   * driver's stop may clear the command register directly, leaving the PciIo
   * attribute cache set, or revert it through PciIo, clearing the cache.
   * Either way only IO and memory decode are restored here and the unlock
   * transaction owns bus mastering afterwards, so compare the cache with the
   * three decode attributes excluded on both sides. */
  const UINT64 decodeTolerance=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER|EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY|EFI_PCI_IO_ATTRIBUTE_VGA_IO;
  Note("decode-attrs-get-status",s);
  Note("decode-attrs-observed",(EFI_STATUS)attrs);Note("decode-attrs-expected",(EFI_STATUS)expected);
  if(s==EFI_SUCCESS && (attrs&~decodeTolerance)!=(expected&~decodeTolerance))s=EFI_NOT_READY;
#else
  if(s==EFI_SUCCESS && attrs!=expected)s=EFI_NOT_READY;
#endif
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationSupported,0,&supported);
#if GU_MSI_INT10_CONSOLE
  Note("decode-supported-status",s);
  Note("decode-supported-needed",(EFI_STATUS)needed);
#endif
  if(s==EFI_SUCCESS && (supported&needed)!=needed)s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS)s=Note("pci-decode-restore-intent-io-mem-only",EFI_SUCCESS);
  /* Durable logging can run firmware callbacks: recheck the exact target,
   * delta and cache under the held exclusive claim before the only write. */
  old=bs->RaiseTPL(TPL_NOTIFY);
  if(s==EFI_SUCCESS)s=AmdOnly();
  if(s==EFI_SUCCESS)s=ReadConfigFull(cfg);
  UINT64 rechecked=0;
  if(s==EFI_SUCCESS && (!GuDecodeRestoreMask(Full.Config,cfg,&rechecked) || rechecked!=needed))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
#if GU_MSI_INT10_CONSOLE
  if(s==EFI_SUCCESS && (attrs&~decodeTolerance)!=(expected&~decodeTolerance))s=EFI_NOT_READY;
#else
  if(s==EFI_SUCCESS && attrs!=expected)s=EFI_NOT_READY;
#endif
  if(s==EFI_SUCCESS)s=Snapshot(bs,&claimGraph);
  if(s==EFI_SUCCESS && !FullExpected(&claimGraph,4))s=EFI_NOT_READY;
#if GU_MSI_INT10_CONSOLE
  /* If the attribute cache still claims the decode bits while the command
   * register lost them, a plain Enable may be optimized away; cycle them so
   * the bus driver reprograms the command register from the cache. */
  if(s==EFI_SUCCESS && needed && (attrs&needed)==needed) {
    s=io->Attributes(io,EfiPciIoAttributeOperationDisable,needed,NULL);
    if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,needed,NULL);
  } else
#endif
  if(s==EFI_SUCCESS && needed)s=io->Attributes(io,EfiPciIoAttributeOperationEnable,needed,NULL);
  bs->RestoreTPL(old);
  /* An API error/partial effect is never promoted by a matching readback. */
  if(s==EFI_SUCCESS)s=ReadConfigFull(cfg);
#if GU_MSI_INT10_CONSOLE
  if(s==EFI_SUCCESS && (Full.Config[1]&4))cfg[1]|=4;
#endif
  if(s==EFI_SUCCESS && !Bytes(cfg,Full.Config,sizeof(cfg)))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=io->Attributes(io,EfiPciIoAttributeOperationGet,0,&attrs);
#if GU_MSI_INT10_CONSOLE
  if(s==EFI_SUCCESS && (attrs&~decodeTolerance)!=(Full.Attributes&~decodeTolerance))s=EFI_NOT_READY;
#else
  if(s==EFI_SUCCESS && attrs!=Full.Attributes)s=EFI_NOT_READY;
#endif
  if(s==EFI_SUCCESS)s=CheckBar(FALSE);
  if(s==EFI_SUCCESS)s=Prom();
  UINT32 cold[9];
  if(s==EFI_SUCCESS)s=Cold(cold);
  if(s==EFI_SUCCESS && !Bytes(cold,Full.Cold,sizeof(cold)))s=EFI_NOT_READY;
  EFI_STATUS closed=bs->CloseProtocol(PreparedPlan.RtxPci,&PciGuid,Full.Image,NULL);
  if(closed==EFI_SUCCESS)Full.Owned=FALSE;
  if(s==EFI_SUCCESS)s=closed;
  if(s==EFI_SUCCESS)Full.Restored=TRUE;
  return Note("pci-decode-restore-result",s);
}
EFI_STATUS GuReleaseGop(EFI_SYSTEM_TABLE *t,EFI_HANDLE image,GU_PREPARE_LOG log,VOID *context) {
  Full=(FULL){0};Full.Table=t;Full.Image=image;Full.Log=log;Full.Context=context;
  if(!t || !t->BootServices || !image || !log)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *bs=t->BootServices;static GRAPH g;
  EFI_STATUS s=GuVerifyPreparedConsole(t);
#ifdef GU_HEADLESS_RESEARCH
  s=Note("headless-console-pre-release-verification",s);
#endif
  if(s==EFI_SUCCESS)s=Snapshot(bs,&g);
  if(s==EFI_SUCCESS)s=FullConsumers(&g);
#ifdef GU_HEADLESS_RESEARCH
  s=Note("headless-display-child-consumers-preflight",s);
#endif
#if GU_EDID_AGENT_PRESENT
  if(s==EFI_SUCCESS)s=Note("ami-runtime-code-profile",Profile(Full.EdidAgent,TRUE));
#endif
  if(s==EFI_SUCCESS)s=Note("nvidia-code-profile",Profile(Full.NvAgent,FALSE));
  if(s==EFI_SUCCESS)s=CapturePermit();
  s=Note("full-release-preflight",s);if(s!=EFI_SUCCESS)return s;
#if GU_MSI_INT10_CONSOLE
  (void)Note("entry-bar-probe-return",GuProbeBars(log,context,TRUE));
  (void)GuCaptureExt(log,context);
#endif
  for(UINTN stage=3;stage<=4;stage++) {
#if !GU_EDID_AGENT_PRESENT
    /* No EDID/mouse consumer agent exists on this firmware: the display
     * child is released recursively by the stage-4 driver disconnect. */
    if(stage==3)continue;
#endif
#ifdef GU_HEADLESS_RESEARCH
    UINTN repeats=stage==3?Full.ChildCount:1;
    for(UINTN child=0;child<repeats;child++) {
#endif
    s=Note(stage==3?"detach-rtx-edid-intent-shared-pointer-pauses":"detach-nvidia-gop-intent",EFI_SUCCESS);
    if(s==EFI_SUCCESS)s=Alive(t,&PreparedPlan);
    UINT32 cfg[64],cold[9];
    if(s==EFI_SUCCESS)s=ReadConfigFull(cfg);
    if(s==EFI_SUCCESS && !Bytes(cfg,Full.Config,sizeof(cfg)))s=EFI_NOT_READY;
    if(s==EFI_SUCCESS)s=Cold(cold);
    if(s==EFI_SUCCESS && !Bytes(cold,Full.Cold,sizeof(cold)))s=EFI_NOT_READY;
    EFI_TPL old=bs->RaiseTPL(TPL_NOTIFY);
    if(s==EFI_SUCCESS)s=Profile(stage==3?Full.EdidAgent:Full.NvAgent,stage==3);
    if(s==EFI_SUCCESS)s=Snapshot(bs,&g);
    if(s==EFI_SUCCESS && !FullExpected(&g,stage-1))s=EFI_NOT_READY;
#ifdef GU_HEADLESS_RESEARCH
    for(UINTN c=0;s==EFI_SUCCESS && c<Full.ChildCount;c++) {
      EFI_DEVICE_PATH_PROTOCOL *path=NULL;
      s=Interface(bs,Full.Children[c],&PathGuid,(VOID **)&path);
      if(s==EFI_SUCCESS && !PathMatches(path,Full.ChildPaths[c]))s=EFI_NOT_READY;
    }
    if(s==EFI_SUCCESS)s=Alive(t,&PreparedPlan);
#endif
    bs->RestoreTPL(old);
    if(s!=EFI_SUCCESS)return Note("full-release-recheck-failed",s);
#ifdef GU_HEADLESS_RESEARCH
    s=bs->DisconnectController(stage==3?Full.Children[child]:PreparedPlan.RtxPci,stage==3?Full.EdidAgent:Full.NvAgent,NULL);
    if(s==EFI_SUCCESS && stage==3)Full.EdidDetached++;
#else
    s=bs->DisconnectController(stage==3?PreparedPlan.Rtx:PreparedPlan.RtxPci,stage==3?Full.EdidAgent:Full.NvAgent,NULL);
#endif
    s=Note(stage==3?"detach-edid-return":"detach-nvidia-return",s);if(s!=EFI_SUCCESS)return s;
    if(stage==3)s=Alive(t,&PreparedPlan);
    else {Full.Released=TRUE;s=AmdOnly();}
    if(s==EFI_SUCCESS)s=Snapshot(bs,&g);
    if(s==EFI_SUCCESS && !FullExpected(&g,stage))s=EFI_NOT_READY;
    s=Note(stage==3?"edid-detach-verified":"nvidia-detach-verified",s);if(s!=EFI_SUCCESS)return s;
#ifdef GU_HEADLESS_RESEARCH
    }
#endif
  }
  s=RestoreDecode();if(s!=EFI_SUCCESS)return s;
  s=GuVerifyReleased(t,TRUE);
  return Note("full-release-ready-for-original-unlock",s);
}
static GU_VERIFY_DIAGNOSTIC VerifyDiagnostic;
const GU_VERIFY_DIAGNOSTIC *GuLastVerifyDiagnostic(VOID) {return &VerifyDiagnostic;}
/* Exact-card PCIe capability at 0x78: Device Control at 0x80, Status at
 * 0x82. Only the two observed pre-existing CED/URD status bits may clear
 * after the original unlock/FLR. Never ignore new errors, control changes,
 * other status bits, or any change before unlock. No register writes. */
static BOOLEAN VerifyConfigWord(UINTN word,UINT32 expected,UINT32 observed,BOOLEAN checkCold) {
  if(expected==observed)return TRUE;
#if GU_MSI_INT10_CONSOLE
  /* The CSM firmware entered with bus mastering set; releasing the firmware
   * driver chain leaves it cleared and the unlock transaction owns it from
   * there, so the command register may legitimately differ from the entry
   * snapshot by exactly that bit. */
  if(word==1 && (Full.Config[1]&4) && (expected|4)==(observed|4))return TRUE;
#endif
  const UINT32 clearable=0x00090000;
  if(checkCold || word!=0x80/4 || Full.Config[0x78/4]!=0x0012b410)return FALSE;
  return (expected & ~clearable)==(observed & ~clearable) &&
    !(observed & clearable & ~expected);
}
/* No logging/console callbacks during verification. The caller reports only
 * after failure, or performs the original final fresh check after closing IO. */
#ifdef GU_HEADLESS_RESEARCH
static EFI_STATUS VerifyReleasedInner(EFI_SYSTEM_TABLE *t,BOOLEAN checkCold) {
#else
EFI_STATUS GuVerifyReleased(EFI_SYSTEM_TABLE *t,BOOLEAN checkCold) {
#endif
  VerifyDiagnostic=(GU_VERIFY_DIAGNOSTIC){.Stage="verify-state",.Status=EFI_NOT_READY};
  if(!t || t!=Full.Table || !Full.Captured || !Full.Released || !Full.Restored || Full.Owned)return EFI_NOT_READY;
  static GRAPH g;UINT32 cfg[64],cold[9];
  EFI_STATUS s;
#define VERIFY_STEP(label,expr) do {VerifyDiagnostic.Stage=(label);s=(expr);VerifyDiagnostic.Status=s;if(s!=EFI_SUCCESS)return s;} while(0)
#ifdef GU_HEADLESS_RESEARCH
  VERIFY_STEP("verify-zero-physical-gop-input",AmdOnly());
#else
  VERIFY_STEP("verify-amd-display-input",AmdOnly());
#endif
  VERIFY_STEP("verify-pci-read",ReadConfigFull(cfg));
  for(UINTN i=0;i<64;i++)if(!VerifyConfigWord(i,Full.Config[i],cfg[i],checkCold)) {
    VerifyDiagnostic=(GU_VERIFY_DIAGNOSTIC){"verify-pci-config-diff",EFI_NOT_READY,TRUE,(UINT32)i*4,Full.Config[i],cfg[i]};
    return EFI_NOT_READY;
  }
  VERIFY_STEP("verify-bar",CheckBar(FALSE));
  UINT64 attrs=0;
  VERIFY_STEP("verify-pci-attributes-read",PreparedPlan.RtxIo->Attributes(PreparedPlan.RtxIo,EfiPciIoAttributeOperationGet,0,&attrs));
#if GU_MSI_INT10_CONSOLE
  /* The firmware driver's stop reverted the whole attribute cache; the
   * restore re-enables IO and memory only, so compare with the reverted
   * decode and legacy-VGA attributes excluded on both sides. */
#define GU_MSI_ATTR_TOLERANCE (EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER|EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY|EFI_PCI_IO_ATTRIBUTE_VGA_IO)
  VERIFY_STEP("verify-pci-attributes-match",(attrs&~(UINT64)GU_MSI_ATTR_TOLERANCE)==(Full.Attributes&~(UINT64)GU_MSI_ATTR_TOLERANCE)?EFI_SUCCESS:EFI_NOT_READY);
#else
  VERIFY_STEP("verify-pci-attributes-match",attrs==Full.Attributes?EFI_SUCCESS:EFI_NOT_READY);
#endif
  if(checkCold) {
    VERIFY_STEP("verify-cold-read",Cold(cold));
    VERIFY_STEP("verify-cold-match",Bytes(cold,Full.Cold,sizeof(cold))?EFI_SUCCESS:EFI_NOT_READY);
  }
  VERIFY_STEP("verify-driver-graph-read",Snapshot(t->BootServices,&g));
  VERIFY_STEP("verify-driver-graph-match",FullExpected(&g,4)?EFI_SUCCESS:EFI_NOT_READY);
  VerifyDiagnostic.Stage="verify-complete";
  return EFI_SUCCESS;
#undef VERIFY_STEP
}
#ifdef GU_HEADLESS_RESEARCH
EFI_STATUS GuVerifyReleased(EFI_SYSTEM_TABLE *t,BOOLEAN checkCold) {
  if(!t || !t->BootServices || !t->BootServices->RaiseTPL || !t->BootServices->RestoreTPL)return EFI_INVALID_PARAMETER;
  EFI_TPL old=t->BootServices->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS s=VerifyReleasedInner(t,checkCold);
  t->BootServices->RestoreTPL(old);return s;
}
#endif

EFI_STATUS GuProbeBars(GU_PREPARE_LOG Log,VOID *Context,BOOLEAN atEntry) {
  /* Diagnostic: probe each decode BAR the way the Windows PCI driver does.
   * The unlocked card may report different size masks than the entry
   * snapshot, which is what makes Windows refuse resource assignment. */
  if(!atEntry && (!Full.Restored || !PreparedPlan.RtxIo))return EFI_NOT_READY;
  if(!PreparedPlan.RtxIo)return EFI_NOT_READY;
  static const struct {UINTN off;const char *mask;const char *orig;
                       const char *emask;const char *eorig;} probes[4]=
    {{0x10,"probe-bar0-mask","probe-bar0-orig","entry-probe-bar0-mask","entry-probe-bar0-orig"},
     {0x14,"probe-bar1-mask","probe-bar1-orig","entry-probe-bar1-mask","entry-probe-bar1-orig"},
     {0x18,"probe-bar1hi-mask","probe-bar1hi-orig","entry-probe-bar1hi-mask","entry-probe-bar1hi-orig"},
     {0x30,"probe-rom-mask","probe-rom-orig","entry-probe-rom-mask","entry-probe-rom-orig"}};
  EFI_PCI_IO_PROTOCOL *io=PreparedPlan.RtxIo;
  for(UINTN b=0;b<4;b++) {
    const char *maskLabel=atEntry?probes[b].emask:probes[b].mask;
    const char *origLabel=atEntry?probes[b].eorig:probes[b].orig;
    UINT32 orig=0,all=0xFFFFFFFFu,mask=0xFFFFFFFFu;
    EFI_STATUS s=io->Pci.Read(io,EfiPciIoWidthUint32,(UINT32)probes[b].off,1,&orig);
    if(s==EFI_SUCCESS)s=io->Pci.Write(io,EfiPciIoWidthUint32,(UINT32)probes[b].off,1,&all);
    if(s==EFI_SUCCESS) {
      EFI_STATUS r=io->Pci.Read(io,EfiPciIoWidthUint32,(UINT32)probes[b].off,1,&mask);
      EFI_STATUS w=io->Pci.Write(io,EfiPciIoWidthUint32,(UINT32)probes[b].off,1,&orig);
      if(r!=EFI_SUCCESS)s=r;else if(w!=EFI_SUCCESS)s=w;
    }
    if(s!=EFI_SUCCESS)return Log(Context,maskLabel,s);
    EFI_STATUS q=Log(Context,maskLabel,(EFI_STATUS)(UINTN)mask);if(q!=EFI_SUCCESS)return q;
    q=Log(Context,origLabel,(EFI_STATUS)(UINTN)orig);if(q!=EFI_SUCCESS)return q;
  }
  return EFI_SUCCESS;
}

static UINT32 ExtEntry[960];
static BOOLEAN ExtCaptured=FALSE;
static volatile UINT32 *EcamCard;

static EFI_STATUS EcamInit(VOID) {
  /* This firmware's PciBusDxe refuses PciIo config reads beyond 256 bytes,
   * so map the card through the memory-mapped ECAM region. Probe the common
   * MCFG bases by matching the card's vendor/device dword at bus 1 dev 0. */
  static const UINT64 cand[]={0xE0000000,0xE1000000,0xE2000000,0xC0000000,0xB0000000,0xA8000000};
  if(EcamCard)return EFI_SUCCESS;
  for(UINTN i=0;i<sizeof(cand)/sizeof(cand[0]);i++) {
    volatile UINT32 *p=(volatile UINT32 *)(UINTN)(cand[i]+0x100000);
    if(p[0]==GU_BOARD_ID) {EcamCard=p;return EFI_SUCCESS;}
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS GuCaptureExt(GU_PREPARE_LOG Log,VOID *Context) {
  /* Snapshot the full 4 KiB extended config space (offsets 0x100-0xFFF) so
   * the post-unlock pass can report exactly which extended-capability
   * dwords the GSP transaction changed. Windows parses these for VF BARs. */
  EFI_STATUS s=EcamInit();
  if(s==EFI_SUCCESS)for(UINTN i=0;i<960;i++)ExtEntry[i]=EcamCard[(0x100/4)+i];
  if(s==EFI_SUCCESS)ExtCaptured=TRUE;
  return Log(Context,"ext-capture-return",s);
}

EFI_STATUS GuDiffExt(GU_PREPARE_LOG Log,VOID *Context) {
  if(!ExtCaptured)return Log(Context,"ext-diff-skipped",EFI_NOT_READY);
  static UINT32 now[960];
  for(UINTN i=0;i<960;i++)now[i]=EcamCard[(0x100/4)+i];
  UINTN shown=0;
  for(UINTN i=0;i<960 && shown<32;i++) if(now[i]!=ExtEntry[i]) {
    EFI_STATUS q=Log(Context,"ext-diff-offset",(EFI_STATUS)(UINTN)(0x100+i*4));if(q!=EFI_SUCCESS)return q;
    q=Log(Context,"ext-diff-entry",ExtEntry[i]);if(q!=EFI_SUCCESS)return q;
    q=Log(Context,"ext-diff-now",now[i]);if(q!=EFI_SUCCESS)return q;
    shown++;
  }
  /* Walk the extended capability list. The GSP transaction leaves SR-IOV
   * VF Enable set (vGPU mode); Windows then tries to fit the virtual
   * function BAR apertures into the root port window, fails, and reports
   * the code 12 resource conflict. Clear the bit so Windows enumerates a
   * plain single-function device. */
  UINTN next=0x100,steps=0;BOOLEAN sriov=FALSE;
  while(next>=0x100 && next<0xf00 && steps++<64) {
    UINT32 hdr=EcamCard[next/4];
    UINTN id=hdr&0xffffu;
    EFI_STATUS q=Log(Context,"ext-cap-offset",(EFI_STATUS)(UINTN)next);if(q!=EFI_SUCCESS)return q;
    q=Log(Context,"ext-cap-id",(EFI_STATUS)(UINTN)id);if(q!=EFI_SUCCESS)return q;
    if(id==0x0010) {
      sriov=TRUE;
      UINT32 ctrl=EcamCard[(next+8)/4];
      q=Log(Context,"sriov-control",(EFI_STATUS)(UINTN)ctrl);if(q!=EFI_SUCCESS)return q;
      q=Log(Context,"sriov-vfbar0",(EFI_STATUS)(UINTN)EcamCard[(next+0x24)/4]);if(q!=EFI_SUCCESS)return q;
      q=Log(Context,"sriov-vfbar1-lo",(EFI_STATUS)(UINTN)EcamCard[(next+0x2c)/4]);if(q!=EFI_SUCCESS)return q;
      q=Log(Context,"sriov-vfbar1-hi",(EFI_STATUS)(UINTN)EcamCard[(next+0x30)/4]);if(q!=EFI_SUCCESS)return q;
      if(ctrl&1u) {
        EcamCard[(next+8)/4]=ctrl&~1u;
        q=Log(Context,"sriov-vf-disabled",(EFI_STATUS)(UINTN)EcamCard[(next+8)/4]);if(q!=EFI_SUCCESS)return q;
      }
    }
    next=(hdr>>20)&0xfffu;
  }
  if(!sriov)return Log(Context,"sriov-cap-absent",EFI_SUCCESS);
  return Log(Context,"ext-diff-count",(EFI_STATUS)(UINTN)shown);
}

EFI_STATUS GuSilenceDevice(VOID) {
  /* Last-resort handoff: unconfigure the device exactly as a cold POST would
   * leave it (command register cleared, BARs and ROM zeroed) so the Windows
   * PCI driver performs a completely fresh resource assignment instead of
   * honoring the addresses UEFI programmed before the unlock transaction. */
  if(!EcamCard && EcamInit()!=EFI_SUCCESS)return EFI_NOT_FOUND;
  if(!EcamCard)return EFI_NOT_FOUND;
  EcamCard[0x0c]=0;                 /* ROM base address */
  EcamCard[6]=0;EcamCard[5]=0;EcamCard[4]=0; /* BAR1hi, BAR1, BAR0 */
  EcamCard[1]=0;                    /* command: IO, MEM, BME all off */
  return EcamCard[1]==0?EFI_SUCCESS:EFI_NOT_READY;
}
