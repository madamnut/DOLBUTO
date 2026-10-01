# LOD 그리기 목록 재사용 · 헤드리스 전후 비교 · 2026-10-01

AI 작업 컨텍스트. 사용자 헤드리스 전후 비교 요청과 후속 `실시`에 따라 목록 재사용을 구현하고 동일 계측을 가진 두 Release 실행 파일을 비교했다. 앞선 draw-profile 기록은 최적화 전 비용 조사이며 아래가 실제 전후 비교다.

## 변경

LodRenderer는 active scene의 메시 순서를 유지하는 `draw_meshes_`를 보관한다. 일반 지형에 가려지는 level0 타일 제외와 메시 map 조회는 active scene 교체 또는 coverage 중심/마스크 변경 시 수행한다. 각 렌더 패스는 이 목록을 순회하면서 기존 시야·그림자 거리 판정과 push/bind/draw를 수행한다. 시야 판정 결과 자체를 캐시하지 않는다. 간접 그리기, 버퍼 통합, 새 컬링 수식이나 셰이더 변경은 없다.

목록의 포인터는 std::map 노드를 가리키며 active scene의 키는 collect에서 유지된다. 새 active scene을 받아들일 때 collect 전에 목록을 재구축하고 clear/shutdown에서는 먼저 비운다. pending 업로드 완료 전 scene을 조기 공개하지 않는다.

CPU CSV 끝에 `world_prepare_cpu_ms`를 추가했다. 기존 `WorldView::prepare` 전체 타이머(upload_cpu_ms)를 기록하며 순수 GPU 업로드 시간이 아니다. 목록 생성 비용이 준비 단계로 옮겨졌는지 확인하기 위해 before/after 모두 동일 계측을 사용했다.

## 조건과 재현

- Windows x64, i7-14700F, RTX 5060, Release, headless 1280×900. 성능 실행은 validation OFF.
- 현재 돌 평지, 자연 물 없음, seed1337, 원점 컬럼0/0, 카메라 높이193.6/yaw−90/pitch−22/정오12시, FOV75.
- 근거리 반경12/24/32, LOD ON/거리64 고정. 그림자 거리192/품질2, 구름 품질2/양50, 기타 효과 ON, VSync OFF/FPS 무제한.
- near 준비 완료 및 LOD pending/queued0, 최신 scene 업로드 완료 후240프레임 예열, 실행마다600steady. 거리별 전후 각각3회, 총18회/10,800steady 프레임. 이상치를 제거하지 않았다.
- 첫째·셋째 반복은 거리12→24→32에서 before→after, 둘째는 거리32→24→12에서 after→before. 단일 프로세스로 순차 실행했다. 600연속 프레임을600개의 독립 실험으로 해석하지 않는다.
- 원본 CSV/로그/PNG/설정/두 실행 파일/분석 스크립트: `build/release/lod-draw-compare`. 통계 사본: `docs/benchmarks/lod-draw-cache-2026-10-01-summary.json`.

격리 런타임에서 아래 명령의 R, variant, N을 바꿨다.

```text
variant.exe --headless --render-distance R --profile-gpu rR-variant-N-gpu.csv --profile-draws rR-variant-N-draw.csv --profile-samples 600 --profile-view 193.6 -90 -22 12 --seconds 300 --capture rR-variant-N.png
```

SHA256:

- before.exe: `6C2E990BD845E2375426C6FC17812683EECED1C50E80694962E9F21D836913ED`
- after.exe: `B571557C96AB860B7376021DD79DE80DD2335F448B91D686090BAD8567A411CF`
- settings.json 전후 동일: `285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4`

## 결과

CPU 단위는 ms/프레임. 평균과 p95는 각 조건1800프레임이며 p95는 nearest-rank다. LOD 구간은 순회·컬링·명령 기록을 포함한다.

| 근거리 반경 | LOD 평균 전 | LOD 평균 후 | 감소 | LOD p95 전 | LOD p95 후 |
|---|---:|---:|---:|---:|---:|
|12|0.153637|0.053059|65.5%|0.213100|0.084200|
|24|0.412085|0.053375|87.0%|0.466400|0.090200|
|32|0.636444|0.092912|85.4%|0.753600|0.115900|

| 반경 | 월드 준비 전→후 | 일반 지형 CPU 전→후 | 준비+일반+LOD 합 전→후 | 합 감소 | 전체 GPU 전→후 |
|---|---:|---:|---:|---:|---:|
|12|0.026147→0.030427|0.037908→0.044012|0.217691→0.127498|41.4%|1.655432→1.656013|
|24|0.062048→0.052846|0.277338→0.175087|0.751471→0.281308|62.6%|1.355474→1.363478|
|32|0.114490→0.105976|0.575692→0.486203|1.326627→0.685091|48.4%|1.479176→1.491012|

거리12의 준비 시간은 약0.0043ms 증가했지만 세 CPU 구간 합은 감소했다. 일반 지형 코드는 이번 최적화에서 바꾸지 않았으므로 그 구간의 변화까지 map 조회 제거의 직접 효과로 단정하지 않는다. CPU 캐시·스케줄링·프레임 진행 조건의 영향은 별도 분리하지 않았다. GPU 평균 차이는 약+0.04/+0.59/+0.80%로 GPU 개선은 관측하지 못했다. CPU 합은 전체 CPU 프레임 시간이 아니고 CPU/GPU는 겹쳐 실행되므로 이 감소율을 FPS 증가율로 바꾸지 않는다.

| 반경 | 일반 draw 전후 | LOD 타일 전후 | LOD draw 전→후 | triangles 카운터 전후 |
|---|---:|---:|---:|---:|
|12|209|402|403~405→402~404|570112|
|24|1017|447|449~450→447~449|1035648|
|32|1819|489|491~493→490~491|1494656|

동일한 면을 그려도 기존1MiB/1ms 업로드 예산에서 버퍼 part 분할 시점이 달라져 호출 수는 실행 간 조금 달랐다. 호출 수를 고정하거나 배칭한 변경이 아니다. 삼각형 카운터와 타일 수는 각 거리의 모든 전후 실행에서 일치했다.

## 검증과 한계

- 두 Release 빌드 성공. 로그 `build/lod-compare-before-build.log`, `build/lod-compare-after-build.log`. clang-format dry-run 및 git diff --check 통과.
- 성능18회 모두 exit0/Vulkan errors0/UI issues0/texture failures0. 매 실행 steady600, CPU/GPU frame 일치, LOD pending/queued0, near descriptor=draws+1, LOD vertex binds=draws 확인.
- 별도 before/after 각각 validation ON, 거리12/60steady 및 PNG/그림자 원본 캡처: 모두 exit0, Vulkan errors0, 기존 미사용 셰이더 출력 warnings10, UI/texture0. 성능 통계에서는 제외했다.
- validation의 `all-depth.f32`, `solid-depth.f32`, `colour.rgba8`, `shaft.rgba8`와 extent.txt는 전후 SHA256이 모두 일치했다.
- 거리32 전후 PNG 직접 시각 확인: 지형·하늘·그림자·HUD·선택 표시 정상. 전체9쌍의 PNG 평균 절대 채널 차이는 최대0.4005/255였다. 최대 개별 채널 차이는38이며 픽셀 완전 동일은 아니다. 종료 프레임/TAA 이력 등이 다르므로 PNG 차이를 전부 최적화 오류나 전부 TAA로 단정하지 않는다.
- 초기 로딩의 coverage 변경과 scene 교체를 포함해 실행했다. 이동 입력·블록 편집·월드 재생성·자연 물·복잡한 산악·다른 GPU의 실환경 검증은 하지 않았다. 현재 고정 평지 결과를 모든 장면에 일반화하지 않는다. 자동 테스트/CTest/CU/합성 입력은 추가하거나 사용하지 않았다.
- 결과가 반복 재현되어 캐시 변경을 유지하고 Release를 `out/DOLBUTO`에 패키징했다. 사용자 설정은 보존하며 commit/push는 하지 않았다.
