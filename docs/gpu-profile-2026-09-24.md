# 그래픽 GPU 계측 · 2026-09-24

AI 작업 컨텍스트. 사용자 `측정부터 해보자 실시`로 계측만 승인했다. 렌더 수식·해상도·그림자 갱신 주기·필터·SSR 알고리즘은 변경하지 않았다. 후속 최적화는 별도 합의 사항이다.

## 계측 구현

- `Renderer::enable_gpu_profile()`는 initialize 전에 명시적으로 호출한다. 일반 실행은 추가 query pool/CSV/표본 저장 없이 기존 전체/업로드 타이머만 사용한다.
- 프레임 슬롯마다 최대64 timestamp query를 따로 둔다. 시작 TOP_OF_PIPE와 각 구간 끝 ALL_COMMANDS의 차이를 기록한다. 개별 marker에서 wait/queue idle/fence를 추가하지 않는다. 기존 프레임 fence가 완료된 슬롯에서 결과를 읽고 마지막 두 프레임은 최종 idle 후 회수한다.
- 시간은 GPU timestampPeriod와 timestampValidBits를 반영한다. CPU 명령 기록 시간, present 대기, 디스플레이 scanout, OS 전체 프레임 지연이 아니다. GPU 파이프라인 중첩과 marker 자체 오버헤드가 있으므로 독립 shader 실행 비용이나 효과 OFF 절감량과 일치하지 않는다. 각 marker 사이 전환/barrier/clear/copy 비용은 해당 구간에 포함된다. 구간 합과 total은 반올림 오차 내에서 일치해야 한다.
- CSV는 frame serial/steady/실제 출력 크기/공개 컬럼 수/본 화면 청크 및 삼각형 수/stage/ms를 남긴다. 삼각형은 기존 HUD 카운터 기준이며 그림자 전체 primitive 수가 아니다. 같은 stage가 없는 프레임은 집계에서 0으로 취급한다. 원시 로딩 구간은 남기되 최종 steady 통계에서 제외한다.
- `--profile-gpu path.csv`는 월드 자동 진입, 로딩 완료 후 240프레임 예열, 기본600프레임 기록 후 종료한다. 기본 timeout120초이며 완료 전에 종료되면 CSV는 남기고 exit1로 실패한다. 마지막 프레임의 `--capture`는 타이밍 끝 뒤 게임 자체 PNG 출력이며 입력 합성이 아니다.
- `--profile-origin columnX columnZ`와 `--profile-view height yaw pitch hour`는 진단용 초기 장면이다. 프로필 실행은 마우스를 캡처하지 않으므로 플레이어/카메라/시각/물·구름 바람 시간은 고정된다. TAA jitter/history는 원래 경로로 계속 동작한다. 고정 장면이므로 이동 중 잔상/편집/시간 변화/로딩 프레임 성능을 대표하지 않는다.

## 구간의 정확한 범위

| CSV stage | 범위 |
|---|---|
| uploads | 첫 명령~월드 렌더 전 업로드 끝 |
| scene_setup | 장면 uniform/history 초기 준비 |
| shadow_all | 고체+물 그림자 깊이와 색/빛줄기 metadata |
| shadow_solid | 고체 전용 그림자 깊이 |
| opaque_terrain_player | 본 화면 고체 지형/플레이어와 패스 준비 |
| opaque_depth_copy | 지형 깊이 보존 복사 |
| scene_factor | 1×1 장면 인식 계수와 전환 |
| atmosphere | 물 이전 하늘/안개 합성; 수중이면 구름 합성 포함 |
| cloud_raymarch_underwater | 수중에서 바깥 장면용 절반 해상도 구름 |
| water_snapshot | 물 효과용 장면 색/깊이 복사와 준비 |
| water_ssr | 75% 각축 반사 버퍼의 SSR 탐색/metadata |
| water_surface | 전체 해상도 물 shader: 노멀·조명·반사용 구름·SSR 복원 및 경계 재탐색 등 |
| water_depth | 물 표면 깊이 기록 |
| cloud_raymarch | 절반 해상도 본 화면 구름 raymarch |
| volume_composite | 전체 해상도 구름 복원/필요시 재탐색 + 빛줄기 + 물 굴절 |
| underwater_composite | 수중 흡수/빛줄기/굴절 |
| bloom | bloom mip 생성과7×7필터 체인 |
| tone_map / taa / present_composite / ui | 톤 매핑 / TAA / 최종 표시 이미지 합성 / UI |

주의: `volume.frag`는 구름만 계산한다. 빛줄기는 `atmosphere.frag`의 전체 해상도 합성에서 계산한다. 이전 대화의 '구름·빛줄기가 모두 절반 해상도' 설명은 부정확했다. 초기 계측 바이너리의 `volume_clouds_shafts`, `volume_underwater` label은 집계 도구에서 각각 cloud_raymarch, cloud_raymarch_underwater로 정규화한다. label 수정은 계산 순서/경계를 바꾸지 않는다.

## 재현

1. 일반 Release 빌드 후 sandbox.exe, assets, shaders, DLL을 별도 임시 런타임에 복사한다.
2. 출력 폴더에 현재 settings.json/worldgen.json의 사본을 둔다.
3. `python tools/benchmarks/gpu_profile.py <출력폴더> --runtime <임시런타임>`을 명시적으로 실행한다. 기본5장면×3회, 매회600개 steady 프레임. 반복마다 실행 순서를 뒤집는다.
4. `--scenes coast --variants baseline no_shadows no_ssr no_clouds no_bloom --prefix ablation`은 같은 해안 장면에서 설정 사본만 바꾸는 효과 OFF 진단이다. 사용자 패키지 설정을 대상으로 실행하지 않는다.
5. runtime 없이 같은 도구를 호출하면 manifest와 CSV를 읽어 summary.json만 생성한다. 실패 실행/누락 표본/중복 marker/구간 합 불일치는 결과로 채택하지 않는다. CTest/일반 자동 회귀 테스트와 연결하지 않는다.

효과 OFF는 여러 경로를 함께 바꿀 수 있다. shadows OFF는 그림자 map/표면 shadow sampling과 종속 빛줄기를 끈다. clouds OFF는 본 화면 구름·물 반사 구름·구름 그림자를 모두 끈다. ssr OFF는 물 shader의 반사용 하늘·구름 처리까지 생략한다. 따라서 각 OFF 차이를 서로 더하거나 그 차이를 단일 raymarch 비용으로 해석하지 않는다.

## 측정 결과

### 환경과 기준 장면

- RTX3080, 드라이버591.86, Windows/Clang23.1.0 Release. 실제 출력1280×900, FOV90°, 렌더 반경24컬럼(1793공개 컬럼), VSync OFF/무제한. GPU 전원/클럭/프로세스 우선순위를 강제하지 않았다. 실행 중 관측값은1920MHz/메모리9501MHz/58°C/약313W였으나 고정 조건은 아니다. 다른 프로그램과 OS의 변동이 포함된다.
- 사용자 설정: shadow_distance512, shadow_quality2(두 맵 모두2048²), cloud_quality3/coverage100/altitude512, shaft_quality2, bloom40, TAA ON. 물 옵션 전부ON, 일반 안개OFF, 나머지 그래픽ON. 모든 진단 장면 게임 시각12:00/바람 애니메이션 위상0 고정.
- 입력 worldgen SHA256 `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`, settings SHA256 `4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1`.
- 본 측정은5장면×3실행×600정상 프레임=9,000표본. 반복 사이 실행 순서를 뒤집었다. 처음의 pilot-ocean은 집계에서 제외했다. 아래 평균/중앙값/p95는 각 장면1,800표본이며 이상값을 제거하지 않았다. p95는 nearest-rank 방식이다.

| 장면 | 컬럼X/Z | 카메라Y / yaw / pitch | GPU 평균 / 중앙값 / p95(ms) |
|---|---|---|---:|
| 해안 |7040/5504|220/-90/-22|4.531 /4.351 /5.174|
| 육지 |3456/1152|270/-90/-22|3.222 /3.104 /3.861|
| 산지·물 |8064/3456|285/-90/-22|4.602 /4.410 /5.269|
| 하늘 위주 |7040/5504|220/-90/+25|3.853 /3.694 /4.500|
| 수중 수면 방향 |7040/5504|190/-90/+15|4.246 /4.075 /4.863|

3회별 전체 GPU 평균 범위: 해안4.501~4.564, 육지3.194~3.241, 산지4.536~4.642, 하늘3.803~3.922, 수중4.182~4.322ms. 한 실행 내1,800개 독립 실험이 아니라600연속 프레임×3실행이므로 표본수를 독립 실험 수로 해석하지 않는다.

### 패스별 평균(ms)

| 구간 | 해안 | 육지 | 산지·물 | 하늘 위주 | 수중 |
|---|---:|---:|---:|---:|---:|
| 그림자 전체 재질 |1.406|1.085|1.282|1.402|1.404|
| 그림자 고체 전용 |1.090|1.083|1.163|1.084|1.087|
| 본 화면 고체/플레이어 |0.457|0.411|0.466|0.213|0.292|
| SSR 탐색 |0.173|—|0.205|0.116|0.143|
| 최종 물 표현 |0.568|0.001|0.666|0.277|0.328|
| 구름 raymarch |0.089|0.086|0.083|0.086|0.061|
| 구름·빛줄기·굴절 등 합성 |0.187|0.139|0.193|0.170|0.344|
| 블룸 |0.172|0.173|0.174|0.166|0.167|
| TAA |0.069|0.071|0.070|0.063|0.069|

표는 주요 구간만 나열해 total과 직접 합이 맞지 않는다. 전체 단계는 summary.json과 CSV에 있다. 수중의 구름은 cloud_raymarch_underwater, 합성은 underwater_composite이며 같은 GLSL 경로를 의미하지 않는다. 육지의 물0.001은 실제 물 shading이 거의 없는 marker 사이 상태 전환 비용이다.

### 해석과 다음 제안

- 그림자 두 map 생성이 장면별 GPU total의 약53~67%로 가장 크다. 본 화면 카메라를 위로 돌려도 주변 전체 그림자 대상이 크게 줄지 않으므로 하늘 위주 장면에서도 약2.49ms다. 본 화면 지형/물 shader에서 수행하는 그림자 sampling 비용은 이 두 map 시간에 포함되지 않는다.
- 해안·산지에서 최종 물 표현0.57~0.67ms가 SSR 탐색0.17~0.21ms보다 크다. 반사 버퍼 해상도/탐색만 줄이는 것보다 water.frag의 반사 구름 계산, 보간/경계 복원, surface lighting을 먼저 분석할 근거가 있다. 이번에는 shader 내부 항목을 개별 timer로 분리하지 않았으므로 그중 하나가 전부의 원인이라고 단정하지 않는다.
- 블룸의49샘플 분리 최적화는 가능한 후보지만 전체 비용이약0.17ms라 우선순위는 그림자/물보다 낮다. 블룸 전체 비용에는 mip 생성/여러 패스와 전환도 포함되어 필터 샘플49→14가0.17ms 전체의3.5배 가속을 뜻하지 않는다.
- 다음 구현 후보는 그림자 두 경로의 중복 지형 처리 감소, 보수적인 shadow caster 범위 제한, 물 반사 결과에 따른 불필요한 구름 계산 생략이다. 시간 갱신 주기/화질/거리 감소를 적용하지 않았고 이 후보들도 아직 미승인이다.
- 해상도와 장면에 따라 GPU 병목 비중이 변한다. 이번 값은1280×900/고정 정오/정지 카메라의 결과이며 실제 모니터 전체화면·야간·이동 성능이나 달성 가능한 FPS를 직접 나타내지 않는다.

### 효과 OFF 비교

같은 해안 장면에서 baseline/그림자OFF/SSR OFF/구름OFF/블룸OFF를각3회×600표본=9,000표본 추가 측정했다. 실행 순서는 정방향/역방향/정방향이며 각 표본의 그림자/수면/지형은 같은 고정 초기 장면이다. 아래 기준은 이 별도 비교 배치의 baseline4.711ms이며 앞 절의4.531ms를 빼지 않는다.

| 설정 사본의 변경 | GPU 평균 / 중앙값 / p95(ms) | 기준 대비 평균 차이 |
|---|---:|---:|
| 현재 설정 유지 |4.711 /4.697 /5.282|—|
| 그림자OFF(종속 빛줄기 포함) |1.908 /1.762 /2.561|-2.803ms (-59.5%)|
| 물 SSR OFF(반사용 하늘·구름 경로 포함) |4.059 /3.838 /4.703|-0.652ms (-13.8%)|
| 구름OFF(반사/구름 그림자 포함) |4.383 /4.175 /4.952|-0.329ms (-7.0%)|
| 블룸OFF |4.370 /4.217 /4.826|-0.342ms (-7.3%)|

변경 효과 하나의 순수 비용이나 최적화로 그대로 되찾을 수 있는 시간으로 해석하면 안 된다. 예를 들어 블룸OFF 때 그림자 시간도 baseline2.596→2.505ms로 약0.091ms 줄었다. GPU 클럭/열/전력/실행 시점 및 pipeline 상호작용을 고정하지 못했으므로0.342ms 전체를 블룸 비용으로 단정하지 않는다. 0.17ms의 직접 패스 측정이 다른 구간과의 비교에 더 적합하며, 구름OFF와 블룸OFF 간0.013ms 차이로 우선순위를 매기지 않는다.

SSR OFF의 water_surface 평균은0.586→0.190ms, 구름OFF의 water_surface는0.437ms다. 두 토글이 겹치는 경로를 제거하며 raymarch 시간만 측정한 것이 아니므로 각각의 감소량을 더하지 않는다. 그림자OFF 때 두 map 생성이 사라지는 것과 GPU total의 큰 감소는 일관된 결과다.

### 빌드·보관·확인

- 30개 정식 실행 모두 exit0, 600개 steady 프레임, 1793개 공개 컬럼, 1280×900을 확인했다. 구간 합과 total 차이는 CSV 반올림 범위 내이며 중복되거나 누락된 표본은 없다. 총18,000프레임, 351,000행의 steady CSV를 보존했다. 로그의 Vulkan errors/warnings, UI issues, texture failures는 모두0이다. Release validation=0이므로 validation layer 검증을 주장하지 않는다.
- 최초 계측 바이너리로 main15회를 실행한 후 label을 정확하게 바꾸고, 계측OFF일 때 metadata 초기화를 생략하도록 정리했다. 활성 계측의 timestamp 경계와 렌더 계산은 같다. 두 바이너리 SHA는 각 run.json에 기록했다. 최종 바이너리로 효과OFF 비교15회를 실행했다.
- 최종 Release 증분 빌드·패키징 성공. build/release/bin, 임시 runtime, out/Sandbox의 최종 sandbox.exe SHA256은 `DBC1A1CC22F03B4C2815F196186A742FCC38C6433234BEB2F4A56F20840F8C6E`로 같다. 변경 C++의 clang-format 검사도 성공했다. 계측OFF 일반 CLI120프레임 실행은 exit0이며 GPU PROFILE 로그가 없다. 이것은 입력 자동 테스트가 아닌 직접 CLI 실행 확인이다.
- 사용자 out/Sandbox/settings.json과 worldgen.json은 앞 절의 원본 해시를 유지한다. 효과OFF 파일은 별도 임시 runtime에만 썼으며 끝에 설정 사본을 원래대로 돌렸다. 그래픽 최적화나 사용자 옵션 변경은 적용하지 않았다.
- 자체 캡처 main5장면과 해안 그림자OFF/SSR OFF의 PNG를 읽어 지정한 시선과 옵션 차이를 확인했다. CU/합성 입력은 사용하지 않았다. 시간에 따른 움직임·떨림·잔상에 대한 검증은 아니다.
- `docs/benchmarks/gpu-profile-2026-09-24-summary.json`: 실행별/장면별 평균·중앙값·p95·최소·최대 및 입력 해시.
- `docs/benchmarks/gpu-profile-2026-09-24-results.zip`: steady CSV, run manifest, 실행별 설정 사본, 입력 worldgen/settings, 로그, 빌드 로그, main 첫 회 PNG, summary. ZIP SHA256 `C82B4482A5AA34868E0B2B4C0F3C4044DA67F33065F5E0DD75BB50B0390453CA`. 로딩 단계까지 포함한 원시 CSV와 추가 PNG는 `build/release/gpu-profile-2026-09-24`에 남는다.
