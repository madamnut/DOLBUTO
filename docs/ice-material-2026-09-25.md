# 얼음 재질 · 2026-09-25

AI 컨텍스트. 사용자 얼음 PNG 알파 제거, 핫바 원본 무늬, 월드 반투명/반사 요청과 로컬 Complementary 검토 후 `실시` 승인. 초기 얼음의 불투명 렌더/PNG 알파 보존 결정을 대체한다. Release 빌드·패키징, 사용자 설정 보존, CU/합성 입력/CTest 금지 유지.

## 구현

- ice.png 32×32의 모든 픽셀 알파190을255로 변경. RGB 바이트는 전후 동일한지 검사했다. 이전 SHA256 66F155949193551736735EF204B2B7AD2B11220FA068AF8ECE587AC04297B8A5, 새 SHA256 B118279720EBE2279256730F058771584D91C754B35D88DC7E363E92ACEACB5E. 월드 기본 불투명도는 셰이더 재질 상수190/255, 핫바는 PNG 그대로 불투명하다.
- 로컬 ref/ComplementaryUnbound의 translucentIPBR.glsl material32004: smoothness=G³, highlight=min(G²×1.5,1)²×3.5, reflectMult=.7 적용. gbuffers_water.glsl 공통 프레넬 가중치 (fresnel³×.85+.15)×.7, 즉 정면10.5%~비스듬히70%의 반사 색 혼합 후 알파 합성. 기존 GGX와 75%-per-axis SSR/하늘·구름·천체 보완을 재사용한다. 표면 법선은 평평하며 출렁임·이동·거품·물 흡수·물 전용 노멀 없음. packed/blue ice는 추가하지 않는다.
- ChunkMesh는 opaque faces / ice / water, GPU 업로드도 같은 순서. solid_count와 ice_count를 별도로 보관하며 조명만 갱신할 때도 두 개수를 보존한다. PackedFace4bytes/Block1byte/atlas9 유지. 얼음/얼음 접면은 제거하고, 얼음에 인접한 다른 불투명·유체의 면은 남긴다. 실제 충돌/설치/선택은 기존 높이1 고체다.
- 얼음은 전체 AO/광 차폐자가 아니고, 직접 하늘빛은 셀당1 감쇠하며 기존 블록광 전파를 허용한다. 초기/증분/경계 광 계산에 같은 분류 사용. 그림자 고체 단계에서 제외하고 복사 후 투명 단계에서 정적인 푸른 투과색 (.32,.43,.55)으로 처리한다. 물 코스틱 애니메이션은 적용하지 않는다.
- 얼음이 시야에 있을 때만 독립 WaterEffects 인스턴스로 얼음 SSR/합성을 수행한다. 물과 반사 텍스처를 분리하여 재질 간 저해상도 보간 오염을 방지한다. 불투명 장면/대기 → 얼음 배경 복사/SSR/가장 가까운 얼음 표면 합성 및 깊이 기록 → 물 배경 깊이 재설정 → 기존 물/전경 볼륨/TAA 순서다. 얼음 깊이를 물 경계로 오인하여 굴절시키지 않는다. 물 효과OFF에도 얼음 재질은 유지된다. GPU marker ice_snapshot/ice_ssr/ice_surface 추가.

## 한계

전체 Complementary/Iris 픽셀 동일 구현은 아니다. 그린 채널 기반 광택/프레넬은 원본 수식이며 그림자 투과색은 자체 고정 근사다. 현재 가장 가까운 얼음 표면 하나를 배경과 합성하므로, 여러 겹 떨어진 얼음 및 얼음 뒤의 물 등 투명 재질을 재귀적으로 누적하지 않는다. 전경 구름/빛줄기도 얼음 깊이까지 처리한다. 화면 밖 지형 SSR은 하늘 보완으로 대체되며 ray tracing은 아니다. 물과 얼음이 모두 보이면 SSR 버퍼/합성 비용이 각각 발생한다. 얼음 전용 옵션이나 설정 schema는 추가하지 않았다.

## 검증

- Release 컴파일/GLSL 컴파일 성공. 독립 임시 CPU 출력: 단독 얼음6면(opaque0), 인접 얼음10면, 얼음/돌5+6면, 얼음/물6+6면, 청크 halo 경계 얼음5면. 돌→얼음 증분 광과 새 컬럼 계산의 밝기/차폐 불일치0, 얼음 sky14/opaque false, 충돌 full block 유지.
- build/release/fluid-inspection의 임시 WorldView 구현 사본만 고정 재질 장면을 준비하게 하여 별도 ice-capture.exe를 링크했다. 입력 합성 없이 기존 CLI80프레임 캡처로 물ON/OFF 각각 확인. 프로덕션 소스에 fixture/자동 실행 경로를 추가하지 않았고 배포 실행 파일에도 포함하지 않는다. ice-material.png / ice-water-off.png에서 얼음 뒤 블록 무늬·평평한 재질·불투명 아이콘과 물OFF 시 유지 확인. 각 Vulkan오류0/기존경고14/UI문제0/텍스처실패0. 경고는 기존 overlay/미사용 vertex 출력류다.
- 자동 테스트/CTest는 추가하지 않았다. 실제 사용자 입력으로 설치·파괴하는 플레이는 이번 검증 범위 밖이다.
- 최종 Release 빌드·포맷 검사·out/Sandbox 패키징 완료. 실행파일/관련 SPIR-V/ice.png 배포 해시 일치. settings.json 및 worldgen.json은 작업 전후 SHA256 동일.
