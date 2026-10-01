# 블룸 7×7 분리 필터 비교 — 미채택 (2026-10-01)

## 판단

49번 샘플링을 가로7번+세로7번으로 줄인 후보는 현재 GPU/해상도에서 오히려 느렸다. 낮 지형·하늘 위주·밤 세 장면의 블룸 평균은 각각20.6/19.3/22.6% 증가했고, 장면별3쌍 총9쌍 모두 증가했다. 후보를 채택하지 않고 기존49샘플 구현으로 복원했다. 기존 공개 컬럼/LOD coverage 재사용과 앞선 지형 CPU 최적화는 유지한다.

## 후보 구현

기존 weight=[1,6,15,20,15,6,1]의 외적으로 구성된7×7 커널을 가로·세로로 분리했다. 각 축에서64로 나누며 두 축의 정규화 곱4096은 원래와 같다. 원본 mip 생성/거리별 추가 배율/전체 HDR 블룸/하늘 포함/밝기 임계값 없음/톤 매핑 혼합은 유지했다.

각 사용 스케일1..7에 기존 출력과 같은 크기의 RGBA16F 중간 이미지를 frame slot마다 추가했다. 가로 결과를 기록하고 shader-read barrier 뒤 세로 필터를 기존 출력에 기록한다. descriptor pool 용량을 늘리고 생성/해제 경로를 연결했다. 두 slot 기준1280×900에서 추가 이미지의 이론적 texel 저장량은1,534,160바이트(약1.46MiB, 실제 GPU 할당 정렬 제외)이고 매 프레임7개 렌더 패스가 늘어난다. 중간16비트 부동소수점 저장 때문에 수학적으로 같은 커널이어도 반올림 오차가 생길 수 있다.

샘플 수 감소가 패스/중간 이미지 저장/동기화 비용을 상쇄하지 못한 것으로 해석한다. 각 비용을 별도 GPU 타이머로 분리하지 않았으므로 원인별 시간을 단정하지 않는다. 높은 해상도나 다른 GPU까지 느리다고 일반화하지 않는다.

## 재현 조건

- 기준 HEAD31f3af5. Windows/Clang Release, i7-14700F/RTX5060, headless1280×900, 거리12, LOD64, FOV75, 현재 돌 평지 seed1337. 효과 설정은 직전 비교와 같으며 성능 측정은 블룸12/TAA ON, validation OFF.
- GPU/draw 프로필로 근거리와 LOD 준비 완료 후240프레임 예열, 실행당640steady. 세 장면 전후 각3회, 총18회/11,520steady. 반복1·3은 지형→하늘→밤/before→after, 반복2는 역순/after→before. 순차 실행, 이상치 제거 없음.
- 시점 height/yaw/pitch/hour: 지형193.6/−90/−22/12, 하늘193.6/0/60/12, 밤193.6/0/30/0. 원점 컬럼0/0.
- 별도 출력 검증은 블룸 강도40(허용 최대)/TAA OFF/validation ON/각64steady로 위3장면 전후6회와 지형 블룸OFF 전후2회.
- 첫 진단의 강도100은 허용 범위 밖이라 설정 읽기가 실패하고 기본값으로 실행된 사실을 로그에서 발견했다. 그 실행은 유효한 비교에 포함하지 않고 invalid-strength-*로 보관했다. 진행 중이던 잘못된 설정 실행을 중단하고40으로 고친 뒤 유효8회를 다시 실행했다. 이후 설정 읽기 오류를 별도로 확인했다.
- before/after 실행 파일과 전체 셰이더 디렉터리를 분리해 예전 실행 파일이 새 셰이더를 읽는 혼입을 방지했다. 실제 변경된 SPIR-V는 bloom.frag.spv 하나다.
- 원본: build/release/bloom-separable-compare. before-source/candidate-source/candidate.patch, 각 before/after 런타임, 설정 사본, 로그/CSV/PNG/수동 분석 스크립트 포함. 초기 빌드 로그 build/bloom-separable-build.log.
- before EXE SHA256: 8EC722DE395971DC264D8A399D19CFD23B55C87B7407A8746E1D125137350740.
- candidate EXE SHA256: 6A70C10668D43F2BCC70DB7C1CF36987980112E2C1EBA70656E2755CE8DF5986.
- before bloom SPIR-V SHA256: 6869B7DD582DD05548192BC36B93A82E9BE63022E9643F18CA38516A3997C3AD.
- candidate bloom SPIR-V SHA256: CBD9BB509E1FAFC6770947F65AD541E617C55CE60C8D3EB4D4F3F4A99FA21C08.
- 원래 settings 사본 SHA256: 285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4. 실제 실행별 직렬화된 설정과 해시는 *-settings.json/*-run.json에 보존한다.

```text
variant/DOLBUTO.exe --headless --render-distance 12 --profile-gpu NAME-gpu.csv --profile-draws NAME-draw.csv --profile-samples 640 --profile-view 193.6 YAW PITCH HOUR --seconds 300 --capture NAME.png
```

## 결과

전18회 exit0/Vulkan errors0/warnings0/UI0/texture0, 각640steady CPU/GPU frame 일치 및 LOD pending/queued0. 장면별 전후 일반 draw/LOD tiles/triangles/columns 일치. 아래 단위ms, 각 변형1920프레임 평균 및 nearest-rank p95다.

| 장면 | 블룸 평균 전→후 | 증가 | 블룸 p95 전→후 | 전체 GPU 평균 전→후 | 전체 GPU p95 전→후 |
|---|---|---:|---|---|---|
|낮 지형|0.127427 → 0.153649|20.6%|0.128896 → 0.157344|1.659274 → 1.688055|1.826208 → 1.835168|
|하늘|0.129129 → 0.154087|19.3%|0.129568 → 0.157024|1.664263 → 1.661586|1.911104 → 1.804896|
|밤|0.127155 → 0.155831|22.6%|0.129344 → 0.157984|1.754516 → 1.781301|1.915488 → 1.978560|

블룸 전체 계측은 mip 생성·필터·전환을 함께 포함한다. 전체 GPU 평균 변화는+1.7/−0.2/+1.5%로 다른 구간의 변동도 섞여 있다. 전체 시간이 일관되게 개선됐다고 볼 수 없으며, 변경 대상인 블룸 자체는9쌍 모두 느려져 미채택했다.

출력 validation8회도 exit0/errors0/기존 미사용 셰이더 출력 warnings10/UI0/texture0, 각64steady CPU/GPU frame 및 출력량 일치. 강도40/TAA OFF에서 지형/하늘/밤 RGB 채널 평균 절대 차이는 각각0.004611/0.004244/0.007542(0..255), 최대는 모두1. 채널 p99는 모두0이며 블룸OFF는 픽셀 완전 동일하다. 태양과 구름을 포함한 하늘 전후 PNG를 직접 비교했고 눈에 띄는 번짐/형태 변화는 없었다.

성능 실행의 TAA ON PNG9쌍은 평균 절대 채널 차이 최대0.423990/255, 해당 쌍 최대27이다. 프레임/TAA 위상이 고정되지 않은 캡처이므로 이 차이를 블룸 커널만의 오차로 해석하지 않는다. 필터 변경의 시각 검증은 별도 TAA OFF 장면을 사용했다. 통계는 docs/benchmarks/bloom-separable-2026-10-01-summary.json.

## 복원 및 한계

후보 코드3개 파일(scene_effects.cpp/.hpp, bloom.frag)을 HEAD31f3af5로 복원하고 수정시간을 갱신해 실제 C++/셰이더 재컴파일을 확인했다. src/shaders 전체 git diff가 없으며 기존 헤드리스·지형 최적화는 유지한다. 복원 빌드/패키징 로그는 build/bloom-separable-restored-build.log 및 build/bloom-separable-restored-package.log.

복원본으로 지형/블룸40/TAA OFF/validation ON/64steady를 실행해 exit0/errors0/기존 warnings10/UI0/texture0을 확인했다. CPU/GPU frame 일치, near draw209/LOD tiles402/triangles570112이며 기준 PNG와 모든 픽셀이 같다. build/bin·out·restored의 EXE SHA256은 DE1E83E4950BAC32053D58E7B98AEF1A6CD8A64E9B7E5A4981E7B52ADB05D6E7로 동일하다. 세 경로의 bloom SPIR-V는 기준6869B7DD…와 동일하고 패키지 settings.json도 보존했다. 남은 Git 변경은 실험 기록뿐이다.

현재 고정 장면/해상도 검증이며 실제 창 리사이즈·게임 중 옵션 토글·다른 GPU 입력 검증은 수행하지 않았다. 자동 테스트/CTest/CU/합성 입력은 추가하지 않았다. 후보 원본은 비교 재현용으로만 보관하며 commit/push는 수행하지 않는다.
