/* Modified 2026-10-19/20/26 for Windows GPU UNLOCK; changes documented in CHANGES.md. */ // Reproducible ordinary Windows CUDA workload. No unlock, reset, or driver changes.
// 2026-10-26: added FP8(e4m3)/FP4(e2m1) packed-weight paths. Ampere sm_86 has no FP8/FP4
// tensor cores (Ada/Hopper/Blackwell only), so these measure the real quantized-inference
// route on this card: packed low-bit weights -> dequantize -> FP16 tensor-core GEMM with
// FP32 accumulate. A separate cublasLt heuristic probe reports whether native FP8 exists.
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cuda_bf16.h>
#include <cublas_v2.h>
#include <cublasLt.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static void cuda_check(cudaError_t e){if(e!=cudaSuccess){fprintf(stderr,"CUDA: %s\n",cudaGetErrorString(e));exit(1);}}
static void blas_check(cublasStatus_t e){if(e!=CUBLAS_STATUS_SUCCESS){fprintf(stderr,"cuBLAS error %d\n",(int)e);exit(1);}}
template<class T> __global__ void ones(T *p,size_t n){size_t i=blockIdx.x*(size_t)blockDim.x+threadIdx.x;if(i<n)p[i]=T(1.0f);}
// e4m3: sign(1) exponent(4, bias 7) mantissa(3); saturating normals, no inf, 0x7f is NaN.
// Software decode: this card has no FP8 conversion instructions.
__device__ __forceinline__ __half dequant_e4m3(unsigned char v){
  unsigned s=(v>>7)&1,e=(v>>3)&0xF,m=v&7;float f;
  if(e==0)f=(float)m/8.0f*(1.0f/64.0f);            // subnormal: 2^-6 * m/8
  else if(e==15&&m==7)f=__int_as_float(0x7fc00000);// NaN never appears in filled buffers
  else f=(1.0f+(float)m/8.0f)*exp2f((float)e-7.0f);
  return __float2half(s?-f:f);
}
// e2m1 (FP4): sign(1) exponent(2, bias 1) mantissa(1) -> {0,.5,1,1.5,2,3,4,6}.
__device__ __forceinline__ __half dequant_e2m1(unsigned char c){
  const float lut[8]={0.0f,0.5f,1.0f,1.5f,2.0f,3.0f,4.0f,6.0f};
  float f=lut[c&7];if(c&8)f=-f;return __float2half(f);
}
// One thread per 8 packed bytes; 16-byte vector stores keep the dequant pass bandwidth-bound.
__global__ void dq8(const uint2* src,uint4* dst,size_t groups){
  size_t i=blockIdx.x*(size_t)blockDim.x+threadIdx.x;if(i>=groups)return;
  uint2 v=src[i];unsigned char b[8];memcpy(b,&v,8);__half h[8];
  for(int k=0;k<8;k++)h[k]=dequant_e4m3(b[k]);
  uint4 o;memcpy(&o,h,16);dst[i]=o;
}
// FP4 packs two elements per byte, low nibble first; one thread per 8 bytes -> 16 halves.
__global__ void dq4(const uint2* src,uint4* dst,size_t groups){
  size_t i=blockIdx.x*(size_t)blockDim.x+threadIdx.x;if(i>=groups)return;
  uint2 v=src[i];unsigned char b[8];memcpy(b,&v,8);__half h[16];
  for(int k=0;k<8;k++){h[2*k]=dequant_e2m1(b[k]&0xF);h[2*k+1]=dequant_e2m1((unsigned char)(b[k]>>4));}
  uint4 o1,o2;memcpy(&o1,h,16);memcpy(&o2,h+8,16);dst[2*i]=o1;dst[2*i+1]=o2;
}
// Native FP8 tensor matmul support check: heuristic query only, never executed.
static int fp8_hw_probe(void){
  cublasLtHandle_t lt;int available=0;
  if(cublasLtCreate(&lt)!=CUBLAS_STATUS_SUCCESS)return 0;
  cublasLtMatmulDesc_t op;cublasLtMatrixLayout_t la,lb,lc;cublasLtMatmulPreference_t pref;
  cublasLtMatmulHeuristicResult_t res[4];int nres=0;size_t workspace=1u<<20;void *ws=nullptr;
  if(cublasLtMatmulDescCreate(&op,CUBLAS_COMPUTE_32F,CUDA_R_32F)==CUBLAS_STATUS_SUCCESS&&
     cublasLtMatrixLayoutCreate(&la,CUDA_R_8F_E4M3,4096,4096,4096)==CUBLAS_STATUS_SUCCESS&&
     cublasLtMatrixLayoutCreate(&lb,CUDA_R_8F_E4M3,4096,4096,4096)==CUBLAS_STATUS_SUCCESS&&
     cublasLtMatrixLayoutCreate(&lc,CUDA_R_32F,4096,4096,4096)==CUBLAS_STATUS_SUCCESS&&
     cublasLtMatmulPreferenceCreate(&pref)==CUBLAS_STATUS_SUCCESS) {
    cuda_check(cudaMalloc(&ws,workspace));
    cublasLtMatmulPreferenceSetAttribute(pref,CUBLASLT_MATMUL_PREF_MAX_WORKSPACE_BYTES,&workspace,sizeof(workspace));
    if(cublasLtMatmulAlgoGetHeuristic(lt,op,la,lb,lc,lc,pref,4,res,&nres)==CUBLAS_STATUS_SUCCESS)available=nres>0;
    cuda_check(cudaFree(ws));
  }
  cublasLtMatmulPreferenceDestroy(pref);cublasLtMatrixLayoutDestroy(la);cublasLtMatrixLayoutDestroy(lb);
  cublasLtMatrixLayoutDestroy(lc);cublasLtMatmulDescDestroy(op);cublasLtDestroy(lt);return available;
}
static void measure(cublasHandle_t handle,int kind) {
  int n=4096;size_t count=(size_t)n*n;
  // kind 3: FP8 e4m3, one byte per element (fill 0x38 = +1.0). kind 4: FP4 e2m1, two
  // elements per byte (fill 0x22 = two +1.0 nibbles). Both dequantize to FP16 buffers.
  size_t packed=kind==3?count:kind==4?count/2:count*(kind==0?sizeof(float):sizeof(__half));
  size_t input_bytes=kind>=3?packed:packed;
  size_t output_bytes=count*sizeof(float);
  void *a,*b,*c,*ha=nullptr,*hb=nullptr;
  cuda_check(cudaMalloc(&a,input_bytes));cuda_check(cudaMalloc(&b,input_bytes));cuda_check(cudaMalloc(&c,output_bytes));
  if(kind>=3){cuda_check(cudaMalloc(&ha,count*sizeof(__half)));cuda_check(cudaMalloc(&hb,count*sizeof(__half)));}
  unsigned blocks=(unsigned)((count+255)/256);
  if(kind==0){ones<<<blocks,256>>>((float *)a,count);ones<<<blocks,256>>>((float *)b,count);}
  else if(kind==1){ones<<<blocks,256>>>((__half *)a,count);ones<<<blocks,256>>>((__half *)b,count);}
  else if(kind==2){ones<<<blocks,256>>>((__nv_bfloat16 *)a,count);ones<<<blocks,256>>>((__nv_bfloat16 *)b,count);}
  else {cuda_check(cudaMemset(a,kind==3?0x38:0x22,input_bytes));cuda_check(cudaMemset(b,kind==3?0x38:0x22,input_bytes));}
  cuda_check(cudaGetLastError());cuda_check(cudaDeviceSynchronize());
  blas_check(cublasSetMathMode(handle,kind==0?CUBLAS_PEDANTIC_MATH:CUBLAS_TENSOR_OP_MATH));
  auto gemm=[&](){
    float alpha=1,beta=0;
    if(kind==0)blas_check(cublasSgemm(handle,CUBLAS_OP_N,CUBLAS_OP_N,n,n,n,&alpha,(float *)a,n,(float *)b,n,&beta,(float *)c,n));
    else if(kind<=2) {cudaDataType_t type=kind==1?CUDA_R_16F:CUDA_R_16BF;
      blas_check(cublasGemmEx(handle,CUBLAS_OP_N,CUBLAS_OP_N,n,n,n,&alpha,a,type,n,b,type,n,&beta,c,CUDA_R_32F,n,CUBLAS_COMPUTE_32F,CUBLAS_GEMM_DEFAULT_TENSOR_OP));}
    else {
      if(kind==3){unsigned g=(unsigned)(count/8);dq8<<<g,256>>>((const uint2 *)a,(uint4 *)ha,g);dq8<<<g,256>>>((const uint2 *)b,(uint4 *)hb,g);}
      else {unsigned g=(unsigned)(count/16);dq4<<<g,256>>>((const uint2*)a,(uint4*)ha,g);dq4<<<g,256>>>((const uint2*)b,(uint4*)hb,g);}
      blas_check(cublasGemmEx(handle,CUBLAS_OP_N,CUBLAS_OP_N,n,n,n,&alpha,ha,CUDA_R_16F,n,hb,CUDA_R_16F,n,&beta,c,CUDA_R_32F,n,CUBLAS_COMPUTE_32F,CUBLAS_GEMM_DEFAULT_TENSOR_OP));
    }
  };
  auto warm_start=std::chrono::steady_clock::now();
  do {gemm();cuda_check(cudaDeviceSynchronize());}while(std::chrono::duration<double>(std::chrono::steady_clock::now()-warm_start).count()<2.0);
  cudaEvent_t start,stop;cuda_check(cudaEventCreate(&start));cuda_check(cudaEventCreate(&stop));
  std::vector<float> samples;
  for(int i=0;i<30;i++) {
    cuda_check(cudaEventRecord(start));gemm();cuda_check(cudaEventRecord(stop));cuda_check(cudaEventSynchronize(stop));
    float milliseconds;cuda_check(cudaEventElapsedTime(&milliseconds,start,stop));samples.push_back(milliseconds);
  }
  // Exact mathematical reference: every element of an all-ones product equals n.
  float first,last;cuda_check(cudaMemcpy(&first,c,4,cudaMemcpyDeviceToHost));cuda_check(cudaMemcpy(&last,(float *)c+count-1,4,cudaMemcpyDeviceToHost));if(first!=n || last!=n){fprintf(stderr,"Matrix result mismatch\n");exit(1);}
  std::sort(samples.begin(),samples.end());double median=(samples[14]+samples[15])*0.5;
  static const char *names[5]={"fp32_pedantic","fp16_tensor_fp32_accumulate","bf16_tensor_fp32_accumulate",
    "fp8_e4m3_packed_dequant_fp16_tensor","fp4_e2m1_packed_dequant_fp16_tensor"};
  printf("%s{\"name\":\"%s\",\"n\":%d,\"samples\":30,\"warmup_seconds\":2,\"median_ms\":%.6f,\"min_ms\":%.6f,\"max_ms\":%.6f,\"gflops\":%.3f",
    kind?",":"",names[kind],n,median,samples.front(),samples.back(),2.0*n*n*n/(median*1e6));
  if(kind>=3)printf(",\"packed_weight_bytes_per_matrix\":%zu",packed);
  printf(",\"reference_checked\":true}");
  cuda_check(cudaEventDestroy(start));cuda_check(cudaEventDestroy(stop));
  if(ha){cuda_check(cudaFree(ha));cuda_check(cudaFree(hb));}
  cuda_check(cudaFree(a));cuda_check(cudaFree(b));cuda_check(cudaFree(c));
}
int main(int argc, char **argv) {
  if(argc==2 && !strcmp(argv[1],"--inspect-only")) {
    int runtime=0,major=0,minor=0,patch=0;
    cuda_check(cudaRuntimeGetVersion(&runtime));
    blas_check(cublasGetProperty(MAJOR_VERSION,&major));
    blas_check(cublasGetProperty(MINOR_VERSION,&minor));
    blas_check(cublasGetProperty(PATCH_LEVEL,&patch));
    printf("{\"cuda_runtime\":%d,\"cublas_major\":%d,\"cublas_minor\":%d,\"cublas_patch\":%d,\"gpu_workload_run\":false}\n",runtime,major,minor,patch);
    return 0;
  }
  if(argc!=1){fprintf(stderr,"Only --inspect-only or no arguments are supported\n");return 2;}
  int devices=0;cuda_check(cudaGetDeviceCount(&devices));int selected=-1;cudaDeviceProp properties={};
  for(int i=0;i<devices;i++) {cudaDeviceProp p;cuda_check(cudaGetDeviceProperties(&p,i));
    if(!strcmp(p.name,"NVIDIA GeForce RTX 3060") && !p.pciDomainID && p.pciBusID==1 && !p.pciDeviceID){if(selected!=-1)return 2;selected=i;properties=p;}}
  if(selected<0){fprintf(stderr,"Exact RTX 3060 at 0000:01:00.0 not found\n");return 2;}
  cuda_check(cudaSetDevice(selected));cublasHandle_t handle;blas_check(cublasCreate(&handle));
  int runtime=0,driver=0,blas=0;cuda_check(cudaRuntimeGetVersion(&runtime));cuda_check(cudaDriverGetVersion(&driver));blas_check(cublasGetVersion(handle,&blas));
  printf("{\"gpu\":\"%s\",\"pci\":\"0000:01:00.0\",\"compute_capability\":\"%d.%d\",\"cuda_runtime\":%d,\"cuda_driver_api\":%d,\"cublas\":%d,\"fp8_tensor_hw\":%d,\"benchmarks\":[",
    properties.name,properties.major,properties.minor,runtime,driver,blas,fp8_hw_probe());
  for(int kind=0;kind<5;kind++)measure(handle,kind);
  blas_check(cublasDestroy(handle));puts("]}");return 0;
}
