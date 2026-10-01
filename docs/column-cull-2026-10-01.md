# 컬럼 단위 시야 제외 · 전후 비교 · 2026-10-01

AI 작업 컨텍스트. 사용자 다음 작업 후보1번 추천 후 `ㄱㄱ`로 일반 지형 컬럼 전체의 화면 밖 제외와 실제 전후 비교를 승인했다. 이전 nonempty 청크 마스크를 유지한 상태에서 추가 효과를 확인한다.

결론: 후보는 미채택했다. 검사 개수와 출력 정확성은 확인했으나 현재 평지에서 일반 지형 CPU 성능이 일관되게 개선되지 않았다. 최종 소스/빌드는 직전 nonempty 최적화 버전을 유지한다.

## 후보 구현

주 화면의 published 컬럼에서 카메라 상대 base를 계산하고, 16×512×16 AABB를 기존 visible 함수에 전달한다. 각 면에1/16블록 여유를 둬 반올림 오차에 대해 보수적으로 판정한다. 완전히 화면 밖이면 컬럼 내 nonempty 청크 순회를 생략하고, 남은 청크는 기존 시야 검사·선택 표시·고체/물/얼음 처리·순서를 유지한다. 새 가려짐 판정이나 그림자 컬링이 아니라 기존 카메라 frustum 검사의 선행 단계다. 그림자/LOD/셰이더 및 면 데이터는 바꾸지 않는다.

`--profile-near-detail`에서는 기존 마스크 일치 검사에 더해 제외 컬럼의 nonempty 청크를 모두 기존 visible 함수로 재검사한다. 하나라도 통과하면 `Column culling rejected a visible chunk.` 오류로 종료한다. 이 참조 검사는 일반/성능 실행에 들어가지 않는다. near_chunk_slots는 컬럼 판정을 통과해 실제 개별 검사하는 청크 수, nonempty/culled는 컬럼에서 생략한 청크도 포함한다. sampled_columns는 통과 컬럼 중 표집 대상 수이며, 컬럼 검사와 참조 검사는 sample 시간 밖이다. 상세 시간은 최적화 전후 성능 비교에 사용하지 않는다.

## 환경 및 재현

- Windows x64/i7-14700F/RTX5060/Release/headless1280×900. 평지/자연 물 없음/seed1337/원점0,0/높이193.6/yaw−90/pitch−22/정오12/FOV75.
- 근거리12/24/32, LOD64 ON, 그림자192/품질2, 기타 효과 ON, VSync/FPS 제한 OFF. 성능 실행은 validation/세부 계측 OFF.
- near와LOD 최신 scene 준비 완료 후240프레임 예열, 실행당640steady. 거리별 전후 각3회, 총18회/11,520steady. 첫째·셋째는12→24→32/before→after, 둘째는32→24→12/after→before. 순차 실행하며 이상치를 제거하지 않는다.
- 원본 파일/CSV/로그/PNG/설정/분석은 `build/release/column-cull-compare`, 빌드 로그 `build/column-cull-build.log`.
- before SHA256: `996791CA04DEA29E998CC945C4170E08BEDE5B8765E63D0EB104107D698F3261`.
- candidate after SHA256: `9A1E212D04523F0D2A78574D17E2DDCD33BC04D49CFAF1208594BC51276B7F05`.
- settings.json SHA256: `285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4`.

```text
variant.exe --headless --render-distance R --profile-gpu rR-variant-N-gpu.csv --profile-draws rR-variant-N-draw.csv --profile-samples 640 --profile-view 193.6 -90 -22 12 --seconds 300 --capture rR-variant-N.png
```

별도 validation ON/세부 계측 ON 비교는 거리12/64steady/정오12로 아래 조건을 사용했다.

| 장면 | 카메라 높이/yaw/pitch | 원점 컬럼 |
|---|---|---|
|표준|193.6/−90/−22|0/0|
|순환 경계|193.6/45/−2|8191/8191|
|상공에서 아래로|600/137/−89|−1/8192|
|위쪽|193.6/0/60|0/0|

## 출력 검증

각 장면 전후 모두exit0/Vulkan errors0/기존 미사용 셰이더 출력 warnings10/UI issues0/texture failures0. 초기 로딩부터 모든 제외 컬럼의 자식 청크를 참조 판정했고, visible 자식을 제외하는 경우는 없었다. 아래 값은 각64steady 전체에서 동일했다.

| 장면 | 개별 청크 검사 전→후 | 실제 그린 청크 전후 | 후보의 제외 컬럼 |
|---|---:|---:|---:|
|표준|882→436|209|223|
|순환 경계|882→278|125|302|
|상공에서 아래로|882→882|882|0|
|위쪽|882→604|1|139|

표준 before의 진단용 무여유 AABB는224컬럼을 제외 가능으로 계산했지만, 후보의 여유가 있는 판정은223개였다. 경계에서 더 보수적인 결과이며 기존 visible 청크 수는 동일하다. 상공 장면처럼 전체 컬럼이 시야에 들어오면 순회 절감 없이 선행 검사 비용이 추가되는 구조다.

표준 validation의 shadow raw4종 및 extent.txt SHA256은 전후 동일했다. 성능 데이터에 이 validation/세부 계측 실행은 포함하지 않는다. 소스 빌드와 clang-format/git diff --check 성공. 실제 카메라 이동·물/얼음 편집·다른 GPU를 조작한 검증은 아니며 자동 테스트/CTest/CU/합성 입력은 사용하지 않았다.

순환 경계와 상공 장면의 전후 PNG를 직접 비교했다. 경계의 지형·HUD는 같은 구성이며, 상공 장면은 두 버전 모두 구름/대기 효과로 덮인 모습이다. 상공 PNG만으로 세부 지형 동일성을 보장하지 않으며 실제 draw/triangles/LOD 카운터 및 참조 판정으로 추가 확인했다. 8회 모두 CPU/GPU64steady frame 일치, nonempty=culled+visible, 그린 청크/triangles/LOD tiles 전후 일치를 확인했다. 결과 `validation-summary.json`.

## 성능 결과와 판단

성능18회 모두exit0/Vulkan/UI/texture 오류0. 각640steady와 CPU/GPU frame 일치, LOD pending/queued0 및 바인딩 카운터 관계를 확인했다. 거리별 각1920프레임 평균, nearest-rank p95, 단위 ms. 통계 사본 `docs/benchmarks/column-cull-2026-10-01-summary.json`.

| 근거리 | 일반 CPU 전 | 일반 CPU 후 | 변화 | p95 전 | p95 후 |
|---|---:|---:|---:|---:|---:|
|12|0.033215|0.033567|1.1% 증가|0.047400|0.055200|
|24|0.140951|0.136383|3.2% 감소|0.204900|0.207200|
|32|0.259455|0.276524|6.6% 증가|0.404600|0.439200|

| 근거리 | 준비+일반+LOD CPU 전→후 | GPU 전체 전→후 |
|---|---:|---:|
|12|0.116020→0.112479|1.794492→1.906498|
|24|0.255551→0.248450|1.408767→1.405140|
|32|0.425426→0.441865|1.536860→1.507772|

거리24는 평균만 조금 감소했으나 세 쌍 중 첫째·둘째는 각각0.126042→0.135633, 0.137334→0.137537로 증가했다. 셋째0.159477→0.135978의 감소가 평균을 낮췄다. 거리32는 세 쌍 모두 증가(0.275090→0.302987, 0.246583→0.269057, 0.256692→0.257529)했고 모든 거리의 일반 CPU p95도 증가했다. 측정 간 변동이 있으므로 증가율을 모든 PC/장면의 보장값으로 해석하지 않지만 채택을 뒷받침하는 일관된 개선은 없다.

현재 평지는 nonempty 최적화로 이미 컬럼당2청크만 방문한다. 컬럼마다 큰 AABB를 먼저 검사하는 비용을 추가한 만큼 후속 검사를 줄여야 하는데, 기존 조건에서는 이득이 작았던 것으로 해석한다. 평면별 검사 비용이나 CPU 캐시 영향을 별도로 분리하지 않았으므로 이를 유일한 원인으로 단정하지 않는다. 많은 세로 청크가 존재하는 장면이나 다른 bounds/계층에서는 결과가 달라질 수 있지만 이번에는 측정하지 않았다.

GPU도 실행 간 변동이 컸다. 예를 들어 거리12 after 셋째는2.279671ms였으며 다른 두 번1.722662/1.717161ms보다 높았다. 이런 값을 삭제하지 않았고, GPU 차이를 이 CPU 변경의 직접 효과로 해석하지 않는다. GPU/실제 FPS 개선은 입증하지 않았다.

일반 draw209/1017/1819, triangles570112/1035648/1494656, LOD tiles402/447/489는 전후 동일했다. LOD draw는 기존 업로드 part 분할 차이로 소폭 달랐다. 전체9 PNG 쌍의 평균 절대 RGB 채널 차이 최대0.339372/255이며 픽셀 완전 동일을 주장하지 않는다.

## 최종 상태

후보 구현은 `build/release/column-cull-compare/candidate-world_view.cpp`와 `candidate.patch`, after.exe에 남겼다. 제품 소스 world_view.cpp는 비교 전 스냅샷으로 복원했고 git diff로 HEAD(251aa6b)와 동일함을 확인했다. 기존 LOD 및 nonempty 최적화를 취소한 것이 아니라 이번 추가 컬럼 검사만 제거했다. 변경 기록과 통계는 보존하며 commit/push는 하지 않았다.

첫 복원은 Copy-Item이 원래 수정시간을 보존하여 Ninja가 world_view.cpp의 후보 object를 재사용했다. 실행 확인에서 슬롯436이 남아 있음을 발견했고 해당 소스의 수정시간을 갱신해 다시 컴파일했다. 실패한 복원 기록은 restored-stale.*로 보존했다. 최종 빌드 로그 `build/column-cull-restored-build-final.log`에서 world_view.cpp.obj 재컴파일을 확인했으며 패키징 로그는 `build/column-cull-restored-package-final.log`다. 단순 소스 diff/빌드 성공만으로 복원을 완료한 것으로 판단하지 않았다.

최종 restored.exe는 validation/세부 계측 ON/거리12/64steady에서 exit0/Vulkan errors0/기존 warnings10/UI0/texture0. 슬롯882, draw209로 실제 기존 순회 복귀를 확인했고 raw 그림자4종+extent도 before와 동일했다. build/bin·out/DOLBUTO·restored.exe SHA256은 모두 `03BA2612F4B88DD25AD50CE1E56A03B7DA05FAD19A2973B1BFC473EDCDDD5EA3`다. 재빌드 바이너리는 기존 before.exe와 바이트 동일하지 않으며 소스 동일성·재컴파일·실행 결과로 복원을 확인했다. settings.json은 위 원래 해시를 보존했다.
