# 블룸 단일 패스16샘플 비교 (2026-10-01)

세 장면 각3쌍 모두 블룸 GPU 평균이 감소해 채택했다. 전체 프레임 향상은 작으며 아래 결과와 한계를 함께 읽는다.

## 구현

기존7×7 커널의49개 texture 호출을 선형 샘플링16개로 묶었다. bloom.frag의 마지막 필터만 변경하며 mip 생성·HDR 입력·하늘 적용·블룸 강도·톤 매핑·해상도는 유지한다. C++ 렌더링 순서, 패스 수, descriptor, 이미지 할당은 변경하지 않는다. 앞선 가로/세로2패스 후보는 미채택 상태 그대로다.

원래 한 축의 가중치는 [1,6,15,20,15,6,1], 합64다. 중심20을10+10으로 나눠 인접 탭을 네 쌍으로 묶는다.

| 읽을 위치(텍셀 단위) | 묶음 가중치 | 복원되는 원래 탭 |
|---:|---:|---|
|−15/7|7|−3의1 + −2의6|
|−3/5|25|−1의15 + 0의10|
|+3/5|25|0의10 + +1의15|
|+15/7|7|+2의6 + +3의1|

양 축에서 이 네 위치의 외적을 사용해4×4=16회 샘플링하고 기존과 같은4096으로 나눈다. 수학적으로 기존 커널과 같지만 GPU 보간/부동소수점 정밀도 때문에 비트 동일성을 보장하지 않는다. 현재 source/output 크기가 같고 linear/clamp-to-edge sampler를 사용하는 조건에 맞춘 식이다. 작은 mip과 화면 가장자리도 같은 clamp 규칙을 사용한다.

## 비교 조건

- 기준 HEAD31f3af5 및 직전 복원본, C++ 소스는 동일하다. Windows/Clang Release, i7-14700F/RTX5060, headless1280×900, 거리12, LOD64, FOV75, 돌 평지 seed1337.
- 성능 설정은 블룸12/TAA ON, validation OFF. 근거리/LOD 준비 뒤240프레임 예열, 실행마다640steady. 낮 지형·하늘·밤 전후 각3회, 총18회/11,520steady. 반복1·3은 지형→하늘→밤/before→after, 반복2는 역순/after→before. 순차 실행, 이상치 제거 없음.
- height/yaw/pitch/hour: 지형193.6/−90/−22/12, 하늘193.6/0/60/12, 밤193.6/0/30/0. 원점 컬럼0/0.
- 별도 검증은 강도40(허용 최대)/TAA OFF/validation ON, 세 장면 전후6회와 지형 블룸OFF 전후2회. 실행당64steady.
- before/after는 실행 파일과 셰이더 디렉터리를 분리한다. 원본/설정/로그/CSV/PNG/분석: build/release/bloom-bilinear-compare. 빌드 로그 build/bloom-bilinear-build.log.
- before EXE SHA256: DE1E83E4950BAC32053D58E7B98AEF1A6CD8A64E9B7E5A4981E7B52ADB05D6E7.
- after EXE SHA256: 39D917235778249FC824C5AE2D34E390914638A330C3F666F4481567774C36B4.
- before bloom SPIR-V SHA256: 6869B7DD582DD05548192BC36B93A82E9BE63022E9643F18CA38516A3997C3AD.
- after bloom SPIR-V SHA256: 6DAB4A5CA64CE17864647D0A2021CA902F9F9DEF06DD012D5EB671F8283D964C.
- 원래 settings 사본 SHA256: 285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4. 실행별 실제 설정/해시는 *-settings.json/*-run.json에 보관한다.

```text
variant/DOLBUTO.exe --headless --render-distance 12 --profile-gpu NAME-gpu.csv --profile-draws NAME-draw.csv --profile-samples 640 --profile-view 193.6 YAW PITCH HOUR --seconds 300 --capture NAME.png
```

## 화면 검증

별도 validation8회 모두exit0/Vulkan errors0/기존 미사용 셰이더 출력 warnings10/UI0/texture0. 각64steady CPU/GPU frame·그린 청크·LOD tiles·triangles·columns가 전후 일치한다. 강도40/TAA OFF의 RGB 채널 평균 절대 차이는 지형0.001448/하늘0.003091/밤0.004361(0..255), 최대는 모두1이며 채널 p99는0이다. 블룸OFF는 픽셀 완전 동일하다. 태양과 구름을 포함한 하늘 전후 PNG를 직접 확인했고 눈에 띄는 번짐/형태 변화는 없었다.

## 성능 결과

전18회 exit0/Vulkan errors0/warnings0/UI0/texture0. 각640steady CPU/GPU frame 일치, LOD pending/queued0, 장면별 전후 일반 draw/LOD tiles/triangles/columns 일치. 각 변형1920프레임 평균이며 p95는 nearest-rank, 단위ms다.

| 장면 | 블룸 평균 전→후 | 감소 | 블룸 p95 전→후 | 전체 GPU 평균 전→후 | 전체 GPU p95 전→후 |
|---|---|---:|---|---|---|
|낮 지형|0.126990 → 0.113443|10.7%|0.129152 → 0.115968|1.667160 → 1.645744|1.866624 → 1.819904|
|하늘|0.126147 → 0.114054|9.6%|0.129216 → 0.116864|1.633726 → 1.630892|1.794464 → 1.824224|
|밤|0.126613 → 0.114016|9.9%|0.129600 → 0.116096|1.734240 → 1.728520|1.880544 → 1.935904|

블룸은 총9쌍 모두 평균이 감소했다. 절감량은 프레임당 약0.012~0.014ms다. 전체 GPU 평균은 약1.3/0.2/0.3% 감소했으나 지형·하늘의3번째 쌍에서는 전체 평균이 증가했고, 하늘·밤의 합산 전체p95도 증가했다. 전체 GPU에는 다른 구간과 OS/GPU 변동이 포함되므로 전체 프레임/FPS의 확실한 개선이나 끊김 감소를 주장하지 않는다. 변경 대상인 블룸 시간 감소가 반복되고 추가 리소스/패스가 없어 채택한다.

성능 측정의 TAA ON PNG9쌍은 평균 절대 채널 차이 최대0.398179/255, 해당 쌍 최대23이다. 프레임/TAA 위상이 같지 않은 캡처이므로 블룸만의 오차로 해석하지 않는다. 필터 출력 차이는 앞 절의 TAA OFF 별도 진단에서 확인했다. 통계는 docs/benchmarks/bloom-bilinear-2026-10-01-summary.json.

## 최종 반영

bloom.frag만 변경했고 C++/렌더 패스/이미지 리소스는 유지한다. 빌드·패키징 완료(build/bloom-bilinear-package.log). 계측 after·build/bin·out/DOLBUTO의 실행 파일과 bloom SPIR-V 해시가 각각 동일하며 다른 SPIR-V는 기준과 모두 같다. 패키지 settings.json 보존. commit/push는 수행하지 않았다.

## 범위

블룸 GPU 구간에는 mip 생성과 필터·전환이 함께 들어간다. texture 호출 감소율을 전체 GPU/FPS 향상률로 해석하지 않는다. 현재 GPU/해상도/고정 장면 결과이며 다른 GPU·해상도·미래 지형에 같은 이득/오차를 보장하지 않는다. 입력 source/output 크기나 sampler 변경 시 커널 대응을 재검토해야 한다. 자동 테스트/CTest/CU/합성 입력은 추가하지 않았다.
