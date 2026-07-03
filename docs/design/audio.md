# 오디오

오디오는 OpenAL Soft를 사용한다.
현재 구현된 범위는 짧은 효과음 재생과 단순 배경음악 재생이다.

## 디렉터리

```text
assets/audio/sfx
assets/audio/music
assets/audio/ambience
```

- `sfx`: 짧은 효과음. 현재는 PCM 16-bit WAV를 사용한다.
- `music`: 배경음악. 현재는 OGG 파일을 사용한다.
- `ambience`: 바람, 물, 동굴 울림 같은 환경 배경음용으로 사용한다.

## 현재 효과음

```text
assets/audio/sfx/Break.wav
assets/audio/sfx/Button_Click.wav
assets/audio/sfx/Place.wav
assets/audio/sfx/Pop.wav
assets/audio/sfx/walk/walk0.wav ~ walk4.wav
```

- `Break.wav`: 블록 파괴 성공 시 재생한다.
- `Button_Click.wav`: RmlUi 버튼 click 이벤트에서 재생한다.
- `Place.wav`: 블록 설치 성공 시 재생한다.
- `Pop.wav`: 드랍 아이템이 플레이어 인벤토리에 실제로 들어간 시점에 재생한다.
- `walk/*.wav`: 플레이어 발소리로 사용하며, 걸음마다 하나를 랜덤 선택하고 기본 gain `0.8`로 재생한다.

## 재생 방식

- 블록 파괴음은 3D 위치 사운드로 재생한다.
- 블록 파괴음 위치는 파괴된 블록의 중심인 `x, y + 0.5, z`를 사용한다.
- 블록 설치음은 3D 위치 사운드로 재생한다.
- 블록 설치음 위치는 설치된 블록의 중심인 `x, y + 0.5, z`를 사용한다.
- 3D 위치 사운드용 WAV가 stereo이면 로드 시 mono로 다운믹스한다.
- 버튼 클릭음은 2D 사운드로 재생한다.
- 아이템 획득음은 2D 사운드로 재생한다.
- 플레이어 발소리는 2D 사운드로 재생한다.
- 발소리는 ground 상태에서 물에 닿지 않은 채 실제 수평 이동 거리가 `1.8`블록 누적될 때마다 재생한다.
- OpenAL listener는 매 프레임 카메라 위치와 방향으로 갱신한다.
- 현재 WAV 로더는 PCM 16-bit mono/stereo만 지원한다.
- 음악은 `assets/audio/music` 아래의 `.ogg` 또는 `.wav` 파일을 사용한다.
- 사용자 음량 설정은 config directory의 `settings.json`에 저장한다.
- 현재 저장 키는 `audio.bgmVolume`, `audio.sfxVolume`이며 값 범위는 `0.0`부터 `1.0`까지다.
- 시작 시 음악 파일은 디코딩하지 않고 파일 목록만 스캔한다.
- OGG 음악은 `stb_vorbis` 파일 디코더로 스트리밍한다.
- WAV 음악은 현재 재생 직전에 단일 OpenAL buffer로 로드하는 예외 경로로 처리한다.
- 음악은 별도의 music source 하나로 재생한다.
- OGG 음악 스트리밍은 2초 분량 OpenAL buffer 3개를 큐에 넣고, 재생 중 처리 완료된 buffer를 다시 채우는 방식이다.

## 음악 스케줄

- 로비 계열 화면과 인게임 화면은 같은 `assets/audio/music` 목록을 사용한다.
- 로비 계열 화면은 로비, 월드 선택, 월드 생성 화면을 포함한다.
- 인게임 화면은 게임, pause, inventory 화면을 포함한다.
- 로비 계열 화면에서 인게임 화면으로 전환하거나 반대로 전환하면 기존 음악은 즉시 정지한다.
- 씬 전환 시 이전 씬의 음악 재생 위치, 대기 시간, 마지막 곡 정보는 저장하지 않는다.
- 로비 계열 화면에 진입하면 음악을 즉시 하나 랜덤 재생한다.
- 인게임 화면에 진입하면 `10초 ~ 60초` 사이의 랜덤 대기 후 음악을 하나 랜덤 재생한다.
- 음악이 끝나면 다시 `10초 ~ 60초` 사이의 랜덤 대기 후 다음 음악을 재생한다.
- 음악 파일이 둘 이상이면 직전에 재생한 곡을 가능한 한 피한다.

## 런타임 구조

- 오디오 런타임 구현은 `src/audio/AudioSystem.h`와 `src/audio/AudioSystem.cpp`에 둔다.
- `AudioSystem`은 시작 시 OpenAL device/context를 만든다.
- SFX WAV 파일은 `AudioSystem` 초기화 시 OpenAL buffer로 로드한다.
- 음악 파일은 `AudioSystem` 초기화 시 경로와 형식만 기록한다.
- 짧은 효과음은 `AudioSystem` 내부의 16개 source pool을 순환 사용한다.
- source가 다시 필요하면 기존 재생을 멈추고 새 buffer를 연결해 재생한다.
- 음악은 효과음 source pool과 별도의 source를 사용한다.
- `Renderer`는 매 프레임 listener 위치/방향, 현재 음악 씬, 버튼/블록/아이템 이벤트 발생 시점만 `AudioSystem`에 전달한다.
- 현재 음악 씬 분류가 바뀌면 music source를 정지하고 새 씬의 대기 타이머를 다시 잡는다.
- OGG 디코딩은 OpenAL 런타임 확장에 의존하지 않는다.
- OGG 음악 스트림은 씬 전환, 곡 종료, 종료 처리 시 디코더와 큐 buffer를 닫는다.
