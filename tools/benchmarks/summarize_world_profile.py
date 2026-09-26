"""Summarize explicit world profiles; no game launch, config mutation, or test registration."""
import collections
import csv
import json
import math
import pathlib
import statistics
import sys


def stats(values):
    values = sorted(values)
    if not values:
        return {}
    return dict(n=len(values), mean=statistics.mean(values), median=statistics.median(values),
                p95=values[max(0, math.ceil(len(values) * .95) - 1)],
                minimum=values[0], maximum=values[-1])


def summarize(paths):
    totals = collections.Counter()
    generation = collections.Counter()
    samples = collections.Counter()
    timings = collections.defaultdict(list)
    loads = []
    cpu_roots = {'generation', 'local_light', 'connect_light', 'mesh', 'scheduler'}
    markers = {'request', 'mesh_ready', 'delivered', 'published', 'gpu_retired'}
    for path in paths:
        events = collections.defaultdict(dict)
        meshes = collections.Counter()
        frames = []
        gpu = []
        counts = collections.Counter()
        with path.open() as source:
            for row in csv.DictReader(source):
                root, stage = row['root'], row['stage']
                key = (int(row['x']), int(row['z']))
                inc, exc = float(row['inclusive_ms']), float(row['exclusive_ms'])
                start, end = float(row['start_ms']), float(row['end_ms'])
                if root in cpu_roots:
                    totals[stage] += exc
                if root == 'generation':
                    generation[stage] += exc
                    samples[stage] += int(row['samples'])
                if root == stage:
                    counts[root] += 1
                    if root in markers:
                        events[root][key] = end
                    elif root == 'frame':
                        frames.append((start, inc))
                    elif root in ('gpu_frame', 'gpu_upload'):
                        gpu.append((root, end, inc))
                    elif root == 'mesh':
                        meshes[key] += inc
                    elif root == 'task_wait':
                        timings[f'queue_kind_{row["y"]}'].append(inc)
                    else:
                        timings[root].append(inc)
                if stage in ('prepare', 'upload_cpu'):
                    timings[stage].append(inc)
        timings['mesh_column'].extend(meshes.values())
        first = min(events['request'].values())
        last = max(events['published'].values())
        if len(events['request']) != len(events['published']):
            raise RuntimeError(f'Incomplete publication: {path}')
        loads.append(dict(file=path.name, columns=len(events['published']),
                          load_ms=last-first, counts=dict(counts),
                          retired_ms=max(events['gpu_retired'].values())-first
                          if events['gpu_retired'] else None))
        for start, duration in frames:
            if first <= start <= last:
                timings['loading_frame'].append(duration)
        for name, end, duration in gpu:
            if first <= end <= last:
                timings[name].append(duration)
        for a, b in [('request', 'mesh_ready'), ('mesh_ready', 'delivered'),
                     ('delivered', 'published'), ('published', 'gpu_retired')]:
            timings[a+'->'+b].extend(value-events[a][key] for key, value in events[b].items()
                                     if key in events[a])
    total = sum(totals.values())
    return dict(files=loads, loads=stats([x['load_ms'] for x in loads]),
                cpu_exclusive_ms=dict(totals), cpu_total_ms=total,
                cpu_percent={k: v / total * 100 for k, v in totals.items()},
                generation_exclusive_ms=dict(generation), noise_samples=dict(samples),
                timings={k: stats(v) for k, v in timings.items() if v})


directory = pathlib.Path(sys.argv[1])
result = {}
for mode in ('cpu', 'gpu', 'full'):
    for area in range(3):
        files = sorted(directory.glob(f'{mode}-*-{area}.csv'))
        if files:
            result[f'{mode}-{area}'] = summarize(files)
with (directory / 'runs.csv').open() as source:
    runs = list(csv.DictReader(source))
result['cpu_harness'] = {}
for area in range(3):
    group = [r for r in runs if int(r['area']) == area]
    result['cpu_harness'][str(area)] = {
        'timings_ms': {str(mode): stats([float(r['elapsed_ms']) for r in group
                                       if int(r['instrumented']) == mode]) for mode in (0, 1)},
        'face_counts': sorted(set(int(r['faces']) for r in group)),
        'column_counts': sorted(set(int(r['columns']) for r in group)),
    }
(directory / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
for key, value in result.items():
    if key == 'cpu_harness':
        print(key, json.dumps(value))
        continue
    print(key, 'load_ms', value['loads'])
    print('  cpu_percent', {k: round(v, 3) for k, v in sorted(value['cpu_percent'].items(), key=lambda x:-x[1])})
    print('  timings_mean_ms', {k:round(v['mean'], 4) for k,v in value['timings'].items()})
