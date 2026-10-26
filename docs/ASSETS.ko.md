# 실행 펌웨어 준비

[English](ASSETS.en.md) | **한국어**

이 공개본에는 NVIDIA 펌웨어, VBIOS/PROM 덤프, 드라이버 DLL을 넣지 않습니다. 필요한 파일의 정확한 크기·SHA-256은 [unlock/assets.json](../unlock/assets.json)에 있습니다.

상위 코어의 [고정 커밋 firmware 안내](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956/firmware)를 따라 합법적으로 이용할 수 있는 원본에서 추출·생성합니다. 이번 사례의 펌웨어 계열은 **GA102 / 610.43.03**입니다. 이것은 Windows에 설치할 드라이버 버전 지시가 아닙니다. 실제 Windows 드라이버는 617.14로 성공했습니다.

## 상위 도구로 생성

WSL/Linux에서 Git, make, tar, **Python 3.14 이상**을 준비합니다. Python 3.14는 `.tar.zst` 입력 처리 때문에 상위 도구가 요구하는 버전입니다. Windows의 UEFI 빌드/패키징 Python 요구 사항과는 다릅니다.

새 작업 디렉터리에서 실행합니다. 아래 명령은 원본을 내려받아 자산을 생성하며 Linux GPU 모듈을 설치하거나 로드하지 않습니다. `sudo`, `make install-*`, DKMS 설치는 이 Windows 절차에 필요하지 않습니다.

```sh
git clone https://github.com/Zagitta/gpu-unlock.git gpu-unlock-firmware-source
cd gpu-unlock-firmware-source
git checkout --detach 0ebdb1c6308b6a2072be5bf04513472942541956
make inputs firmware PYTHON=python3.14 INPUTS="$PWD/inputs" ASSETS="$PWD/assets"
```

상위 `fetch_artifacts.py`는 고정 NVIDIA 소스와 nvidia-utils 입력을 SHA-256으로 검증하고, 생성 스크립트가 출력 해시도 검사합니다. 생성한 `assets/`의 아래 5개 `.bin`을 Windows GPU UNLOCK의 로컬 `assets/`로 복사합니다. 상위에서 생성하는 `NVIDIA-LICENSE`와 [firmware/NOTICE.md](https://github.com/Zagitta/gpu-unlock/blob/0ebdb1c6308b6a2072be5bf04513472942541956/firmware/NOTICE.md)도 확인·보관하세요. 생성 과정을 이 공개 준비 세션에서 새로 실행하지는 않았으며, 기존 검증 자산과 패키징 도구로 5개 파일의 크기·해시 일치를 확인했습니다.

로컬 `assets/` 디렉터리에 다음 5개를 준비합니다.

```text
booter_load_ga102.bin
v67-ga102-610.43.03.bin
gsp_ga10x.bin
gsp_boot_ga102.bin
gsp_boot_desc_ga102.bin
```

버전이 비슷해도 대체하지 않습니다. 패키징 도구와 UEFI 앱은 해시가 다른 파일을 거부합니다. 원본 취득과 생성 방법·라이선스를 확인할 수 없다면 실행 준비가 완료된 것이 아닙니다.
