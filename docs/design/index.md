# 설계 문서 목차

이 디렉토리는 DOLBUTO의 현재 구현 상태를 정리하는 옵시디언 문서 공간이다.
날짜별 작업 로그가 아니라, 구현이 바뀌면 관련 문서를 갱신하는 현재형 설계 노트로 관리한다.

## 핵심 문서

- [[project-overview]]
- [[build-and-distribution]]
- [[runtime-paths]]
- [[rendering]]
- [[chunk-system]]
- [[world-generation]]
- [[save-load]]
- [[player]]
- [[block-data]]
- [[debug-profiling]]

## 현재 기준

- 현재 버전 표기: `DOLBUTO 0.0.0.1`
- 개발 언어/그래픽스: C++20 / Vulkan
- 윈도우/입력: GLFW
- 빌드: CMake / Ninja / Visual Studio
- 월드 렌더링: 레이마칭 제거 후 메쉬 렌더링
- 청크 크기: `16 x 512 x 16`
- 서브청크 크기: `16 x 16 x 16`
- 저장 위치: `saves/world/regions`
