"""Rebuild numeric appendices for the two focused Overworld reference documents.

Uses the existing downloaded 26.2 client JAR, with no network or game execution.
Narrative sections outside the markers are preserved.
"""
from collections import Counter
from decimal import Decimal
import hashlib
import importlib.util
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('worldgen_document', Path(__file__).with_name('document-minecraft-worldgen.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
PRESETS = ('overworld', 'large_biomes', 'amplified')
WORLD_KEYS = ('overworld', 'overworld_large_biomes', 'overworld_amplified')


def exact_json(value, level=0):
    pad = '  ' * level
    inner = '  ' * (level + 1)
    if isinstance(value, dict):
        return '{\n' + ',\n'.join(inner + json.dumps(k) + ': ' + exact_json(v, level+1) for k, v in value.items()) + '\n' + pad + '}' if value else '{}'
    if isinstance(value, list):
        return '[\n' + ',\n'.join(inner + exact_json(v, level+1) for v in value) + '\n' + pad + ']' if value else '[]'
    if isinstance(value, Decimal):
        return str(value)
    return json.dumps(value, ensure_ascii=False)


def references(value, names):
    found = set()
    if isinstance(value, str) and value.startswith('minecraft:') and value[10:] in names:
        found.add(value[10:])
    elif isinstance(value, dict):
        for child in value.values():
            found.update(references(child, names))
    elif isinstance(value, list):
        for child in value:
            found.update(references(child, names))
    return found


def noise_references(value, names):
    # A condition type such as minecraft:temperature is not a noise reference.
    # Only explicit noise fields identify entries in the NormalNoise registry.
    found = set()
    for node in shared.nodes(value):
        noise = node.get('noise')
        if isinstance(noise, str) and noise.startswith('minecraft:') and noise[10:] in names:
            found.add(noise[10:])
    return found


def replace(document, marker, lines):
    path = ROOT / 'docs' / document
    source = path.read_text(encoding='utf-8')
    start, end = f'<!-- BEGIN {marker} -->', f'<!-- END {marker} -->'
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f'Missing or repeated markers in {path}')
    before, rest = source.split(start)
    _, after = rest.split(end)
    path.write_text(before + start + '\n\n' + '\n'.join(lines) + '\n\n' + end + after, encoding='utf-8', newline='\n')


def main():
    jar = ROOT / 'ref/sources/MCP-Reborn/projects/mcp/build/mcp/downloadClient/client.jar'
    digest = hashlib.sha256(jar.read_bytes()).hexdigest()
    with zipfile.ZipFile(jar) as z:
        def read(name):
            return json.loads(z.read(name), parse_float=Decimal)
        if read('version.json')['id'] != '26.2':
            raise ValueError('These narratives require Minecraft 26.2')
        prefix = 'data/minecraft/worldgen/'
        densities = {n[len(prefix+'density_function/'):-5]: read(n) for n in sorted(z.namelist())
                     if n.startswith(prefix+'density_function/') and n.endswith('.json')}
        noises = {n[len(prefix+'noise/'):-5]: read(n) for n in sorted(z.namelist())
                  if n.startswith(prefix+'noise/') and n.endswith('.json')}
        settings = {name: read(prefix+'noise_settings/'+name+'.json') for name in PRESETS}
        selected = set()
        pending = set().union(*(references(data['noise_router'], densities) for data in settings.values()))
        while pending:
            key = pending.pop()
            if key in selected:
                continue
            selected.add(key)
            pending.update(references(densities[key], densities) - selected)
        if any(k.startswith(('end/', 'nether/')) for k in selected):
            raise ValueError('Unexpected cross-dimension dependency')
        roots = []
        for world in WORLD_KEYS:
            for kind in ('offset', 'factor', 'jaggedness'):
                name = world+'/'+kind
                found = [n['spline'] for n in shared.nodes(densities[name]) if n.get('type')=='minecraft:spline']
                if len(found) != 1 or name not in selected:
                    raise ValueError('Unexpected terrain spline layout: '+name)
                roots.append((name, found[0]))
        all_splines = sum(n.get('type')=='minecraft:spline' for k in selected for n in shared.nodes(densities[k]))
        if all_splines != 9 or any(n.get('type')=='minecraft:spline' for s in settings.values() for n in shared.nodes(s['noise_router'])):
            raise ValueError('Unexpected additional spline; update narrative')
        counts = Counter()
        lines = ['## 부록 A. 오버월드 지형 스플라인 9개 완전 전개', '',
                 '일반·대형 바이옴·증폭을 모두 포함한다. 반복 가지도 생략하지 않는다. '
                 '`x`는 입력 축의 제어점 위치, `derivative`는 해당 축에 대한 기울기이며, '
                 '`root/p번호`는 중첩 경로다. JSON 숫자는 Decimal로 읽어 자릿수를 보존한다.', '',
                 '| 루트 | 중첩 spline 노드 | 제어점 | 상수 잎 |', '| --- | ---: | ---: | ---: |']
        for name, spline in roots:
            c = shared.spline_stats(spline)
            counts.update(c)
            lines.append(f"| `{name}` | {c['splines']} | {c['points']} | {c['leaves']} |")
        lines += [f"| 합계(반복 포함) | {counts['splines']} | {counts['points']} | {counts['leaves']} |", '']
        for i, (name, spline) in enumerate(roots, 1):
            lines += [f'### A{i}. {name}', '', '```text', *shared.spline_lines(spline), '```', '']
        lines += ['## 부록 B. 초기 지형이 실제 참조하는 밀도 함수 전체', '',
                  f'세 오버월드 noise_router에서 등록 함수 참조를 끝까지 따라간 **{len(selected)}개**다. '
                  '이름 참조는 이 부록의 해당 정의로 연결되고 SPLINE은 부록 A의 완전 트리로 연결된다. '
                  '캐시·보간·연산 순서·상수 필드를 생략하지 않는다.', '']
        for name in sorted(selected):
            lines += [f'### B. {name}', '', '```text', *shared.expression(densities[name], name), '```', '']
        lines += ['## 부록 C. 오버월드 3개 프리셋의 초기 생성 설정', '',
                  'surface_rule은 표면 문서에서 다루고 spawn_target은 첫 스폰 탐색용이므로 여기서는 제외한다. '
                  '나머지 기본 블록·유체·noise cell·라우터 15필드와 각 활성 플래그는 전부 표시한다.', '']
        for name, data in settings.items():
            filtered = {k:v for k,v in data.items() if k not in ('surface_rule', 'spawn_target')}
            lines += [f'### C. {name}', '', '```text', *shared.expression(filtered, name), '```', '']
        used_noises = set().union(*(noise_references(densities[k], noises) for k in selected),
                                 *(noise_references(s['noise_router'], noises) for s in settings.values()))
        lines += ['## 부록 D. 초기 지형 그래프의 NormalNoise 파라미터', '',
                  '노이즈 ID, firstOctave, 진폭 배열이다. 호출별 좌표 배율은 부록 B·C에 있다. '
                  'BlendedNoise는 이 NormalNoise 레지스트리와 별도다. 표면 전용 노이즈는 표면 문서에 싣는다.', '',
                  '| ID | firstOctave | amplitudes |', '| --- | ---: | --- |']
        for name in sorted(used_noises):
            n = noises[name]
            lines.append(f"| `{name}` | {n['firstOctave']} | `[{', '.join(map(str,n['amplitudes']))}]` |")
        lines += ['', '## 부록 E. 출처·완전성', '', f'- Minecraft 26.2 client.jar SHA256: `{digest}`',
                  f'- spline 루트9개 / 중첩 노드{counts["splines"]}개 / 제어점{counts["points"]}개 / 상수잎{counts["leaves"]}개.',
                  f'- 연결된 density function {len(selected)}개 / 오버월드 noise settings 3개 / 참조 NormalNoise {len(used_noises)}개.',
                  '- 원본 경로: `data/minecraft/worldgen/{density_function,noise_settings,noise}/`.',
                  '- 재생성: `python tools/document-minecraft-overworld.py`. 두 상세 문서의 부록만 바꾸고 전체 설명서는 수정하지 않는다.']
        replace('minecraft-overworld-base-terrain.md', 'OVERWORLD BASE DATA', lines)

        surface_groups = []
        for name, data in settings.items():
            rule = data['surface_rule']
            group = next((g for g in surface_groups if g[1] == rule), None)
            if group is None:
                surface_groups.append(([name], rule))
            else:
                group[0].append(name)
        surface = ['## 부록 A. 기본 표면 규칙의 전체 트리', '',
                   '배포 JAR의 `noise_settings/<preset>.json`에서 surface_rule을 완전히 전개한 유효 JSON이다. '
                   'sequence 배열 순서가 실행 우선순위다. 조건·상수·block_state의 속성을 빠뜨리지 않는다. '
                   '동일한 세 프리셋은 데이터 동등성을 확인한 뒤 하나로 표시하며, 값이 달라지면 각각 출력한다.', '']
        total_nodes = 0
        for presets, rule in surface_groups:
            encoded = exact_json(rule)
            typed = Counter(n['type'] for n in shared.nodes(rule) if 'type' in n)
            total_nodes += sum(typed.values())
            surface += [f"### A. {' / '.join(presets)}", '',
                        f'규칙 트리 SHA256(이 JSON 표기 기준): `{hashlib.sha256(encoded.encode()).hexdigest()}`', '',
                        '| 규칙/조건 type | 반복 포함 개수 |', '| --- | ---: |']
            surface.extend(f'| `{k}` | {v} |' for k,v in sorted(typed.items()))
            surface += ['', '```json', encoded, '```', '']
        surface_noises = set().union(*(noise_references(s['surface_rule'], noises) for s in settings.values()))
        surface_noises.update(('surface', 'surface_secondary', 'clay_bands_offset', 'badlands_pillar', 'badlands_pillar_roof',
                               'badlands_surface', 'iceberg_pillar', 'iceberg_pillar_roof', 'iceberg_surface'))
        surface += ['## 부록 B. 표면 전용 노이즈 파라미터', '',
                    'surface_rule이 참조하는 노이즈와 SurfaceSystem의 두께·띠·특수 확장용 노이즈를 합친 목록이다. '
                    '이름별 시드 분리·호출 좌표 배율과 함께 읽는다.', '',
                    '| ID | firstOctave | amplitudes |', '| --- | ---: | --- |']
        for name in sorted(surface_noises):
            n = noises[name]
            surface.append(f"| `{name}` | {n['firstOctave']} | `[{', '.join(map(str,n['amplitudes']))}]` |")
        surface += ['', '## 부록 C. 출처·완전성', '', f'- Minecraft 26.2 client.jar SHA256: `{digest}`',
                    f'- 오버월드 프리셋3개 / 서로 다른 surface_rule 트리 {len(surface_groups)}개 / type이 있는 노드 {total_nodes}개(공유 프리셋 중복 제외).',
                    f'- 표면 참조·특수 처리 NormalNoise {len(surface_noises)}개.',
                    '- JSON 밖의 column 순회, 깊이·수위 계산, badlands/빙산 확장은 본문의 Java 소스 설명이 필요하다.',
                    '- 재생성: `python tools/document-minecraft-overworld.py`. 본문은 자동 생성하지 않는다.']
        for relative in ('data/worldgen/SurfaceRuleData.java','world/level/levelgen/SurfaceRules.java','world/level/levelgen/SurfaceSystem.java'):
            p = ROOT/'ref/sources/MCP-Reborn/src/main/java/net/minecraft'/relative
            surface.append(f'- `{relative}` SHA256: `{hashlib.sha256(p.read_bytes()).hexdigest()}`')
        replace('minecraft-overworld-surface.md', 'OVERWORLD SURFACE DATA', surface)
        print(json.dumps({'base_points':counts['points'], 'base_density_functions':len(selected), 'base_noises':len(used_noises),
                          'surface_unique_trees':len(surface_groups), 'surface_typed_nodes':total_nodes, 'surface_noises':len(surface_noises)}))


if __name__ == '__main__':
    main()
