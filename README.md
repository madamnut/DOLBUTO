# DOLBUTO

Vulkan / C++

## 개발

- Visual Studio에서 이 폴더를 CMake 프로젝트로 연다.
- `DOLBUTO` 대상을 선택한다.
- 빌드는 Visual Studio에서 사용자가 직접 수행한다.

## 포터블 배포

- Debug 빌드는 기존 개발 폴더의 `assets`, `config`, `saves/world`를 직접 사용한다.
- Release/Portable 빌드는 실행 파일 옆의 `assets`, `config`, `shaders`, `saves/world`를 사용한다.
- 포터블 배포본은 Release 구성에서 `DOLBUTOPortable` 타깃을 빌드해서 생성한다.
- 생성 위치는 `dist/DOLBUTO_0.0.0.0/`이다.
- 배포 전에는 해당 dist 폴더 안의 `DOLBUTO.exe`를 직접 실행해서 확인한다.

## 조작

- 마우스: 카메라 회전
- 게임 화면 클릭: 마우스 캡처
- 휠 클릭: 마우스 캡처 해제
- W/S: 전후 이동
- A/D: 좌우 이동
- Space: fly 모드에서는 상승, ground 모드에서는 점프
- Shift: fly 모드에서 하강
- F: fly 모드와 ground 모드 전환
- F2: 스크린샷 저장
- F3: 디버그 텍스트 표시 전환
- F4: 지형 wireframe 표시 전환
- F5: 1인칭/3인칭 후방/3인칭 전방 시점 전환
- F11: 전체화면 전환
- 좌클릭: 선택한 블럭 파괴
- 우클릭: 선택한 위치에 rock 블럭 배치
- Esc: 게임 종료

## 디버그 텍스트

- 좌상단: FPS, 프레임 시간, 플레이어 위치, yaw/pitch, 4방위 시야 방향
- 우상단: `DOLBUTO 0.0.0.0`
- 우상단 버전 아래: CPU/GPU 이름, Vulkan API 버전, driver 버전, 해상도
- 우상단 하단: terrain draw, face, quad 통계
- 좌하단: 청크 로딩, 작업 큐, 저장/로드 상태
- 우하단: 누적 peak profiler
