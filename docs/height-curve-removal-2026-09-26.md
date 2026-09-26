# Groundness 단일 높이 곡선 제거 · 2026-09-26

AI 컨텍스트. 사용자가 현재 미사용인 Groundness 단일 높이 곡선을 인게임/에디터에서 전부 제거하도록 `실시` 승인.

- generation_config: HeightPoint/HeightCurve/height_curve 및 여기에의존한 TerrainShaping(smoothness_curve/pv_curve/구형강절삭·압축) 삭제. TerrainSplines.enabled 제거. 현대 스플라인 offset/factor/jaggedness 및 값은 유지. 노이즈·워핑·3D·기후·표면룰 변경없음.
- TerrainGenerator::profile은현재 spline_profile만 호출. 구형 전환과간접곡선멤버/미사용단일입력base_height함수/구형PV접기함수를제거. 양수그라디언트4배는현재사용하던동작을항상적용. GenerationMap::base_height는현대잔굴곡적용전기준높이이므로존치.
- F8: 단일높이곡선UI/포인터상태/구형smoothness/PV곡선/강폭등컨트롤/구형복귀체크박스제거. 현재스플라인·잔굴곡·3D토글유지. Groundness설명/압축설명정정. F3와지도미리보기의구형OFF안내제거.
- 웹: heightCurve함수/생성설정의단일곡선·이전조합/스플라인enabled필드제거. 노이즈와현재곡선드래그·격자·워핑·저장동작유지. /api/curve는offset/factor/jaggedness만허용하고height요청은400.
- 저장 schema9. height_curve/terrain/splines.enabled를출력하지않음. 기존schema5..8에서splines.enabled=true인파일은현대파라미터만읽고폐기항목은무시하며메모리에서schema9정규화. schema1..4 또는구형모드OFF파일은명확한오류로거부하여알아서다른지형으로치환하지않음. 기존파일자동저장없음; 사용자확정저장시에만schema9로바뀜. 번들default.json은동일현대값의schema9로정리.
- 이전스플라인프리셋내에있는enabled고정키는parse_splines에서읽지않으며런타임멤버/토글은없음. 기존사용자파일/백업/과거진단자료의폐기키는보존한다.

## 검증

Release컴파일·링크성공. 격리로컬편집기HTTP수동비교,결과 build/release/height-curve-removal-inspection/results.txt. 게임자동테스트/CTest/합성입력/CU없음.

- 기존편집기와새편집기에현재사용자schema7동일설정입력. 전체월드와1024²지역각64²표본,12종지도모두전후응답바이트동일(총98,304표본). 기존3D밀도경로의현재양수4배동작은코드검토로동일확인; 실제전체블록월드비교라는뜻은아님.
- 정규화후schema9/폐기키없음/새schema왕복동일/현대파라미터모두동일. 구버전의폐기키없이도정상읽음. 제거API와구형OFF파일은400. 번들default schema9정상읽음.
- UI실제조작은검증하지않음. 사용자settings/worldgen바이트보존. 최종빌드·패키징은verification최신절참조.
