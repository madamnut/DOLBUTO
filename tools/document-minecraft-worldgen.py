"""Rebuild the numeric appendices of the Minecraft world-generation reference.

Reads the already downloaded vanilla client JAR; no network, game launch or tests.
Only the marked appendix of the requested Markdown document is replaced.
"""
from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- BEGIN GENERATED MINECRAFT WORLDGEN -->'
END = '<!-- END GENERATED MINECRAFT WORLDGEN -->'


def number(value):
    return str(value)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


def spline_stats(spline):
    count = Counter(splines=1, points=len(spline['points']))
    previous = None
    for point in spline['points']:
        location = point['location']
        if previous is not None and location <= previous:
            raise ValueError('Spline control points are not strictly ascending')
        previous = location
        if isinstance(point['value'], dict):
            count.update(spline_stats(point['value']))
        else:
            count['leaves'] += 1
    return count


def spline_lines(spline, indent='', address='root'):
    yield f"{indent}{address}: axis={spline['coordinate']}"
    for i, point in enumerate(spline['points']):
        loc, der = number(point['location']), number(point['derivative'])
        value = point['value']
        path = f'{address}/p{i}'
        if isinstance(value, dict):
            yield f'{indent}  p{i}: x={loc}; derivative={der}; value=Spline'
            yield from spline_lines(value, indent + '    ', path)
        else:
            yield f'{indent}  p{i}: x={loc}; derivative={der}; value={number(value)}'


def expression(value, owner, indent=''):
    """Lossless field tree except spline bodies, which have their full own atlas."""
    if isinstance(value, dict):
        if value.get('type') == 'minecraft:spline':
            yield indent + f'SPLINE[{owner}] (full tree in Appendix A)'
            return
        for key, child in value.items():
            if isinstance(child, (dict, list)):
                yield indent + key + ':'
                yield from expression(child, owner, indent + '  ')
            else:
                yield indent + f'{key}: {scalar(child)}'
    elif isinstance(value, list):
        for i, child in enumerate(value):
            yield indent + f'[{i}]:'
            yield from expression(child, owner, indent + '  ')
    else:
        yield indent + scalar(value)


def scalar(value):
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, (Decimal, int)):
        return number(value)
    return str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jar', type=Path, default=ROOT / 'ref/sources/MCP-Reborn/projects/mcp/build/mcp/downloadClient/client.jar')
    parser.add_argument('--document', type=Path, default=ROOT / 'docs/minecraft-world-generation.md')
    args = parser.parse_args()
    document = args.document.read_text(encoding='utf-8')
    if document.count(BEGIN) != 1 or document.count(END) != 1:
        raise ValueError('Document must contain exactly one pair of appendix markers')
    output = []
    with zipfile.ZipFile(args.jar) as archive:
        def read(name):
            return json.loads(archive.read(name), parse_float=Decimal)

        version = read('version.json')['id']
        if version != '26.2':
            raise ValueError(f'Narrative describes 26.2; refusing to replace with {version}')
        prefix = 'data/minecraft/worldgen/'
        densities = {
            name[len(prefix + 'density_function/'):-5]: read(name)
            for name in sorted(archive.namelist())
            if name.startswith(prefix + 'density_function/') and name.endswith('.json')
        }
        roots = [(name, n['spline']) for name, data in densities.items()
                 for n in nodes(data) if n.get('type') == 'minecraft:spline']
        expected = {f'{world}/{kind}' for world in ('overworld', 'overworld_large_biomes', 'overworld_amplified')
                    for kind in ('offset', 'factor', 'jaggedness')}
        if {name for name, _ in roots} != expected or len(roots) != 9:
            raise ValueError('Unexpected spline inventory; review narrative before regenerating')
        roots.sort(key=lambda item: (['overworld', 'overworld_large_biomes', 'overworld_amplified'].index(item[0].split('/')[0]),
                                    ['offset', 'factor', 'jaggedness'].index(item[0].split('/')[1])))
        output += ['## 부록 A. 9개 지형 스플라인의 완전 전개', '',
                   f'입력: Minecraft **{version}**의 로컬 `client.jar`. 아래 값은 배포 JAR의 JSON 숫자 표기를 그대로 보존한다.', '',
                   '생성기: `python tools/document-minecraft-worldgen.py`. 네트워크 없이 이미 받은 JAR를 읽는다.', '',
                   '각 `p번호`는 해당 노드 안에서의 제어점 순서다. `root/p5/p0`처럼 부모 경로로 위치를 추적할 수 있다. '
                   '`x`는 블록 X가 아니라 해당 입력 축의 제어점 위치이고, `derivative`는 그 축에 대한 접선 기울기다. '
                   '중복 하위 트리도 참조로 생략하지 않고 매 위치마다 끝까지 반복 출력한다.', '',
                   '| 루트 | 펼쳐진 spline 노드 | 제어점 | 상수 잎 |', '| --- | ---: | ---: | ---: |']
        total = Counter()
        for name, spline in roots:
            count = spline_stats(spline)
            total.update(count)
            output.append(f"| `{name}` | {count['splines']} | {count['points']} | {count['leaves']} |")
        output += [f"| 합계(반복 포함) | {total['splines']} | {total['points']} | {total['leaves']} |", '']
        for index, (name, spline) in enumerate(roots, 1):
            output += [f'### A{index}. {name}', '', f'JAR 경로: `{prefix}density_function/{name}.json`', '', '```text']
            output.extend(spline_lines(spline))
            output += ['```', '']

        output += ['## 부록 B. Density Function 전체 등록 그래프', '',
                   '등록 이름을 따라가면 연산 그래프 전체를 복원할 수 있다. `minecraft:overworld/...` 같은 문자열은 '
                   '다른 등록 함수 참조다. `noise` 필드의 문자열은 부록 D의 노이즈 파라미터 참조이며 함수 참조와 구분한다. '
                   '`SPLINE[이름]`만 부록 A의 완전 전개를 사용하고, 나머지 필드·상수·캐시 표식은 모두 표시한다.', '',
                   f'이 JAR의 density_function 레지스트리 JSON: **{len(densities)}개**.', '']
        for name, data in densities.items():
            output += [f'### B. {name}', '', '```text']
            output.extend(expression(data, name))
            output += ['```', '']

        output += ['## 부록 C. 모든 기본 Noise Settings의 라우터 연결', '',
                   '아래는 JAR의 모든 noise_settings JSON에 대해 surface_rule과 spawn_target 이외의 필드를 펼친 것이다. '
                   'surface_rule은 본문 표면 처리 절, spawn_target은 바이옴·스폰 절에서 별도로 설명한다. '
                   'noise_router의 모든 15개 필드와 인라인 연산은 생략하지 않는다.', '']
        settings_names = [n for n in sorted(archive.namelist()) if n.startswith(prefix + 'noise_settings/') and n.endswith('.json')]
        for name in settings_names:
            key = name[len(prefix + 'noise_settings/'):-5]
            data = read(name)
            data = {k: v for k, v in data.items() if k not in ('surface_rule', 'spawn_target')}
            output += [f'### C. {key}', '', '```text']
            output.extend(expression(data, key))
            output += ['```', '']

        output += ['## 부록 D. 모든 기본 NormalNoise 파라미터', '',
                   '`firstOctave`와 진폭 배열은 실제 JAR 데이터다. 배열 중 0은 해당 옥타브가 기여하지 않음을 뜻한다. '
                   '각 함수가 붙이는 xz/y 배율은 부록 B·C와 함께 봐야 한다.', '',
                   '| 노이즈 ID | firstOctave | amplitudes (순서 유지) |', '| --- | ---: | --- |']
        noises = [n for n in sorted(archive.namelist()) if n.startswith(prefix + 'noise/') and n.endswith('.json')]
        for name in noises:
            data = read(name)
            amps = ', '.join(number(n) for n in data['amplitudes'])
            output.append(f"| `{name[len(prefix + 'noise/'):-5]}` | {data['firstOctave']} | `[{amps}]` |")
        output += ['', '## 부록 E. 자료 식별·완전성 기록', '',
                   f'- JAR SHA256: `{hashlib.sha256(args.jar.read_bytes()).hexdigest()}`',
                   f'- spline 루트 {len(roots)}개, density_function 파일 {len(densities)}개, noise_settings 파일 {len(settings_names)}개, noise 파일 {len(noises)}개.',
                   '- 완전성의 범위: 기본 지형 스플라인 9개는 모든 반복·중첩·위치·기울기·상수 잎까지 전개했다. '
                   '지형 density_function 등록 그래프와 기본 noise_router도 전개했다. 모든 구조물 템플릿의 블록, '
                   '모든 바이옴 장식 목록을 전수 복제한다는 뜻은 아니다.',
                   '- 이 문서는 시드에 독립적인 함수 정의다. 특정 시드의 (x,y,z) 노이즈 결과나 전체 맵을 열거한 것은 아니다.',
                   '- JSON 숫자는 float 저장값을 왕복할 수 있는 표기다. 계산을 재현할 때 CubicSpline의 float32와 '
                   'DensityFunction의 double 경계를 지켜야 한다. 문서에서 수치를 반올림해 다시 저장하지 않는다.', '']
        for relative in ('data/worldgen/TerrainProvider.java', 'world/level/levelgen/NoiseRouterData.java', 'util/CubicSpline.java'):
            source = ROOT / 'ref/sources/MCP-Reborn/src/main/java/net/minecraft' / relative
            output.append(f'- `{relative}` SHA256: `{hashlib.sha256(source.read_bytes()).hexdigest()}`')
        output.append('')
    before, rest = document.split(BEGIN)
    _, after = rest.split(END)
    args.document.write_text(before + BEGIN + '\n\n' + '\n'.join(output) + '\n' + END + after, encoding='utf-8', newline='\n')
    print(json.dumps({'version': version, 'spline_roots': len(roots), 'expanded': dict(total),
                      'density_functions': len(densities), 'noise_settings': len(settings_names),
                      'noises': len(noises), 'appendix_lines': len(output)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
