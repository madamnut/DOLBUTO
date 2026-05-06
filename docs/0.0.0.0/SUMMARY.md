# 0.0.0.0 Summary

## 범위
- Vulkan/C++20 기반 DOLBUTO 초기 프로젝트 골격을 구성했다.
- CMake/Ninja, GLFW, Vulkan SDK, bundled GLFW binary 사용 방식을 정했다.
- 목표를 마인크래프트류 샌드박스 복셀 게임으로 정하고, 청크 기반 월드 구조를 도입했다.

## 렌더링
- Vulkan instance, device, swapchain, render pass, depth buffer, command buffer, sync object 기본 구조를 구성했다.
- 하늘, 해, 달, 크로스헤어, 디버그 텍스트, 스크린샷 기능을 추가했다.
- F11 전체화면 토글, F3 디버그 텍스트 토글, F4 wireframe 토글, F5 시점 토글을 추가했다.
- terrain 렌더링은 초기 raymarch 실험 후 mesh 기반으로 전환했다.
- 그리디 메싱, 프러스텀 컬링, ambient occlusion, mipmap 거리 선택을 적용했다.
- terrain buffer를 `DEVICE_LOCAL`로 이동하고 staging upload batch를 적용했다.
- terrain descriptor bind 반복을 줄이고, vertex pulling 및 quad record 기반 terrain draw를 실험했다.

## 월드와 청크
- 청크 크기를 `16x512x16`으로 정하고, 메싱 단위는 `16x16x16` 서브청크로 정했다.
- 동쪽은 `X+`, 북쪽은 `Z+`로 좌표계를 확정했다.
- 블럭 좌표 기준은 블럭의 아래 중앙으로 정했다.
- 플레이어 주변 로딩 범위를 `loadGridScale` 기반으로 설정하도록 했다.
- 청크 로딩 순서는 플레이어 중심 2x2 청크 기준 거리순 오프셋 테이블을 사용하도록 했다.
- runtime data 범위와 render 범위를 분리하고, render 범위 밖의 청크는 단계적으로 언로드하도록 했다.

## 월드 생성
- 높이맵 기반 terrain 생성 로직을 추가했다.
- FastNoise2를 외부 라이브러리로 추가하고, 4D simplex noise 기반 타일링 가능한 높이맵을 도입했다.
- 도메인 워핑 파라미터를 config로 노출했다.
- 높이 LUT 생성용 `pyw` 도구를 추가했다.
- 표층은 grass/dirt/rock 구조로 생성하도록 했다.
- bedrock, plant, tree 생성 규칙을 추가했다.
- 나무 feature가 청크 경계를 넘는 문제를 처리하기 위해 feature write/finalize 흐름을 설계했다.

## 블럭 데이터
- 블럭 정의를 JSON 기반으로 관리하도록 했다.
- block id는 0 air, 1 rock, 2 grass, 3 dirt, 4 sand, 5 sandstone, 6 mud, 7 clay, 8 trunk, 9 leaves, 10000 plant, 65535 bedrock으로 시작했다.
- 렌더 타입, AO 적용 여부, 충돌 여부, face occlusion, alpha mode, mip distance scale 등을 블럭 정의에 포함했다.
- cube와 cross 렌더 타입을 구분했다.

## 플레이어와 입력
- 플레이어 위치 기준을 발밑 중앙으로 정했다.
- 1인칭 눈높이는 `y + 1.5625`로 정했다.
- 1인칭, 3인칭 후방, 3인칭 전방 시점을 F5로 전환하도록 했다.
- fly mode와 ground mode를 F 키로 전환하도록 했다.
- WASD 이동, space 점프/상승, shift 하강, ESC 종료, 마우스 캡처/해제를 추가했다.
- 플레이어 박스 콜라이더와 지형 충돌을 추가했다.
- 20Hz 물리 tick과 보간 기반 이동 구조를 적용했다.
- 좌클릭 블럭 파괴, 우클릭 rock 배치, 1인칭 카메라 기준 raycast, 선택 블럭 outline을 추가했다.

## 저장과 로드
- 저장 경로를 `saves/world/regions`로 정했다.
- region 파일 확장자는 `.region`으로 정했다.
- region은 16x16 청크 단위로 구성하고, 4096 byte sector 기반 header를 사용하도록 했다.
- 청크 payload는 RLE와 LZ4 literal block 형식으로 저장하도록 했다.
- save worker를 추가하고, 언로드/종료 시 저장하도록 했다.
- 저장된 clean revision은 재저장하지 않는 정책을 추가했다.
- region header cache를 추가해 반복 로드 확인 비용을 줄였다.

## 디버그와 프로파일링
- 좌상단 FPS, 위치, yaw/pitch/방향 표시를 추가했다.
- 우상단 버전, Vulkan/driver, 해상도, CPU/GPU/VRAM 관련 정보를 표시했다.
- chunk/job/save/load 상태와 terrain draw/faces/quads 통계를 표시했다.
- 우하단 peak profiler를 추가하고, R 키로 재측정하도록 했다.
- 로딩, 언로드, retire, ensure runtime, save/load 병목을 단계별로 확인했다.

## 주요 결정
- 빌드는 반드시 사용자가 직접 수행한다.
- 구현은 가능한 단순하게 하되, 렌더링/월드/입력/저장 책임은 분리한다.
- 미래 기능을 위해 과다구현하지 않고, 실제로 필요한 시점에 구조를 확장한다.
- 저장 포맷은 초기 개발 중이므로 아직 호환성을 강하게 보장하지 않는다.
- terrain 최적화는 0.0.0.0에서 일단 중단하고, 0.0.0.1부터는 기능 안정화와 저장/로드 검증을 우선한다.

## 남은 문제
- 저장/불러오기 안정성은 추가 검증이 필요하다.
- 나무/식생 feature가 경계와 언로드 상황에서 항상 안정적인지 계속 확인해야 한다.
- terrain quad record 최적화는 빌드/실행 후 렌더링 품질과 성능 변화를 확인해야 한다.
- 블럭 편집 후 리메시와 저장 상태가 모든 상황에서 일관적인지 추가 테스트가 필요하다.
