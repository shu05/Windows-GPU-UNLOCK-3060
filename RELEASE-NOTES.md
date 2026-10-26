# Windows GPU UNLOCK — RTX 3090 Windows case study

이 문서는 **v0.1.0 USB 수동 릴리스 이력**입니다. 새 내부 부팅 소스·실험용 자동판은
[v0.2.0 안내](RELEASE-v0.2.0.md)와 [단계별 가이드](docs/INTERNAL-BOOT.ko.md)를 참고하세요.

실제 성공한 RTX3090 24GiB / AMD iGPU / ASUS B650M-PLUS WIFI 포트와 한국어 가이드입니다.

- 언락 후 재부팅 없이 **Boot Override → Windows Boot Manager**로 진입.
- Windows 드라이버 617.14 유지. 동일 CUDA12.6/cuBLAS 측정: FP16 70.55 → 125.91 TFLOPS (+78.5%), BF16 70.53 → 128.70 TFLOPS (+82.5%). 각 상태 3회 중앙값.
- 대응 공개 소스로 재빌드한 EFI가 실제 성공 USB EFI와 byte-for-byte 동일.
- host mock 10그룹, PE 음성 검사와 패키징 검사 5개 통과.

**특정 PC에 고정된 실험 코드입니다. 모든 RTX3090/30시리즈에서 그대로 작동하지 않습니다.** 지원 검사 거부를 우회하지 마세요. 장기 안정성과 ComfyUI 실제 속도는 별도 검증 대상입니다.

ZIP은 코드·문서·라이선스·성공 EFI를 포함합니다. **실행 펌웨어 5개는 포함하지 않습니다.** docs/ASSETS.ko.md의 생성 절차와 unlock/assets.json의 해시를 확인하세요. USB 준비·출력 조건·오류 대응은 docs/GUIDE.ko.md에 있습니다.

EFI SHA-256:

```text
f5cbf7ae2884d2707ee888589f725b18bbc8b0f9672a511e3467ada02af6b0f5
```

기존 Windows EFI/복구 파티션 변경과 드라이버 교체는 필요하지 않습니다. 내부 디스크 자동 부팅과 Windows 직접 chainload는 이 릴리스에 구현되지 않았습니다.
