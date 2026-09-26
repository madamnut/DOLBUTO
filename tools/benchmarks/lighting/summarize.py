"""Summarize explicitly invoked comparison runs. Uses only the Python standard library."""
import csv
import json
import math
import pathlib
import statistics
import sys
from collections import defaultdict


def stats(values):
    values = sorted(values)
    return {'n': len(values), 'mean': statistics.mean(values), 'median': statistics.median(values),
            'p95': values[math.ceil(len(values)*.95)-1], 'min': values[0], 'max': values[-1]}


root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
groups = defaultdict(lambda: defaultdict(list))
raw = []
for directory in ('final-a', 'final-b'):
    for row in csv.DictReader((root / directory / 'timings.csv').open()):
        groups[row['case']]['before' if row['reference'] == '1' else 'after'].append(float(row['ms']))
        raw.append({'session': directory, **row})
summary = {name: {mode: stats(values) for mode, values in modes.items()} for name, modes in groups.items()}
for name, modes in summary.items():
    modes['speedup_median'] = modes['before']['median']/modes['after']['median']
(out / 'lighting-2026-09-24-summary.json').write_text(json.dumps(summary, indent=2))
with (out / 'lighting-2026-09-24-timings.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, raw[0].keys()); writer.writeheader(); writer.writerows(raw)
for name, modes in summary.items():
    print(name, 'before', modes['before'], 'after', modes['after'], 'speedup', modes['speedup_median'])

games = []
for source in sorted(root.glob('game24-*.csv')):
    events = defaultdict(dict)
    work = defaultdict(float)
    with source.open() as f:
        for row in csv.DictReader(f):
            stage, parent = row['stage'], row['root']
            if stage == parent and stage in ('request', 'published', 'gpu_retired'):
                events[stage][(row['x'], row['z'])] = float(row['end_ms'])
            if parent in ('local_light', 'connect_light'):
                work[stage] += float(row['exclusive_ms'])
    if len(events['request']) != 1793 or len(events['published']) != 1793 or len(events['gpu_retired']) != 1793:
        raise RuntimeError(f'Incomplete game profile: {source}')
    games.append({'file': source.name, 'mode': source.stem.split('-')[1],
                  'load_ms': max(events['published'].values())-min(events['request'].values()),
                  'lighting_task_ms': sum(work.values()), 'stages': dict(work)})
(out / 'lighting-2026-09-24-game.json').write_text(json.dumps(games, indent=2))
for mode in ('before', 'after'):
    print('game24', mode, stats([x['load_ms'] for x in games if x['mode'] == mode]))

pipelines = {}
for mode in ('before', 'after'):
    rows = list(csv.DictReader((root / ('pipeline-'+mode) / 'runs.csv').open()))
    pipelines[mode] = {
        str(area): {'timing': stats([float(r['elapsed_ms']) for r in rows if int(r['area']) == area and r['instrumented'] == '0']),
                    'faces': sorted(set(int(r['faces']) for r in rows if int(r['area']) == area))}
        for area in range(3)}
(out / 'lighting-2026-09-24-pipeline.json').write_text(json.dumps(pipelines, indent=2))
print('pipeline', pipelines)
