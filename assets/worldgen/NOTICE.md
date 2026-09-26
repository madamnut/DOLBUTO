# 지형 프리셋 출처 (AI 컨텍스트)

- `default.json`의 `splines.offset`, `splines.factor`, `splines.jaggedness` 수치는 Snowcapped의 `VanillaSplines.ts`에서 가져왔다. C/E의 0 기울기와 W/PV 중첩 제어점·값·기울기를 보존했다. 빈 칸이 있는 표 형태로 저장한다.
- 고정 참조: https://github.com/jacobsjo/snowcapped/blob/b16f532359d82ddb17cd0911a5d1420cb7560986/src/main/Vanilla/VanillaSplines.ts
- Snowcapped의 라이선스는 vanilla splines / biome configuration을 Microsoft 소유 자료로 따로 명시한다. 이 데이터가 Snowcapped 코드의 MIT 라이선스로 재라이선스되었다고 표기하지 않는다. 자료의 원 권리자는 Mojang / Microsoft다.
- 라이선스 참조: https://github.com/jacobsjo/snowcapped/blob/b16f532359d82ddb17cd0911a5d1420cb7560986/LICENSE.txt
- `src/world/terrain_preset.inc`에도 같은 데이터를 내장한다. `tools/import-terrain-preset.py`는 고정 커밋에서 이 두 파일을 갱신하는 개발용 가져오기 도구다. 게임은 네트워크나 이 도구를 사용하지 않는다.
- 평가기와 편집 UI는 이 프로젝트에서 새로 구현했다. Minecraft 코드 전체를 복제하지 않았으며 주기적 Perlin, 밀도 진폭, 해수면 기준 변환, jagged 노이즈의 샘플 간격은 프로젝트에 맞춘다.
