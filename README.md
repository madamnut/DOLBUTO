# DOLBUTO

Vulkan / C++

## 개발

- Visual Studio에서 이 폴더를 CMake 프로젝트로 연다.
- `DOLBUTO` 대상을 선택한다.
- 빌드는 Visual Studio에서 사용자가 직접 수행한다.

## 포터블 배포

- Debug 빌드는 기존 개발 폴더의 `assets`, `config`, `saves/world`를 직접 사용한다.
- Release 빌드는 실행 파일 옆의 `assets`, `config`, `shaders`, `saves/world`를 사용한다.
- 별도 포터블 생성 타깃은 사용하지 않는다.
- Release 출력 폴더 자체를 폴더째 이동 가능한 포터블 실행 단위로 본다.
- 배포 전에는 Release 출력 폴더 안의 `DOLBUTO.exe`를 직접 실행해서 확인한다.

## 조작

- 마우스: 카메라 회전
- 게임 화면 클릭: 마우스 캡처
- 휠 클릭: 마우스 캡처 해제
- W/S: 전후 이동
- A/D: 좌우 이동
- Space: fly 모드에서는 상승, ground 모드에서는 점프
- Space 더블탭: Sandbox 모드에서 fly/ground 전환
- Shift: fly 모드에서 하강
- Shift: ground 모드에서 웅크리기
- Ctrl: ground 모드에서 달리기, fly 모드에서 가속
- Enter: 채팅/명령어 입력 열기
- F: 바라보는 드랍 아이템 획득
- Q: 선택된 핫바 아이템 1개 버리기
- Shift+Q: 선택된 핫바 아이템 스택 전체 버리기
- M: 배치 모드 rock/glowing_rock 전환
- E: 인벤토리 열기/닫기
- F1: 핫바 HUD와 크로스헤어 표시 전환
- F2: 스크린샷 저장
- F3: 디버그 텍스트 표시 전환
- F4: 지형 wireframe 표시 전환
- F5: 1인칭/3인칭 후방/3인칭 전방 시점 전환
- F6: 기후 오버레이 전환
- F11: 전체화면 전환
- 좌클릭: 선택한 블럭 파괴
- 우클릭: 선택한 위치에 현재 배치 모드 블록 배치
- Esc: 게임에서는 일시정지, 인벤토리/일시정지에서는 게임 복귀

## 디버그 텍스트

- 좌상단: FPS, 프레임 시간, 플레이어 위치, yaw/pitch, 4방위 시야 방향, LOOKAT, 기후, 시간, 시드
- 우상단: `DOLBUTO 0.0.0.3`
- 우상단 버전 아래: CPU/GPU 이름, Vulkan API 버전, driver 버전, 해상도
- 우상단 하단: VRAM, terrain draw, face, quad 통계
- 하단 디버그 텍스트와 peak profiler는 현재 표시하지 않는다.
