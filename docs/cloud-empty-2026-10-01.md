# 구름 기여0 픽셀의 색 변환 생략 비교 (2026-10-01)

최종 상태: 사용자 취소 요청으로 미채택/복원했다. 전체 GPU 지연시간이 최우선이며 일부 구간의 작은 개선만으로 채택하지 않는다. 아래 채택·최종 반영 절은 취소 전 실험 이력이다.

## 사용자 요청에 따른 복원

atmosphere.frag를 before 소스로 복원하고 수정시간을 갱신한 뒤 실제 SPIR-V 재컴파일·Release 빌드·패키징했다. 복원 EXE의 build/bin·out·격리 실행 해시는 C8D7DD370D0610DE2D3BB857EAB1F4D24E4D526A0763E9F3522EF41A905E754F로 같다.24개 SPIR-V 모두 before와 일치하며 사용자 설정 보존을 확인했다. 복원64steady/validation ON/TAA OFF/블룸40 지형 실행은 오류0/기존 warnings10, CPU·GPU frame 일치, draw209/descriptor 갱신0, 기준 PNG 픽셀 동일. 로그는 build/cloud-empty-restored-build.log, build/cloud-empty-restored-package.log와 격리 폴더 restored.log. 기존 블룸/descriptor 재사용 변경은 유지한다. 후보 소스는 candidate-atmosphere.frag로 보존했다.

## 후보

atmosphere.frag의 구름 합성에서 최종 volume RGBA가 모두 정확히0일 때 두 색 공간 변환(pow 1/2.2, pow 2.2)을 생략한다. 원래 max(base,0)은 유지하며 threshold나 근삿값 판정을 사용하지 않는다. RGB 또는 불투명도가 하나라도0이 아니면 기존 식을 그대로 실행한다. 구름 생성·보간·경계 fallback·빛줄기 계산·TAA·블룸·렌더 패스·리소스·카메라 갱신은 변경하지 않는다.

수학적 항등 변환을 생략하지만 부동소수점 왕복의 반올림이 달라질 수 있으므로 픽셀 동일성을 가정하지 않고 별도 TAA OFF 캡처로 비교한다. 새 분기 비용 때문에 실제 GPU 개선도 가정하지 않는다.

## 비교 조건

- Windows/Clang Release, i7-14700F/RTX5060, headless1280×900, 거리12/LOD64/FOV75, 평지 seed1337. 앞서 채택한 블룸16샘플과 descriptor 재사용을 양쪽에 포함한다.
- 지형(height193.6,yaw−90,pitch−22), 하늘(193.6,0,60), 구름층 아래 경계(620,0,8), 모두 hour12/origin0/0. 구름 고도640/양50/품질2.
- 성능은 블룸12/TAA ON/validation OFF, 지형·하늘·경계 각3쌍18회. 준비 완료 뒤240프레임 예열, 실행마다640steady. 반복1·3은 장면 정순/before→after, 반복2는 역순/after→before. 순차 실행, 이상치 제거 없음.
- 별도 출력 검증은 세 장면과 구름OFF 지형의 전후8회, TAA OFF/블룸40/validation ON, 각64steady. 구름OFF에서도 빛줄기 설정은 유지한다.
- 실행별 EXE와 셰이더 폴더를 분리한다. 원본 소스·설정·로그·CSV·PNG·실행 인자와 해시: build/release/cloud-empty-compare. 빌드 로그 build/cloud-empty-build.log.
- before atmosphere SPIR-V SHA256: 24077E7345E984EDB8AAA0F167E4785BB3491F38CC7D76AD8194C4FBF742BF48.
- after atmosphere SPIR-V SHA256: 55D8F4A76A7310983EC26AE1CB995E68CDB2D344C9F1984B1661809CEBEC826C.

```text
VARIANT/DOLBUTO.exe --headless --render-distance 12 --profile-gpu NAME-gpu.csv --profile-draws NAME-draw.csv --profile-samples 640 --profile-view HEIGHT YAW PITCH 12 --seconds 300 --capture NAME.png
```

대상 GPU 시간은 volume_composite 전체이며 색 변환 외에 기존 깊이 판정·합성·빛줄기 계산도 포함한다. 실제로 생략된 픽셀 수나 GPU 명령 수는 계측하지 않았다. 다른 GPU/해상도나 실제 이동·수중·리사이즈 조작에 일반화하지 않는다. 자동 테스트/CTest/CU/합성 입력은 추가하지 않는다.

## 화면 검증

8회 모두exit0/Vulkan errors0/기존 셰이더 미사용 출력 warnings10/UI0/texture0. 각64steady CPU/GPU frame 일치, LOD pending/queued0, 전후 draw/triangles/LOD tiles/columns가 같았다. 기존 descriptor 재사용도 steady 호출0/쓰기0으로 유지됐다.

| 장면 | 평균 절대 RGB 채널 차이(0..255) | 최대 차이 | 차이 있는 픽셀 비율 |
|---|---:|---:|---:|
|지형|0.004076|1|1.2080%|
|하늘|0.001020|1|0.3042%|
|구름층 아래 경계|0.00000145|1|0.000434%|
|구름OFF 지형|0.004402|1|1.3174%|

네 장면의 채널 차이p99는0이다. 후보의 하늘·구름층 아래 PNG를 직접 확인했으며 태양/구름과 하늘의 경계가 포함돼 있다. 픽셀 완전 동일은 아니며 양자화된 최종 출력에서 확인한 차이다. HDR 중간 버퍼의 오차는 별도로 읽지 않았다.

## 성능 결과

전18회/11,520steady 모두exit0/Vulkan errors0/warnings0/UI0/texture0. 각640steady CPU/GPU frame 일치, LOD 준비 완료, 장면별 전후 draw/triangles/LOD tiles/columns 동일. 각 변형1920steady 평균, nearest-rank p95, 단위ms다.

| 장면 | 합성 평균 전→후 | 감소 | 합성 p95 전→후 | 전체 GPU 평균 전→후 | 전체 GPU p95 전→후 |
|---|---|---:|---|---|---|
|지형|0.130057 → 0.128377|1.3%|0.129984 → 0.128000|1.645986 → 1.641945|1.824224 → 1.805664|
|하늘|0.244805 → 0.244619|0.1%|0.243648 → 0.243168|1.614347 → 1.615692|1.766048 → 1.790752|
|구름층 아래 경계|0.213327 → 0.213096|0.1%|0.213472 → 0.212288|1.148422 → 1.151999|1.156896 → 1.277920|

지형3쌍은0.130343→0.128312,0.129205→0.127869,0.130625→0.128948로 모두 감소했고 평균 절감량은 약0.00168ms다. 하늘3쌍 중2쌍, 경계3쌍 중1쌍만 평균이 감소했다. 총9쌍의 대상 p95는 모두 감소했지만 하늘·경계의 평균 차이는 변동과 구분되는 확실한 이득으로 해석하지 않는다. 구름이 섞인 픽셀은 원래 식을 그대로 실행하고 분기 비용도 추가되므로 모든 장면이 빨라질 것을 기대하지 않는다.

전체 GPU 평균은 지형 약0.2% 감소, 하늘 약0.1% 증가, 경계 약0.3% 증가였다. 하늘·경계의 전체p95도 증가했다. 특히 경계 전체p95는1.156896→1.277920ms로 증가한 반면 대상 합성p95는 감소했다. 다른 단계의 변동도 포함돼 있으며 단일 원인을 분리하지 않았다. 전체 GPU/FPS 향상이나 끊김 감소를 주장하지 않는다. 통계는 [JSON](benchmarks/cloud-empty-2026-10-01-summary.json)에 보존한다.

TAA ON 성능 PNG9쌍의 평균 절대 RGB 채널 차이 최대0.426473/255, 해당 쌍 최대28이다. 캡처 프레임/TAA 위상이 같지 않으므로 후보 수식만의 오차로 해석하지 않는다. 화면 오차 평가는 앞 절의 별도 TAA OFF 결과를 사용한다.

## 최종 반영

atmosphere.frag의 조건 분기만 이번 작업에서 변경했다. 이전 블룸16샘플/descriptor 재사용과 사용자 설정은 유지한다. Release 빌드·패키징 완료, build/cloud-empty-package.log 참조. after·build/bin·out EXE SHA256 A6B21AFE82ACE12463D78BC4073179A42F54C46F05040937302D456B2187ED04 일치. before EXE는 D7AA96421842643C3BFDC8AA5752C5C4304A5C17C21C73AE0DE798203B41DB95다.24개 SPIR-V 중 atmosphere만 바뀌었고 다른23개는 기준과 같다. 배포/빌드/after의 atmosphere 해시는 위 after 값으로 일치한다. 패키지 settings.json 보존을 확인했다. commit/push는 수행하지 않았다.
