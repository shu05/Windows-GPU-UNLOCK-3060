/* GPL-2.0-only. Two-child exact-profile headless release host replay. */
#include "test_full.c"
int main(int argc,char **argv) {
  Check(argc==5,"two PE code fixtures, PROM and full PCI fixture required");
  LoadMapped(argv[1],MouseMemory,sizeof(MouseMemory));LoadMapped(argv[2],NvMemory,sizeof(NvMemory));
  FILE *f=fopen(argv[3],"rb");Check(f && fread(PromMemory,1,sizeof(PromMemory),f)==sizeof(PromMemory),"read pinned PROM");fclose(f);
  f=fopen(argv[4],"rb");Check(f && fread(InitialFullConfig,1,sizeof(InitialFullConfig),f)==sizeof(InitialFullConfig),"read exact full config");fclose(f);
  DecodeTests();
  Check(Run(31)!=EFI_SUCCESS && !Calls,"missing Reset rejected before any device detach");
  Check(RunFull(0)==EFI_SUCCESS && Calls==5 && Full.EdidDetached==2 && EnableCalls==1 && ClaimCalls==1 && CloseCalls==1,"two EDID + NV stop and bounded IO/MEM-only restore");
  Check(GuVerifyReleased(LastTable,TRUE)==EFI_SUCCESS,"zero physical GOP and protected graph verified");
  Check(Live.Count+8==sizeof(FixtureEdges)/sizeof(FixtureEdges[0]),"three console edges, two EDID edges, three NV edges removed only");
  for(int scenario=1;scenario<=20;scenario++) {
    Check(RunFull(scenario)!=EFI_SUCCESS,"partial failure/code/config/cold/graph/owner/log fault stops");
    if(scenario==8 || scenario==9 || scenario==10 || scenario==11 || scenario==12 || scenario==13)Check(EnableCalls==1,"failure after effect remains failure");
    else Check(!EnableCalls,"pre-write fault cannot enable decode");
    if(scenario==18)Check(!ClaimCalls,"new owner blocks exclusive auto-disconnect");
  }
  for(int scenario=23;scenario<=30;scenario++) {
    Check(RunFull(scenario)!=EFI_SUCCESS && !EnableCalls && !ClaimCalls,"extra child/consumer/path/code/missing EDID/warning/partial NV stop blocks decode and unlock");
    if(scenario==25 || scenario==26)Check(Calls==4,"late second child/code mutation blocks recursive NV stop");
    if(scenario==30)Check(Calls==5 && !Full.Released,"NV partial child cleanup error cannot be promoted to release success");
  }
  Check(RunFull(32)!=EFI_SUCCESS && Calls==3 && !EnableCalls && !ClaimCalls,"last cold-read GOP info mutation blocks next detach");
  Check(RunFull(0)==EFI_SUCCESS,"new release session");
  Phase=3;Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"reappeared physical GOP blocks unlock/Windows");Phase=4;
  Check(RunFull(0)==EFI_SUCCESS,"new session for late input change");
  Keys.WaitForKey=NULL;Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"late input protocol drift blocks handoff");
  Check(RunFull(0)==EFI_SUCCESS,"new session for post-unlock status policy");
  MockConfig[32]=0x00002930;Check(GuVerifyReleased(LastTable,FALSE)==EFI_SUCCESS,"only observed old status clear permitted after unlock");
  MockConfig[1]|=4;Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"BME remaining set blocks handoff");
  printf("PASS headless full-release checks: %d; real graph/code/PROM fixtures, no GPU access\n",Checks);
  return 0;
}
