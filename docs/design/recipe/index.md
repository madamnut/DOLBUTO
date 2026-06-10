# recipe

이 디렉터리는 아이템 결과를 만드는 레시피 데이터를 정리한다.
레시피는 플레이어가 직접 선택해 즉시 실행하는 상호작용 레시피와, 환경/장치 상태에 의해 시간 경과로 진행되는 processing 레시피로 나뉜다.

## 문서

- [[interactions]]
- [[processings]]

## 데이터 파일

```text
assets/data/recipes/interactions.json
assets/data/recipes/processings/
```

`processings/` 아래의 JSON 파일명은 process key로 사용한다.
예를 들어 `pyrolysis.json`은 `pyrolysis` processing 레시피 묶음이고, `smelt.json`은 도가니 용해 레시피 묶음이다.

관련 문서: [[../item-data]], [[../item-interactions]], [[../block-data]]
