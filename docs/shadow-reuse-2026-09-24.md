# 고체 그림자 깊이 재사용 비교 · 2026-09-24

AI 작업 컨텍스트. 사용자 `하고 비교해서 알려줘 실시`로 고체 깊이 재사용과 전후 비교를 승인했다. 다른 렌더 최적화, 해상도/거리/갱신 주기 변경은 범위 밖이다.

## 구현

기존은 layer0에 고체+물+플레이어, layer1에 고체+플레이어를 따로 그렸다. 현재는 다음 순서다.

1. layer1에 고체/플레이어 깊이와 두 색 attachments를 CLEAR 후 기록한다. 기존 shadow.frag가 고체 색0과 빛줄기에 쓰는 높이 alpha를 만든다.
2. D32 깊이를 layer1→layer0으로 vkCmdCopyImage한다. 깊이 규약/투영/왜곡/크기가 같으므로 변환이나 보간은 없다.
3. layer0와 색 attachments를 LOAD해서 물만 그린다. 물이 고체보다 가까운 픽셀만 기존 LESS 깊이 검사를 통과해 깊이·색·빛줄기 정보를 대체한다.

고체와 플레이어의 중복 draw/vertex 처리를 제거한다. 물 형상/그림자 shader 수식/색/빛줄기/조명 샘플링/그림자 업데이트는 유지한다. 고체 전용 fragment 없는 pipeline2개가 사라지고, 기존 색 shader가 고체 pass에도 사용된다. 따라서 절약량은 기존 고체 전용 pass 시간 전체와 같지 않다. 복사·전환 비용도 포함해 측정한다.

프레임 내 graphics queue에서 depth attachment→transfer→attachment 전환과 colour write→LOAD 동기화를 수행한다. CPU readback/idle은 정상 경로에 없다. 설정 OFF/크기 변경/첫 프레임은 기존 준비 흐름을 따른다. 동일 크기 전제를 ensure_images가 검사하며 현재 두 행렬은 prepare에서 같은 값을 지정한다.

## 비교 장치

- `--capture-shadow-maps <directory>`는 --profile-gpu가 끝난 마지막 프레임 제출 뒤 GPU idle을 기다리고 4개 이미지를 읽는다. 이 readback은 프레임 timer 뒤에 실행되며 일반 실행에서는 호출하지 않는다. 출력은 little-endian D32 float인 all-depth.f32/solid-depth.f32와 RGBA8인 colour.rgba8/shaft.rgba8, 두 맵 한 변 크기의 extent.txt다.
- CLI `--validation`은 명시적으로 Vulkan validation을 요청한다. 성능 실행은 OFF, 리소스/동기화 확인은 별도로 수행한다. 테스트 프레임워크나 합성 입력을 추가하지 않았다.
- 기존 배포 original.exe를 보존했다. 비교 before.exe에는 readback 및 동일한 image transfer usage만 추가하고 기존 두 pass의 렌더 경로를 유지했다. after.exe는 같은 usage와 최적화 경로를 사용한다. GPU 이미지를 읽는 기능의 유무 때문에 서로 다른 usage/layout 가능성을 비교하지 않도록 맞췄다. 이전 source snapshot과 각 바이너리를 build/release/shadow-reuse-2026-09-24/before-source 및 별도 runtime에 보관한다.
- `tools/benchmarks/shadow_compare.py`는 수동 실행용이다. 같은 settings/worldgen 사본, 같은 runtime의 before.exe/after.exe를 장면별로 교대 실행한다. 반복1·3은 before→after, 반복2는 after→before이며 장면 순서도 뒤집는다. CMake/CTest/게임 시작 검사에 연결하지 않는다.
- 240프레임 예열 후600프레임을 측정하며 렌더 반경24/1793컬럼 완료를 확인한다. 프레임 슬롯의 GPU query 결과와 마지막 두 프레임을 모두 회수한다. 구간 합은 반올림 범위 내 total과 같아야 한다. 총 그림자 비용은 이전 shadow_all+shadow_solid, 이후 shadow_solid+shadow_depth_copy+shadow_water다.
- GPU timestamps는 GPU 경과시간이다. CPU 명령 생성/present/scanout을 포함한 FPS가 아니며 marker/barrier/clear 비용과 GPU pipeline 중첩의 영향을 받는다. 클럭/전원/OS 부하를 강제 고정하지 않았으므로 작은 차이를 과장하지 않는다.
- 그림자 맵4장 자체를 비교하므로 TAA/프레임별 dither 차이와 분리할 수 있다. 최종 화면은 정지 자체 캡처로 확인하되 픽셀 전체의 bitwise 일치를 보장하는 실험은 아니다. 이동·시간 변화의 동영상 검증은 수행하지 않는다.

## 입력과 환경

- RTX3080, Windows x64, Clang23.1.0 Release, Vulkan SDK1.4.341.1. 출력1280×900, FOV90, 렌더 거리24, VSync OFF/무제한. shadow_distance512/quality2(2048² 두 장), 구름 quality3/양100/높이512, 빛줄기 quality2, 블룸40, TAA ON. 사용자 옵션을 그대로 복사한다.
- 사용자 settings SHA256: `4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1`.
- 사용자 worldgen SHA256: `964ABA96CC5E6D75653D7C265E0DAD0F4E85CD47650DEA53873E951560742C9D`.
- 해안: 컬럼7040/5504, 카메라Y220/yaw-90/pitch-22. 육지:3456/1152,Y270/-90/-22. 산지:8064/3456,Y285/-90/-22. 수중:7040/5504,Y190/-90/+15. 성능 장면은 정오12시, 바람 위상0이며 실제 키보드/마우스를 조작하지 않는다.

## 결과

4장면×전후2종×3회×600프레임, 총24실행/14,400steady프레임이다. 장면/구현별1,800표본을 합쳤으며 이상값을 제거하지 않았다. 평균은 프레임 경과시간의 산술평균, p95는 nearest-rank다. 연속600프레임을 독립된600회 실험으로 해석하지 않는다.

| 장면 | 그림자 평균 전→후(ms) | 그림자 감소 | 전체 GPU 평균 전→후(ms) | 전체 감소 |
|---|---:|---:|---:|---:|
| 해안 |2.537→1.522|40.0%|4.610→3.583|22.3%|
| 육지 |2.229→1.226|45.0%|3.309→2.410|27.2%|
| 산지·물 |2.519→1.421|43.6%|4.711→3.639|22.8%|
| 수중 |2.506→1.520|39.3%|4.264→3.294|22.7%|

| 장면 | 전체 GPU 중앙값 전→후(ms) | 전체 GPU p95 전→후(ms) |
|---|---:|---:|
| 해안 |4.628→3.420|5.137→4.122|
| 육지 |3.129→2.136|3.859→2.922|
| 산지·물 |4.755→3.420|5.269→4.242|
| 수중 |4.098→3.153|4.886→3.954|

변경 후 평균 구성은 해안 고체1.113+복사0.061+물0.348ms, 육지1.154+0.067+0.005ms, 산지1.218+0.063+0.140ms, 수중1.113+0.062+0.346ms다. 복사/전환 비용을 포함해 그림자 평균약0.99~1.10ms를 절감했다.

육지 after 첫 실행에는 전체 GPU107.279ms, 그림자 구간45.105ms까지 긴 표본이 있었다. 원인을 특정하지 못했으며 제거하지 않았다. 육지 after 실행별 전체 평균2.745/2.203/2.283ms, before3.327/3.235/3.364ms다. 해안/산지/수중에서도 실행 변동이 있어 소수점 이하를 고정 성능으로 취급하지 않는다. 위 전체 GPU 감소율은 실제 사용자 FPS 증가율과 같지 않다.

## 값·화면·Vulkan 확인

- 첫 반복의4장면에서 all-depth/solid-depth/colour/shaft, 총16개2048²이미지를 비교했다. 모든 이미지가 바이트 단위로 동일하다. 비교한 texel은67,108,864개이며 입력/출력 파일 SHA를 comparison.json에 보존했다. 검증한 고정 장면에 대한 결과이지 모든 월드/시간에서 bitwise 일치를 증명한 것은 아니다.
- 자체 캡처로 해안·육지·산지·수중을 읽어 확인했다. TAA history와 최종 frame index가 실행마다 달라 전체 PNG는 동일하지 않다. RGB채널별 평균 절대차는 해안0.171~0.333, 육지0.369~0.653, 산지0.271~0.570, 수중0.032~0.039(8bit0..255 기준)이다. 해안/육지/산지의 최대 채널 차이는81~95, 수중2까지 있어 '최종 화면의 모든 픽셀이 같다'고 보고하지 않는다. 이번 변경의 직접 출력인 그림자 맵4종은 일치했다.
- 성능 실행24회는 모두exit0,1793컬럼/1280×900/600steady프레임을 완료했고 UI/texture/Vulkan 오류 로그0이었다. 성능 실행의 validation은OFF다.
- 추가로 after에 --validation과 SDK1.4.341.1의 khronos_validation.validate_sync=true, syncval_submit_time_validation=true를 지정했다. 반경2의3회 진단으로 quality1/2/3(1024²/2048²/4096²), 게임 시각19/12/0, 수면 위/수중을 확인했다. 마지막 맵 readback까지 포함해 전부exit0, validation=1, Vulkan errors0, SYNC-HAZARD0, UI/texture 오류0이다. map 파일 크기도 각 extent×extent×4bytes와 같았다.
- 각 validation 로그에는 경고14개가 있다. 외부 EOS 중복 레이어 및 Bandicam/Overwolf 레이어의 API버전 경고4개, 공용 vertex shader 출력이 fragment shader에서 소비되지 않는 인터페이스 성능 경고10개(중복 출력 제한 포함)다. 경고가 없었다고 보고하지 않는다. shader/외부 프로그램 정리는 이번 범위에 적용하지 않았다.
- 자동 테스트/CTest나 합성 입력/CU를 사용하지 않았다. validation용 설정은 임시 runtime에서만 변경했고 원래 사본으로 복원했다. 움직이는 카메라/태양의 동영상 비교는 하지 않았다.

## 빌드·보관·재현

- Release 증분 빌드와 패키징 완료. 변경 C++의 clang-format --dry-run --Werror 통과. 최초 readback 구현의 enum narrowing 컴파일 오류는 명시적 VkImageAspectFlags 변환으로 고쳤다. 최종 비교 바이너리는 성공한 빌드만 사용했다.
- before.exe SHA256: `09E650FE3ED90A42B3AA42D0EA84493B94214C39676E466FA513BB24CDA5265E`.
- after.exe/빌드/배포 sandbox.exe SHA256: `1743CA2FBCBD7DF1671B00887809E54101DB5805FE9CF7BC68C443508B4A6AE7`.
- 사용자 settings/worldgen은 위 입력 해시를 유지한다. settings schema, 저장된 값, 지형 생성기, 물 반사/블룸 구현은 바꾸지 않았다.
- `python tools/benchmarks/shadow_compare.py build/release/shadow-reuse-2026-09-24 --runtime build/release/shadow-reuse-runtime`로 명시적 비교를 수행했다. runtime을 생략하면 기존 CSV/manifest/맵을 집계한다. 완료된 run.json을 덮어쓰지 않으므로 재실행은 입력 사본을 둔 새 출력 폴더를 사용한다.
- `docs/benchmarks/shadow-reuse-2026-09-24-comparison.json`에 실행별/통합 통계, 맵 비교 해시, 이미지 차이, validation 요약이 있다.
- `docs/benchmarks/shadow-reuse-2026-09-24-results.zip`에288,000행의steady CSV, 설정/생성 입력,24개 manifest와로그,빌드로그,validation로그와설정,전후PNG를보존했다. SHA256 `5F7412E7A495887564EEA45524E3456BB47BC764EA83AFA99B252CC58E402F4D`. 로딩 표본을 포함한 전체 CSV, raw맵, 원본/비교 바이너리는 build/release/shadow-reuse-*에 남아 있다. 첫 pilot 두 실행은 정식 통계에 포함하지 않았다.
