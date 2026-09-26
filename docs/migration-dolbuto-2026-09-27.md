# DOLBUTO 로컬 교체 — 2026-09-27

AI용 작업 컨텍스트. 사용자가 기존 DOLBUTO 백업 후 현재 sandbox 프로젝트로 교체를 승인했다. GitHub push는 사용자 직접 수행. 구현 변경이나 새 커밋 생성은 범위가 아니다.

## 경로와 보존

- 현재 프로젝트: C:/Users/PC/Desktop/YD_Unity/DOLBUTO
- 보존한 원본 sandbox: C:/Users/PC/Desktop/YD_Unity/sandbox
- 이전 DOLBUTO 전체 백업: C:/Users/PC/Desktop/YD_Unity/DOLBUTO-backup-20260927-024333
- 백업 전 파일별 SHA256·크기 manifest: 위 경로 + .manifest.json
- 복사 로그: 위 경로 + .copy.log
- 복사된 과거 빌드 캐시 별도 보관: C:/Users/PC/Desktop/YD_Unity/DOLBUTO-imported-build-20260927-024333

원래 DOLBUTO는 같은 디스크에서 폴더째 Move-Item으로 백업해 .git/ignored/untracked/saves/config까지 보존했다. 이동 전 파일별 해시 기록 후 백업의16821파일 존재·크기 일치 확인. .git HEAD/config/index와 config4개 및 saves4개의 SHA256도 일치. 총5.321GiB. old DOLBUTO는 진행 전 Git추적변경없고실행프로세스없음.

새 경로에 기존.git 복사 후 sandbox 전체를복사했다. robocopy의첫로그인자형식오류는원본손실없이수정후재실행. 복사실패0,36876파일/10.055GiB. slash형식의절대제외경로가매칭되지않아복사된build는재사용하지않았다. 생성캐시재귀삭제는자동승인검토에서차단되어삭제하지않고명시적경로검증후형제폴더로옮겼다. 새 build/release는빈상태에서생성한다. 원본sandbox의build와junction에는손대지않았다.

## Git 상태

main, HEAD c9fd48b6a63f54e55a04cbf1e969a3541cc1ddc1, origin https://github.com/madamnut/DOLBUTO.git 유지. 원격통신/fetch/push/commit/add/reset은수행하지않았다. 대규모삭제·추가가보이는것은이전프로젝트파일을새프로젝트로교체한작업트리차이다. .cache/.tools/build/out/ref/MCP-Reborn은새.gitignore로제외. 기존저장소가추적한옛파일은삭제상태로남아사용자가교체커밋에포함해야한다. Git이력재작성없음.

## 소스와 설정 검증

복사 직후 src/assets/shaders/tools/cmake/docs/third_party 및 루트파일245개를 원본과 SHA256 대조해 불일치0. 이후 AGENTS/이문서/verification에 이관맥락만추가한다. 게임실행파일명 sandbox.exe, out/Sandbox 및에디터명은현재구현대로유지한다. 기존역사문서의sandbox절대경로는당시기록이며현재명령은DOLBUTO경로를사용한다.

out/Sandbox/settings.json SHA256: 1597232D238F770788C3E86C69B4C331B3F0310CC30ED52DFD6E4A7558253D62
out/Sandbox/worldgen.json SHA256: E6991E0A058F663EE92C22BC8CAFDC161F62605792BC649E28976E6C935FCDB8

해당파일의소스/새프로젝트바이트일치확인. 실제확정·초안규칙을기본값으로되돌리지않는다. 새참고자료를추적후보로봤을때50MiB초과파일없음. Minecraft로컬참고코드는Git제외유지.

## 빌드·실행

새경로 tools/environment.ps1 → cmake --preset release → cmake --build --preset release --parallel 8 → tools/package.ps1. 새경로 전체빌드·패키징 성공. 격리 게임120프레임 실행exit0/Vulkan오류0/UI문제0/텍스처실패0, 에디터state·curve API/종료exit0 확인. 기존Vulkan경고14개와SDL컴파일경고1개는잔존. 사용자배포설정해시재확인일치. 구체적인 결과는 docs/verification.md에 기록했다. 자동테스트/CTest/컴퓨터유즈/합성입력은금지. 실행확인은별도런타임사본에서기존CLI사용으로설정원본을보존한다.
