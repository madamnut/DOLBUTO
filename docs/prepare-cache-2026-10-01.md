# 공개 컬럼 목록·LOD coverage 재사용 (2026-10-01)

## 변경과 무효화

기존 WorldView::prepare는 매 프레임 columns_ 전체를 훑어 임시 published 벡터를 만들고, LodRenderer::prepare는 129×129 coverage를 0으로 초기화한 뒤 공개 컬럼 좌표를 변환해 마스크를 만들었다. 이제 공개 컬럼 목록과 Coverage를 보관하고 변경 시 재계산한다.

- 최초 실행: 목록 dirty=true, coverage valid=false로 빈 상태도 명시적으로 준비한다.
- 새 컬럼 공개: published=true 전환과 함께 목록 dirty. 정지 중 비동기 로딩 완료도 반영한다.
- 공개 컬럼 퇴거: 목록 dirty. 미공개 컬럼 삽입/퇴거는 목록 내용에 영향 없다.
- 청크 경계 이동: coverage 중심 비교로 좌표 변환을 갱신한다. 목록 멤버십이 같으면 그 목록은 그대로 재사용한다.
- 렌더 거리 변경: 기존 centre_ 초기화/stream request가 퇴거·추가를 처리하며, 실제 목록 변경이 발생하면 갱신한다.
- 재생성: 목록 clear+dirty, LodRenderer::clear에서 coverage valid=false. 첫 prepare에서 재구축한다.
- 블록·조명 편집: 컬럼 멤버십은 같으므로 목록 갱신 불필요. 메시/LOD scene 업로드와 교체는 기존대로 매 프레임 진행한다.
- 디버그 팔레트는 매 프레임 최신 상태를 반영한다. 각 frame slot의 fence 이후 GPU coverage 전체 복사와 flush는 유지한다. GPU 버퍼 갱신 생략이나 shader/culling 변경은 없다.

단일 렌더 스레드 소유 값이며 published 벡터에는 ColumnKey 값만 보관한다. 컬럼 map 재해시/삭제에 무효화되는 포인터를 저장하지 않는다. 현재 컬럼 맵 변경 지점(try_emplace/erase/clear)과 published 전환 지점을 소스 검토했다.

## 비교 조건

- 기준 소스 HEAD251aa6b(기존 LOD 목록 및 빈 청크 순회 개선 포함). 컬럼 AABB 선행 검사는 미적용 상태다.
- Windows/Release, i7-14700F/RTX5060, headless1280×900, 임시 돌 평지 seed1337, 정오12, view193.6/−90/−22, origin0/0, LOD64.
- 초기 로딩 완료 뒤240프레임 예열, 거리12/24/32마다 전후 각3회/640steady. 총18회/11,520표본, 순차 실행. 반복1·3은12→24→32/before→after, 반복2는32→24→12/after→before. 성능 실행은 validation/세부 계측 OFF.
- 별도 출력 검증은 validation/세부 계측 ON, 거리12/64steady로 기본·순환 경계(8191,8191)·상공(origin−1,8192)·위쪽4시점 전후8회. 원래 shadow raw4종+extent도 비교한다.
- 원본 실행 파일/기준 소스/설정/CSV/PNG/로그/수동 분석: build/release/prepare-cache-compare. 빌드 로그 build/prepare-cache-build.log.
- before SHA256: 03BA2612F4B88DD25AD50CE1E56A03B7DA05FAD19A2973B1BFC473EDCDDD5EA3.
- after SHA256: 5F743A6050A0D467C1D128558A9E0AFD015217FC891CFBF3B8653B007D049991.
- settings SHA256: 285B798EADAB3BBBB17C90CE7910A6431CBFACF1314814659A3BEAFC38A61CF4.

```text
variant.exe --headless --render-distance R --profile-gpu rR-variant-N-gpu.csv --profile-draws rR-variant-N-draw.csv --profile-samples 640 --profile-view 193.6 -90 -22 12 --seconds 300 --capture rR-variant-N.png
```

## 결과

성능18회/11,520steady 모두 exit0, Vulkan errors0/UI issues0/texture failures0. 실행마다640steady CPU/GPU frame 일치, LOD pending/queued0 및 draw/바인딩 카운터 관계를 확인했다. 이상치를 제거하지 않았으며 각 거리 전후1920프레임을 합산했다. 아래 단위는 ms, p95는 nearest-rank다.

| 거리 | 준비 평균 전→후 | 감소 | 준비 p95 전→후 | 준비+일반+LOD 평균 전→후 | 합계 감소 | 합계 p95 전→후 |
|---|---|---:|---|---|---:|---|
|12|0.031834 → 0.013376|58.0%|0.049600 → 0.016500|0.125116 → 0.109441|12.5%|0.191100 → 0.173700|
|24|0.057821 → 0.012469|78.4%|0.088900 → 0.013900|0.250906 → 0.191962|23.5%|0.402100 → 0.284300|
|32|0.098253 → 0.013143|86.6%|0.142900 → 0.021900|0.442330 → 0.342402|22.6%|0.688400 → 0.541200|

세 거리 각각3쌍 모두 준비 평균과 CPU 세 구간 합계가 감소했다. 준비 시간 절감은 프레임당 약0.018/0.045/0.085ms다. 거리12의 일반 지형 평균은0.036967→0.038320ms, LOD는0.056315→0.057745ms로 소폭 증가했지만 합계는 줄었다. 다른 구간의 변동을 전부 이번 변경의 효과라고 단정하지 않는다.

GPU 전체 평균은 거리12 1.672081→1.657809ms, 거리24 1.372662→1.391990ms, 거리32 1.511665→1.508608ms로 큰 변화가 없다. CPU 반복 작업 절감으로 채택하며 GPU 속도/FPS 비례 향상은 주장하지 않는다.

일반 draw209/1017/1819, triangles570112/1035648/1494656, LOD tiles402/447/489는 전후 모든 steady에서 같다. LOD draw 범위는 전404~405/447~449/491~493, 후404~406/448~450/490~491로 조금 다르다. 기존1MiB/1ms 업로드 예산에 따른 버퍼 part 분할이 실행마다 달라지며 그릴 면/타일 수는 동일하다.

별도4시점 전후8회 validation 실행도 모두 exit0/Vulkan errors0/기존 미사용 셰이더 출력 warnings10/UI0/texture0. 각64steady CPU/GPU frame 일치 및 그린 청크/triangles/LOD tiles 일치. 기본 시점 그림자 raw4종+extent는 바이트 동일하다. 기본·순환 경계 PNG를 직접 확인했고 지형/HUD 구성에 눈에 띄는 변화가 없다. 성능 PNG9쌍의 평균 절대 RGB 채널 차이는 최대0.339056/255로 완전 픽셀 동일은 아니다.

통계 사본: docs/benchmarks/prepare-cache-2026-10-01-summary.json. 기존 LOD/nonempty 개선과 함께 소스에 반영한다. 빌드 이후 추가한 헤더 설명 주석까지 최종 재빌드했으며 성능 측정본과의 실행 코드 변경은 없다. 최종 빌드 로그 build/prepare-cache-final-build.log, 패키징 로그 build/prepare-cache-package.log. 최종 바이너리 SHA256은 8EC722DE395971DC264D8A399D19CFD23B55C87B7407A8746E1D125137350740이며 측정본 after.exe도 원본 그대로 보관한다.

최종 재빌드본을 별도 validation/세부 계측 ON으로64steady 실행해 exit0/errors0/기존 warnings10/UI0/texture0을 확인했다. CPU/GPU frame 일치, near slots882/draw209, LOD tiles402/triangles570112와 기본 raw 그림자4종+extent가 기준과 일치한다. build/bin·out/DOLBUTO·final.exe 해시는 동일하며 패키지 settings.json과 격리 계측 settings.json도 보존했다. commit/push는 수행하지 않았다.

## 해석·범위

world_prepare_cpu_ms는 WorldView::prepare 전체 CPU 시간이며 이번에 생략한 계산만의 전용 타이머가 아니다. CPU 합계는 준비+일반 지형+LOD의 세 구간만 더한 값이며 전체 프레임 CPU나 FPS가 아니다. 고정 시점의 현재 평지 결과이므로 다른 GPU나 향후 복잡한 지형에 일반화하지 않는다.

초기 스트리밍/고정 시점/서로 다른 시작 좌표를 실행 검증했다. 실제 이동·거리 설정 변경·재생성·F4 조작 입력 검증은 수행하지 않았으며 해당 무효화 경로는 소스로 확인했다. 자동 테스트/CTest/CU/합성 입력은 추가하지 않았다.
