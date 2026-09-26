// Map interaction and comparison. All world values still come from the C++ generator.
let comparisonConfig = null, comparisonVersion = 0, previewTimer, previewBusy = false, previewQueued = false;
let dragMap = null, probeJob = 0;
const WORLD = 131072;
function readRange() {
    const r = {};
    for (const k of ['x0', 'z0', 'x1', 'z1'])
        r[k] = Number($(k).value);
    if (Object.values(r).some(v => !Number.isFinite(v) || v < 0 || v > WORLD) || r.x1 <= r.x0 || r.z1 <= r.z0)
        throw Error('범위는 0~131,072이며 끝 좌표가 시작보다 커야 합니다.');
    return r;
}
function previewOptions() {
    return {
        ...readRange(),
        kind : $('map').value,
        resolution : Number($('resolution').value),
        octave : $('map').value === 'groundness' ? Number($('octave').value) : -1,
        weighted : $('weighted').checked,
        before_warp : $('before-warp').checked
    };
}
function viewKey() {
    return JSON.stringify({...previewOptions(), compare : $('compare').checked, comparisonVersion});
}
function refreshPreviewState() {
    if (!config)
        return;
    $('map-title').textContent = maps[$('map').value];
    $('split-control').hidden = !$('compare').checked;
    let dirty = true;
    try {
        const r = readRange();
        $('range-label').textContent =
            `X ${Math.round(r.x0).toLocaleString()} – ${Math.round(r.x1).toLocaleString()} / Z ${
                Math.round(r.z0).toLocaleString()} – ${Math.round(r.z1).toLocaleString()}`;
        dirty = !shown || shown.gen !== generation || shown.key !== viewKey();
    } catch {
    }
    $('map-state').textContent = previewBusy ? '계산 중 · 표시된 지도는 마지막 계산 결과입니다.'
                                 : !shown    ? '미리보기 갱신을 눌러 지도를 만드세요.'
                                 : dirty
                                     ? '● 변경 내용이 아직 지도에 반영되지 않았어요. 미리보기를 갱신하세요.'
                                     : '✓ 현재 작업과 범위가 지도에 반영됐어요.';
    $('map-state').classList.toggle('stale', dirty);
    $('render').disabled = previewBusy;
    $('render').textContent = previewBusy ? '계산 중…' : '미리보기 갱신';
}
function schedulePreview() {
    if (!config)
        return;
    clearTimeout(previewTimer);
    previewJob++;
    refreshPreviewState();
    drawMap();
    previewTimer = setTimeout(() => run(renderPreview), 180);
}
function imageFromResponse(bytes, kind) {
    const data = new Float32Array(bytes), w = data[0], h = data[1], values = data.slice(2);
    if (!Number.isInteger(w) || !Number.isInteger(h) || w < 2 || h < 2 || values.length !== w * h)
        throw Error('지도 크기 오류');
    const raster = el('canvas');
    raster.width = w;
    raster.height = h;
    const ctx = raster.getContext('2d'), image = ctx.createImageData(w, h);
    let low = Infinity, high = -Infinity;
    values.forEach((v, i) => {
        low = Math.min(low, v);
        high = Math.max(high, v);
        image.data.set([...colour(kind, v), 255 ], i * 4);
    });
    ctx.putImageData(image, 0, 0);
    return {raster, w, h, values, low, high};
}
async function renderPreview() {
    clearTimeout(previewTimer);
    if (previewBusy) {
        previewQueued = true;
        return;
    }
    const options = previewOptions(), key = viewKey(), job = ++previewJob, gen = generation;
    const body = {...options, config : copy(config)}, compare = $('compare').checked;
    const baseline = compare ? copy(comparisonConfig) : null;
    if (compare && options.octave >= baseline.groundness.octaves)
        throw Error(
            '비교 기준에 해당 옥타브가 없습니다. 전체 합성을 선택하거나 비교 기준을 다시 지정하세요.');
    previewBusy = true;
    previewQueued = false;
    refreshPreviewState();
    const start = performance.now();
    try {
        const after = imageFromResponse(await request('preview', body, true), options.kind);
        let before = null;
        if (compare && job === previewJob)
            before = imageFromResponse(await request('preview', {...options, config : baseline}, true),
                                       options.kind);
        if (job !== previewJob)
            return;
        shown = {...after, body, gen, key, before, baseline};
        $('empty-map').hidden = true;
        $('metrics').textContent =
            `${after.w}×${after.h} · ${(performance.now() - start).toFixed(0)} ms · 값 ${
                after.low.toFixed(3)} ~ ${after.high.toFixed(3)}`;
        $('legend').textContent = options.kind.includes('height')
                                      ? '파랑: 수면 192 아래 · 초록 → 흰색: 192~512'
                                  : options.kind === 'temperature'   ? '파랑: 추움 → 빨강: 더움'
                                  : options.kind === 'precipitation' ? '갈색: 건조 → 파랑: 습윤'
                                                                     : '검정: 낮음 → 흰색: 높음';
        $('probe').replaceChildren(el('p', '지도에서 위치를 클릭하세요.'));
        drawMap();
        status('미리보기 갱신 완료');
    } finally {
        previewBusy = false;
        refreshPreviewState();
        if (previewQueued) {
            previewQueued = false;
            setTimeout(() => run(renderPreview), 0);
        }
    }
}
function mapRect(range) {
    const bounds = $('map-stage').getBoundingClientRect(), padding = 24;
    const scale = Math.min((bounds.width - padding * 2) / (range.x1 - range.x0),
                           (bounds.height - padding * 2) / (range.z1 - range.z0));
    const width = (range.x1 - range.x0) * scale, height = (range.z1 - range.z0) * scale;
    return {x : (bounds.width - width) / 2, y : (bounds.height - height) / 2, width, height, scale, bounds};
}
function drawMap() {
    const canvas = $('map-canvas'), bounds = $('map-stage').getBoundingClientRect(),
          dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.max(1, Math.round(bounds.width * dpr));
    canvas.height = Math.max(1, Math.round(bounds.height * dpr));
    const ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);
    ctx.imageSmoothingEnabled = false;
    if (!shown)
        return;
    let view;
    try {
        view = readRange();
    } catch {
        view = shown.body;
    }
    const fit = mapRect(view), r = shown.body;
    const imageRect = [
        fit.x + (r.x0 - view.x0) * fit.scale, fit.y + (r.z0 - view.z0) * fit.scale, (r.x1 - r.x0) * fit.scale,
        (r.z1 - r.z0) * fit.scale
    ];
    ctx.save();
    ctx.beginPath();
    ctx.rect(fit.x, fit.y, fit.width, fit.height);
    ctx.clip();
    ctx.drawImage(shown.raster, ...imageRect);
    if ($('compare').checked && shown.before) {
        const split = fit.x + fit.width * Number($('compare-split').value) / 100;
        ctx.save();
        ctx.beginPath();
        ctx.rect(fit.x, fit.y, split - fit.x, fit.height);
        ctx.clip();
        ctx.drawImage(shown.before.raster, ...imageRect);
        ctx.restore();
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(split, fit.y);
        ctx.lineTo(split, fit.y + fit.height);
        ctx.stroke();
        ctx.font = 'bold 13px Segoe UI, Malgun Gothic';
        ctx.fillStyle = '#14221ee6';
        ctx.fillRect(fit.x + 8, fit.y + 8, 80, 28);
        ctx.fillRect(fit.x + fit.width - 88, fit.y + 8, 80, 28);
        ctx.fillStyle = '#fff';
        ctx.fillText('변경 전', fit.x + 24, fit.y + 27);
        ctx.fillText('현재 작업', fit.x + fit.width - 78, fit.y + 27);
    }
    ctx.restore();
    ctx.strokeStyle = '#78948a';
    ctx.strokeRect(fit.x, fit.y, fit.width, fit.height);
}
function setRange(r) {
    for (const a of ['x', 'z']) {
        const size = Math.min(WORLD, Math.max(16, r[a + '1'] - r[a + '0']));
        const start = Math.max(0, Math.min(WORLD - size, r[a + '0']));
        $(a + '0').value = Number(start.toFixed(3));
        $(a + '1').value = Number((start + size).toFixed(3));
    }
}
function zoomMap(factor, px = .5, pz = .5) {
    const r = readRange(), x = r.x0 + (r.x1 - r.x0) * px, z = r.z0 + (r.z1 - r.z0) * pz;
    const width = Math.max(16, Math.min(WORLD, (r.x1 - r.x0) * factor)),
          height = Math.max(16, Math.min(WORLD, (r.z1 - r.z0) * factor));
    setRange(
        {x0 : x - width * px, x1 : x + width * (1 - px), z0 : z - height * pz, z1 : z + height * (1 - pz)});
    schedulePreview();
}
function pointerPosition(event) {
    const b = $('map-stage').getBoundingClientRect();
    return {x : event.clientX - b.left, y : event.clientY - b.top};
}
async function probeAt(event) {
    if (!shown)
        return;
    const point = pointerPosition(event), view = readRange(), fit = mapRect(view),
          px = (point.x - fit.x) / fit.width, pz = (point.y - fit.y) / fit.height;
    if (px < 0 || px > 1 || pz < 0 || pz > 1)
        return;
    const x = view.x0 + (view.x1 - view.x0) * px, z = view.z0 + (view.z1 - view.z0) * pz, s = shown;
    if (x < s.body.x0 || x > s.body.x1 || z < s.body.z0 || z > s.body.z1)
        return;
    const before = $('compare').checked && s.before && px < Number($('compare-split').value) / 100,
          source = before ? s.before : s;
    const ix = Math.round((x - s.body.x0) / (s.body.x1 - s.body.x0) * (source.w - 1)),
          iz = Math.round((z - s.body.z0) / (s.body.z1 - s.body.z0) * (source.h - 1));
    const job = ++probeJob,
          values = await request('probe', {config : before ? s.baseline : s.body.config, x, z});
    if (job !== probeJob || shown !== s)
        return;
    $('probe').replaceChildren(
        el('div', `${before ? '변경 전' : '현재 작업'} · X ${x.toFixed(1)} / Z ${z.toFixed(1)}`),
        el('div', `표시 픽셀: ${source.values[iz * source.w + ix].toFixed(5)}`));
    for (const [k, v] of Object.entries(values))
        $('probe').append(el('div', `${maps[k]}: ${Number(v).toFixed(5)}`));
    const locate = button('가까운 스플라인 칸 열기', () => {
        if (before || s.gen !== generation) {
            status('현재 작업으로 지도를 갱신한 뒤 선택해 주세요.', true);
            return;
        }
        tab = 'spline';
        splineMode = 'curve';
        nodePath = [];
        const grid = config.splines[gridKind];
        const nearest = (axis, value) =>
            axis.reduce((best, v, i) => Math.abs(v - value) < Math.abs(axis[best] - value) ? i : best, 0);
        cell = nearest(grid.groundness, values.groundness) * grid.smoothness.length +
               nearest(grid.smoothness, values.smoothness);
        renderForm();
    });
    locate.title =
        '이 좌표의 Groundness/Smoothness에 가장 가까운 제어 칸입니다. 실제 값은 여러 칸을 보간할 수 있습니다.';
    locate.disabled = before || s.gen !== generation;
    $('probe').append(locate);
    $('probe-panel').open = true;
}
$('render').onclick = () => run(renderPreview);
$('map').onchange = () => {
    updateOctaves();
    schedulePreview();
};
$('octave').onchange = () => {
    updateOctaves();
    schedulePreview();
};
for (const k of ['resolution', 'weighted', 'before-warp'])
    $(k).onchange = schedulePreview;
for (const k of ['x0', 'z0', 'x1', 'z1'])
    $(k).oninput = refreshPreviewState;
$('apply-range').onclick = () => run(async () => {
    readRange();
    schedulePreview();
});
$('range-open').onclick = () => $('range-panel').open = !$('range-panel').open;
$('whole').onclick = () => {
    setRange({x0 : 0, z0 : 0, x1 : WORLD, z1 : WORLD});
    schedulePreview();
};
$('random-area').onclick = () => run(async () => {
    const r = readRange();
    for (const a of ['x', 'z']) {
        const size = r[a + '1'] - r[a + '0'], random = new Uint32Array(1);
        crypto.getRandomValues(random);
        r[a + '0'] = Math.floor(random[0] / 4294967296 * (WORLD - size));
        r[a + '1'] = r[a + '0'] + size;
    }
    setRange(r);
    schedulePreview();
});
$('zoom-in').onclick = () => run(async () => zoomMap(.5));
$('zoom-out').onclick = () => run(async () => zoomMap(2));
$('compare').onchange = schedulePreview;
$('compare-split').oninput = drawMap;
$('pin-baseline').onclick = () => {
    comparisonConfig = copy(config);
    comparisonVersion++;
    $('compare-label').textContent = '비교 기준: ' + new Date().toLocaleTimeString('ko-KR') + '의 작업';
    schedulePreview();
};
$('map-canvas').addEventListener('wheel', event => {
    event.preventDefault();
    if (!shown || dragMap)
        return;
    run(async () => {
        const fit = mapRect(readRange()), p = pointerPosition(event);
        zoomMap(Math.exp(Math.max(-1, Math.min(1, event.deltaY * .0015))),
                Math.max(0, Math.min(1, (p.x - fit.x) / fit.width)),
                Math.max(0, Math.min(1, (p.y - fit.y) / fit.height)));
    });
}, {passive : false});
$('map-canvas').onpointerdown = event => {
    if (event.button !== 0 || !shown)
        return;
    run(async () => {
        const range = readRange(), fit = mapRect(range), point = pointerPosition(event);
        if (point.x < fit.x || point.x > fit.x + fit.width || point.y < fit.y || point.y > fit.y + fit.height)
            return;
        dragMap = {range, fit, x : event.clientX, y : event.clientY, moved : false};
        event.target.setPointerCapture(event.pointerId);
        event.target.classList.add('dragging');
    });
};
$('map-canvas').onpointermove = event => {
    if (!dragMap)
        return;
    const dx = event.clientX - dragMap.x, dy = event.clientY - dragMap.y;
    if (Math.hypot(dx, dy) > 4)
        dragMap.moved = true;
    if (!dragMap.moved)
        return;
    const r = dragMap.range, scale = dragMap.fit.scale;
    setRange(
        {x0 : r.x0 - dx / scale, x1 : r.x1 - dx / scale, z0 : r.z0 - dy / scale, z1 : r.z1 - dy / scale});
    refreshPreviewState();
    drawMap();
};
$('map-canvas').onpointerup = event => {
    if (!dragMap)
        return;
    const moved = dragMap.moved;
    dragMap = null;
    event.target.classList.remove('dragging');
    if (event.target.hasPointerCapture(event.pointerId))
        event.target.releasePointerCapture(event.pointerId);
    if (moved)
        schedulePreview();
    else
        run(() => probeAt(event));
};
$('map-canvas').onpointercancel = () => {
    if (dragMap) {
        setRange(dragMap.range);
        dragMap = null;
        drawMap();
        refreshPreviewState();
    }
    $('map-canvas').classList.remove('dragging');
};
$('export-map').onclick = () => {
    if (shown)
        shown.raster.toBlob(blob => {
            if (blob)
                download(blob, `${shown.body.kind}-${shown.body.x0}-${shown.body.z0}.png`);
        });
};
new ResizeObserver(drawMap).observe($('map-stage'));

const topbar = document.querySelector('.topbar');
new ResizeObserver(() => document.documentElement.style.setProperty(
                       '--topbar-height', `${topbar.getBoundingClientRect().height}px`))
    .observe(topbar);

// Start only after both scripts have initialized their shared state.
run(loadState);
