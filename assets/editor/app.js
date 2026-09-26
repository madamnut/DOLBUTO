'use strict';
const $ = id => document.getElementById(id), copy = v => structuredClone(v);
const token = location.hash.slice(1) || sessionStorage.getItem('editor-token') || '';
if (token)
    sessionStorage.setItem('editor-token', token);
history.replaceState(null, '', location.pathname);
let config, revision = '', generation = 0, tab = 'spline', undo = [], shown = null, previewJob = 0;
const openSections = new Map();
let selectedSignal = 'groundness', climateSignal = 'temperature', nodePath = [], splineCanvas,
    publishedSignature = '';
let gridKind = 'offset', cell = 0, splineMode = 'grid', cellClipboard, localCanvas, localTimer,
    previewGroundness = 0, previewSmoothness = 0, previewAxis = "weirdness", previewWeirdness = 0, curveTimer,
    savedSignature = '', rawEdited = false, rawGeneration = 0;
function syncRaw() {
    if (!rawEdited) {
        $('raw').value = JSON.stringify(config, null, 2);
        rawGeneration = generation;
    }
}
const maps = {
    groundness : 'Groundness · 대륙성',
    smoothness : 'Smoothness · 완만함',
    weirdness : 'Weirdness',
    pv : 'PV · 봉우리/계곡',
    base_height : '기준 높이 · 3D 적용 전',
    effective_height : '잔굴곡 적용 높이 · 3D 적용 전',
    temperature : '온도',
    precipitation : '강수량',
    offset : '높이 오프셋',
    factor : '최종 압축',
    jaggedness : '잔굴곡 강도',
    jagged_noise : '잔굴곡 원본 노이즈'
};
const labels = {
    frequency_multiplier : '입력 주파수 배율',
    xz_scale : '3D 가로 스케일',
    y_scale : '3D 세로 스케일',
    xz_factor : '혼합 가로 계수',
    y_factor : '혼합 세로 계수',
    smear : '세로 Smear',
    seed : '월드 시드',
    spacing_log2 : '격자 간격 지수',
    octaves : '옥타브 수',
    gain : '옥타브 공통 감쇠',
    seed_offset : '시드 오프셋',
    enabled : '사용',
    strength : '변위 강도 (블록)',
    shape_enabled : '3D 노이즈 사용',
    vertical_scale : '3D 세로 간격',
    amplitude : '3D 진폭',
    squash : '전체 압축 강도',
    equator : '적도 온도',
    poles : '극지 온도',
    latitude_power : '위도 변화 곡률',
    variation : '지역 온도 변동',
    height_scale : '높이 배율',
    jagged_scale : '잔굴곡 배율',
    jagged_enabled : '잔굴곡 사용',
};
const help = {
    frequency_multiplier :
        '입력 좌표에 곱하는 배율입니다. 클수록 간격이 작아집니다. Jagged의 원본 초기값은1500입니다.',
    xz_scale : '3D 하한·상한 노이즈의 가로 주파수입니다. 클수록 가로 굴곡이 작아집니다.',
    y_scale : '3D 하한·상한 노이즈의 세로 주파수입니다. 클수록 세로 무늬가 촘촘해집니다.',
    xz_factor :
        '혼합 노이즈의 가로 주파수를 이 값으로 나눕니다. 클수록 하한·상한을 선택하는 가로 영역이 커집니다.',
    y_factor : '혼합 노이즈의 세로 주파수를 이 값으로 나눕니다. 클수록 혼합 영역이 세로로 길어집니다.',
    smear :
        '원본의 세로 표본 보정 배율입니다. 세로 방향 노이즈의 내부 표본을 계단식으로 보정하며 결과 밀도는4블록 격자에서 보간합니다.',

    spacing_log2 :
        '실제 격자 간격은 2의 지수승 블록입니다. 예: 10 → 1024블록. 옥타브가 올라갈수록 절반 간격입니다.',
    octaves : '합성할 노이즈 개수입니다. 간격과 옥타브 범위는 게임 생성기가 검증합니다.',
    gain : '개별 가중치가 없을 때 다음 옥타브에 곱하는 값입니다.',
    seed_offset : '이 신호에만 더하는 시드입니다.',
    squash : '수직 그라디언트 압축 강도입니다. 커질수록 같은 3D 노이즈가 만드는 높이 변화가 줄어듭니다.',
    vertical_scale : '3D 노이즈의 세로 격자 간격입니다.',
    amplitude : '3D 노이즈가 지형 밀도에 미치는 크기입니다.',
    strength : '이 워핑을 선택한 노이즈의 X/Z 좌표 변위 배율입니다. 0이면 변형하지 않습니다.',
    variation : '기후 노이즈가 위도에 따른 기본 온도에서 벗어나는 정도입니다.',
    seed : '같은 시드와 규칙은 같은 월드를 만듭니다.'
};
function status(text, error = false) {
    $('status').textContent = text;
    $('status').classList.toggle('error', error);
    $('spline-status').textContent = error && tab === 'spline' ? text : '';
    $('files-status').textContent = $('files-dialog').open ? text : '';
    refreshIndicators();
}
async function request(path, body, binary = false) {
    const options = {headers : {'X-Editor-Token' : token}};
    if (body !== undefined) {
        options.method = 'POST';
        options.headers['Content-Type'] = 'application/json';
        options.body = JSON.stringify(body);
    }
    const response = await fetch('/api/' + path, options);
    if (!response.ok) {
        const t = await response.text();
        let message = t;
        try {
            message = JSON.parse(t).error || t;
        } catch {
        }
        throw Error(message);
    }
    return binary ? response.arrayBuffer() : response.json();
}
function changed(snapshot = true) {
    if (snapshot) {
        undo.push(copy(config));
        if (undo.length > 16)
            undo.shift();
    }
    generation++;
    $('map-state').textContent = '편집 중 변경 사항이 있습니다. 지도를 다시 생성하면 반영됩니다.';
}
function mutate(fn) {
    changed();
    fn();
    updateOctaves();
    syncRaw();
    refreshIndicators();
    refreshPreviewState();
    if ($('auto-preview').checked)
        schedulePreview();
    if (tab === 'spline') {
        clearTimeout(curveTimer);
        curveTimer = setTimeout(() => {
            refreshSplineCurve();
            refreshLocalCurve();
        }, 120);
    }
}
function el(tag, text, cls) {
    const e = document.createElement(tag);
    if (text !== undefined)
        e.textContent = text;
    if (cls)
        e.className = cls;
    return e;
}
function button(text, action) {
    const b = el('button', text);
    b.type = 'button';
    b.onclick = action;
    return b;
}
function input(value, onchange) {
    const i = el('input');
    i.type = 'number';
    i.step = 'any';
    i.value = value;
    i.onchange = () => {
        if (!Number.isFinite(i.valueAsNumber)) {
            i.value = value;
            status('유한한 숫자를 입력해 주세요.', true);
            return;
        }
        onchange(i.valueAsNumber);
    };
    return i;
}
function field(parent, obj, key) {
    const row = el('label', undefined, 'field');
    row.append(el('span', labels[key] || key));
    row.title = help[key] || '';
    let i;
    if (typeof obj[key] === 'boolean') {
        i = el('input');
        i.type = 'checkbox';
        i.checked = obj[key];
        i.onchange = () => mutate(() => obj[key] = i.checked);
    } else
        i = input(obj[key], v => mutate(() => obj[key] = v));
    row.append(i);
    parent.append(row);
}
function scalarFields(parent, obj, keys = Object.keys(obj)) {
    for (const k of keys)
        if (typeof obj[k] === 'number' || typeof obj[k] === 'boolean')
            field(parent, obj, k);
}
function section(parent, title, text) {
    parent.append(el('h3', title));
    if (text)
        parent.append(el('p', text, 'help'));
}
function select(options, value, onchange) {
    const s = el('select');
    for (const [v, label] of Object.entries(options)) {
        const o = el('option', label);
        o.value = v;
        s.append(o);
    }
    s.value = value;
    s.onchange = () => onchange(s.value);
    return s;
}
function noise(parent, key, title, settings = config[key]) {
    const d = el('section', undefined, 'noise-settings');
    section(d, title);
    const n = settings;
    if (key !== 'shape' && key !== 'warp-field' && key !== 'jagged') {
        const row = el('label', undefined, 'field');
        row.append(el('span', '적용 워핑'));
        const choices = Object.fromEntries(
            [ [ '', '없음' ], ...config.warps.map(w => [w.id, w.name + (w.enabled ? '' : ' (꺼짐)')]) ]);
        row.append(select(choices, n.warp || '', value => mutate(() => n.warp = value)));
        d.append(row, el('p',
                         key === 'temperature' ? '주기적 Double Perlin · X 순환 / Z 직선 · 위도 띠 유지'
                                               : '주기적 Double Perlin · X/Z 순환 · 월드 주기 131072블록',
                         'help'));
    }
    for (const k of (key === 'groundness'
                         ? [ 'octaves', 'seed_offset', 'frequency_multiplier' ]
                         : [ 'spacing_log2', 'octaves', 'gain', 'seed_offset', 'frequency_multiplier' ])) {
        if (k !== 'octaves') {
            field(d, n, k);
            continue;
        }
        const row = el('label', undefined, 'field');
        row.append(el('span', labels[k]));
        row.title = help[k];
        row.append(input(n.octaves, v => {
            mutate(() => {
                if (key === 'groundness' && !n.spacings)
                    n.spacings = Array.from({length : n.octaves}, (_, i) => 2 ** (n.spacing_log2 - i));
                n.octaves = Math.max(1, Math.min(key === 'shape' ? 8 : 16, Math.round(v)));
                if (key === 'groundness') {
                    while (n.spacings.length < n.octaves)
                        n.spacings.push(Math.max(.001, n.spacings[n.spacings.length - 1] / 2));
                    n.spacings.length = n.octaves;
                }
                if (n.weights)
                    n.weights = Array.from({length : n.octaves}, (_, i) => n.weights[i] ?? 0);
            });
            renderForm();
        }));
        d.append(row);
    }
    if (key !== 'groundness') {
        const row = el('label', undefined, 'check'), i = el('input');
        i.type = 'checkbox';
        i.checked = Boolean(n.weights);
        row.append(i, el('span', '개별 옥타브 가중치 사용'));
        i.onchange = () => {
            mutate(() => {
                if (i.checked)
                    n.weights = Array.from({length : n.octaves}, (_, i) => Math.pow(n.gain, i));
                else
                    delete n.weights;
            });
            renderForm();
        };
        d.append(row);
    }
    d.append(el(
        'p',
        n.weights
            ? '개별 가중치 사용 중에는 공통 감쇠가 적용되지 않습니다. 0은 해당 겹을 제외합니다. 원본의 1/2 옥타브 감쇄와 Double Perlin 보정은 별도 적용됩니다.'
            : '다음 겹마다 공통 감쇠를 곱합니다. 원본의 1/2 옥타브 감쇄와 Double Perlin 보정은 별도 적용됩니다.',
        'help'));
    if (key === 'groundness')
        d.append(el(
            'p',
            '각 옥타브의 간격을 블록 단위로 입력합니다(0.001~131072, 소수 가능). 간격 순서는 자유이며 월드 경계는 이어집니다. 새 겹은 마지막 간격의 절반과 가중치0으로 추가합니다.',
            'help'));
    if (n.weights)
        for (let i = 0; i < n.octaves; i++) {
            const row = el('label', undefined, 'weight-row');
            row.append(el('span', `${i + 1}번 옥타브`));
            row.append(input(n.weights[i] ?? 0, v => mutate(() => n.weights[i] = v)));
            d.append(row);
            if (key === 'groundness') {
                const spacingRow = el('label', undefined, 'field');
                spacingRow.append(el('span', `${i + 1}번 간격 (블록)`));
                const scaleInput = input(n.spacings[i], v => {
                    if (v < .001 || v > 131072) {
                        scaleInput.value = n.spacings[i];
                        status('간격은 0.001~131072블록입니다.', true);
                        return;
                    }
                    mutate(() => n.spacings[i] = v);
                });
                scaleInput.min = .001;
                scaleInput.max = 131072;
                spacingRow.append(scaleInput);
                d.append(spacingRow);
            }
        }
    const actual = el('details');
    actual.append(el('summary', '두 묶음의 실제 옥타브 간격 (블록)'));
    for (let i = 0; i < n.octaves; ++i) {
        const requested = (n.spacings?.[i] ?? 2 ** (n.spacing_log2 - i)) / n.frequency_multiplier;
        const effective = ratio => 131072 / Math.max(1, Math.round(131072 / requested * ratio));
        actual.append(el(
            'p', `${i + 1}: ${effective(1).toPrecision(7)} / ${effective(1.0181268882175227).toPrecision(7)}`,
            'help'));
    }
    d.append(actual);
    parent.append(d);
}
function warpList(parent) {
    const w = config.warps[0];
    section(
        parent, '공유 Shift',
        '하나의 주기적 3D Double Perlin에서 (X,0,Z), (Z,X,0) 두 단면을 사용합니다. 기본 변위16블록은 원본 Shift ×4의 월드 좌표 환산값입니다.');
    field(parent, w, 'enabled');
    field(parent, w, 'strength');
    noise(parent, 'warp-field', 'Shift 노이즈', w.noise);
}

function drawGraph(canvas, points, controls) {
    const ctx = canvas.getContext('2d');
    canvas.width = 720;
    canvas.height = 320;
    ctx.clearRect(0, 0, 720, 320);
    const vals = points.map(p => p[1]);
    if (controls)
        vals.push(...controls.map(p => p[1]));
    let low = Math.min(...vals), high = Math.max(...vals);
    if (high - low < .01) {
        low -= 1;
        high += 1;
    }
    const pad = (high - low) * .08;
    low -= pad;
    high += pad;

    let xlow = Math.min(-1, ...points.map(p => p[0])), xhigh = Math.max(1, ...points.map(p => p[0]));
    if (canvas.dragGraph)
        ({low, high, xlow, xhigh} = canvas.dragGraph);
    const project = p => [45 + (p[0] - xlow) / (xhigh - xlow) * 650, 285 - (p[1] - low) / (high - low) * 250];
    canvas.graph = {pos : project, low, high, xlow, xhigh};
    ctx.strokeStyle = document.body.classList.contains('dark') ? '#444' : '#ddd';
    ctx.fillStyle = document.body.classList.contains('dark') ? '#ccc' : '#555';
    ctx.font = '18px Segoe UI';
    for (let i = 0; i <= 4; i++) {
        const y = 35 + i * 62.5;
        ctx.beginPath();
        ctx.moveTo(45, y);
        ctx.lineTo(695, y);
        ctx.stroke();
        ctx.fillText((high - (high - low) * i / 4).toFixed(1), 3, y);
    }
    ctx.fillText(xlow.toFixed(1), 40, 312);
    ctx.fillText(((xlow + xhigh) / 2).toFixed(1), 350, 312);
    ctx.fillText(xhigh.toFixed(1), 665, 312);
    ctx.strokeStyle = '#2a9d8f';
    ctx.lineWidth = 3;
    ctx.beginPath();
    points.forEach((p, i) => {
        const [x, y] = project(p);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    });
    ctx.stroke();
    if (controls) {
        ctx.fillStyle = '#c48c1b';
        for (const p of controls) {
            const [x, y] = project(p);
            ctx.beginPath();
            ctx.arc(x, y, 6, 0, 7);
            ctx.fill();
        }
    }
}
async function graph(canvas, kind, controls, c = 0, e = 0) {
    const id = canvas.requestId = (canvas.requestId || 0) + 1;
    try {
        const r = await request('curve', {config : copy(config), kind, groundness : c, smoothness : e});
        if (id === canvas.requestId && canvas.isConnected)
            drawGraph(canvas, r.points, controls);
    } catch (err) {
        if (id === canvas.requestId && canvas.isConnected)
            status('곡선: ' + err.message, true);
    }
}
// Relationships are navigation state only; the value-owned generation tree is unchanged.
const splineNames = {
    offset : 'Offset',
    factor : 'Factor',
    jaggedness : 'Jaggedness'
};
const axisNames = {
    groundness : 'GND',
    smoothness : 'Smoothness',
    weirdness : 'Weirdness',
    pv : 'PV'
};
const expandedRelations = new Set();
let relationLabels = [];
const numberLabel = v => Number(v.toPrecision(7)).toString();
function relationKey(path) { return gridKind + ':' + path.join('.'); }
function samePath(a, b) { return a.length === b.length && a.every((v, i) => v === b[i]); }
function openSplineNode(path) {
    nodePath = [...path ];
    renderSpline();
    document.querySelector('.editor-panel').scrollTop = 0;
}
function renderRelationships() {
    const host = $('spline-relations');
    if (!host)
        return;
    const scroll = host.scrollTop;
    relationLabels = [];
    host.replaceChildren();
    const root = config.splines[gridKind].tree;
    host.append(el('p', '▸ 펼치기 · 이름을 누르면 해당 규칙 편집', 'help'));
    const top = button(`${splineNames[gridKind]} 전체 규칙`, () => openSplineNode([]));
    top.classList.toggle('active', !nodePath.length);
    top.setAttribute('aria-current', !nodePath.length ? 'true' : 'false');
    host.append(top);
    const append = (parent, node, path) => {
        if (typeof node === 'number')
            return;
        const list = el('ul', undefined, 'relation-list');
        parent.append(list);
        node.points.forEach((point, index) => {
            const childPath = [...path, index ], item = el('li');
            const nested = typeof point.value !== 'number';
            const text = () => `${axisNames[node.axis]} ${numberLabel(point.location)} → ${
                nested ? axisNames[point.value.axis] + ' 곡선' : numberLabel(point.value)}`;
            const choose = button(text(), () => openSplineNode(childPath));
            const refreshLabel = () => {
                choose.textContent = text();
                choose.title = text();
            };
            refreshLabel();
            relationLabels.push(refreshLabel);
            choose.classList.toggle('active', samePath(childPath, nodePath));
            choose.setAttribute('aria-current', samePath(childPath, nodePath) ? 'true' : 'false');
            list.append(item);
            if (!nested) {
                item.append(choose);
                return;
            }
            const branch = el('details'), summary = el('summary');
            summary.append(choose);
            branch.append(summary);
            item.append(branch);
            // A button selects; the native disclosure marker expands independently.
            choose.onclick = event => {
                event.preventDefault();
                openSplineNode(childPath);
            };
            const key = relationKey(childPath);
            const selectedAncestor =
                childPath.length <= nodePath.length && childPath.every((v, i) => v === nodePath[i]);
            branch.open = expandedRelations.has(key) || selectedAncestor;
            let populated = false;
            const populate = () => {
                if (!populated) {
                    append(branch, point.value, childPath);
                    populated = true;
                }
            };
            if (branch.open)
                populate();
            branch.ontoggle = () => {
                if (!branch.isConnected)
                    return;
                if (branch.open) {
                    expandedRelations.add(key);
                    populate();
                } else
                    expandedRelations.delete(key);
            };
        });
    };
    append(host, root, []);
    host.scrollTop = scroll;
    const selected = host.querySelector('[aria-current="true"]');
    if (selected) {
        const bounds = host.getBoundingClientRect(), point = selected.getBoundingClientRect();
        if (point.top < bounds.top)
            host.scrollTop -= bounds.top - point.top + 8;
        else if (point.bottom > bounds.bottom)
            host.scrollTop += point.bottom - bounds.bottom + 8;
    }
}
function nodeEditor(parent, previewParent = parent) {
    const grid = config.splines[gridKind];
    let node = grid.tree ?? grid.cells[cell],
        set = value => grid.tree !== undefined ? grid.tree = value : grid.cells[cell] = value;
    const trail = el('nav', undefined, 'breadcrumbs');
    trail.setAttribute('aria-label', '현재 스플라인 경로');
    trail.append(
        button(grid.tree !== undefined ? splineNames[gridKind] : '선택한 칸', () => openSplineNode([])));
    const conditions = [];
    for (let depth = 0; depth < nodePath.length; depth++) {
        const index = nodePath[depth];
        if (typeof node === 'number' || !node || !node.points[index]) {
            nodePath = nodePath.slice(0, depth);
            break;
        }
        const point = node.points[index], label = `${axisNames[node.axis]} ${numberLabel(point.location)}`;
        conditions.push(label);
        node = point.value;
        set = value => point.value = value;
        const path = nodePath.slice(0, depth + 1);
        trail.append(el('span', '›'), button(label, () => openSplineNode(path)));
    }
    trail.append(el('span', '›'), el('strong', typeof node === 'number' ? '고정값' : axisNames[node.axis]));
    parent.append(trail);
    parent.append(el(
        'p',
        conditions.length
            ? `${
                  conditions.join(
                      ' → ')} 제어점에 연결된 출력 규칙입니다. 이 경로는 제어점의 연결 관계이며, 그 사이 입력값은 보간됩니다.`
            : `${
                  splineNames
                      [gridKind]}의 시작 규칙입니다. 아래 표의 각 점은 숫자 또는 다른 곡선에 연결됩니다.`,
        'help'));
    const label = el('label', '출력 방식 / 입력축');
    const modes = nodePath.length < 8 ? {
        constant : '고정된 숫자',
        groundness : 'GND에 따라 변하는 곡선',
        smoothness : 'Smoothness에 따라 변하는 곡선',
        weirdness : 'Weirdness에 따라 변하는 곡선',
        pv : 'PV에 따라 변하는 곡선'
    }
                                      : {constant : '고정된 숫자'};
    label.append(select(modes, typeof node === 'number' ? 'constant' : node.axis, axis => {
        if (typeof node !== 'number' && !confirm('이 단계의 곡선과 하위 곡선을 교체할까요?')) {
            renderSpline();
            return;
        }
        mutate(() => set(axis === 'constant' ? 0 : {
                   axis,
                   points : [
                       {location : -1, derivative : 0, value : typeof node === 'number' ? node : 0},
                       {location : 1, derivative : 0, value : typeof node === 'number' ? node : 0}
                   ]
               }));
        renderSpline();
    }));
    parent.append(label);
    const fields = [];
    let refreshAdd = () => {};
    const checkedInput = (value, title, min, max, apply) => {
        const field = el('input');
        field.type = 'number';
        field.step = 'any';
        field.value = value;
        const bounds =
            () => [typeof min === 'function' ? min() : min, typeof max === 'function' ? max() : max];
        const refreshBounds = () => {
            const [lo, hi] = bounds();
            field.min = lo;
            field.max = hi;
        };
        fields.push(refreshBounds);
        refreshBounds();
        field.onchange = () => {
            const v = field.valueAsNumber, [ lo, hi ] = bounds();
            if (!Number.isFinite(v) || v < lo || v > hi) {
                field.value = value;
                status(`${title}: ${numberLabel(lo)} ~ ${numberLabel(hi)} 범위의 숫자를 입력하세요.`, true);
                return;
            }
            mutate(() => apply(v));
            value = v;
            // Keep DOM/focus stable when a number is committed by clicking another control.
            fields.forEach(refresh => refresh());
            relationLabels.forEach(refresh => refresh());
            refreshAdd();
        };
        field.setAttribute('aria-label', title);
        return field;
    };
    if (typeof node === 'number') {
        const row = el('label', '고정 출력값');
        row.append(checkedInput(node, '고정 출력값', -64, 64, v => {
            set(v);
            node = v;
        }));
        parent.append(
            row,
            el('p',
               '이 값은 다른 노이즈에 따라 변하지 않습니다. 위에서 입력축을 선택하면 하위 곡선으로 바꿀 수 있습니다.',
               'help'));
    } else {
        parent.append(el('h3', `${axisNames[node.axis]} 제어점 표`));
        parent.append(el(
            'p',
            '입력 위치는 오름차순입니다. 숫자는 고정 출력, 곡선은 다른 입력에 따라 변하는 출력입니다. 접선은 이 입력축에 대한 기울기입니다.',
            'help'));
        const wrap = el('div', undefined, 'points-table-wrap'),
              table = el('table', undefined, 'points-table');
        const head = el('thead'), headings = el('tr');
        for (const name of ['입력 위치', '출력값 / 연결된 규칙', '접선 기울기', '관리']) {
            const th = el('th', name);
            th.scope = 'col';
            headings.append(th);
        }
        head.append(headings);
        table.append(head);
        wrap.append(table);
        parent.append(wrap);
        const body = el('tbody');
        table.append(body);
        node.points.forEach((point, index) => {
            const row = el('tr'), location = el('td'), output = el('td'), derivative = el('td'),
                  actions = el('td');
            const prefix = `${axisNames[node.axis]} 제어점 ${index + 1}`;
            location.append(checkedInput(
                point.location, prefix + ' 위치', () => index ? node.points[index - 1].location + .00011 : -2,
                () => index + 1 < node.points.length ? node.points[index + 1].location - .00011 : 2,
                v => point.location = v));
            if (typeof point.value === 'number') {
                output.append(el('small', '고정 숫자'),
                              checkedInput(point.value, prefix + ' 출력값', -64, 64, v => point.value = v));
                if (nodePath.length < 8)
                    output.append(button('곡선으로 변경…', () => openSplineNode([...nodePath, index ])));
            } else {
                output.append(button(`${axisNames[point.value.axis]} 곡선 → 열기`,
                                     () => openSplineNode([...nodePath, index ])));
                output.append(
                    el('small', `${point.value.points.length}개 제어점 · 다른 입력에 따라 출력이 변함`));
                const evaluated = el('small', '미리보기 출력 계산 중…', 'point-evaluation');
                evaluated.dataset.point = index;
                output.append(evaluated);
            }
            derivative.append(
                checkedInput(point.derivative, prefix + ' 접선', -64, 64, v => point.derivative = v));
            const remove = button('삭제', () => {
                if (typeof point.value !== 'number' && !confirm('이 제어점과 연결된 하위 곡선을 삭제할까요?'))
                    return;
                mutate(() => node.points.splice(index, 1));
                renderSpline();
            });
            remove.disabled = node.points.length <= 1;
            remove.setAttribute('aria-label', prefix + ' 삭제');
            actions.append(remove);
            row.append(location, output, derivative, actions);
            body.append(row);
        });
        const add = button('＋ 제어점 추가', () => {
            mutate(() => {
                if (node.points.length === 1) {
                    const first = node.points[0],
                          location = first.location < 1.9 ? first.location + .1 : first.location - .1;
                    node.points.push({location, derivative : 0, value : copy(first.value)});
                    node.points.sort((a, b) => a.location - b.location);
                } else {
                    let index = 0;
                    for (let i = 1; i < node.points.length - 1; i++)
                        if (node.points[i + 1].location - node.points[i].location >
                            node.points[index + 1].location - node.points[index].location)
                            index = i;
                    const a = node.points[index], b = node.points[index + 1];
                    node.points.splice(
                        index + 1, 0,
                        {location : (a.location + b.location) / 2, derivative : 0, value : copy(a.value)});
                }
            });
            renderSpline();
        });
        refreshAdd = () => {
            add.disabled =
                node.points.length >= 64 ||
                (node.points.length > 1 &&
                 !node.points.some((p, i) => i && p.location - node.points[i - 1].location >= .00022));
        };
        refreshAdd();
        parent.append(
            add,
            el('p', '추가한 점은 왼쪽 점의 출력 규칙을 복사합니다. 추가 후 곡선 모양은 달라질 수 있습니다.',
               'help'));
    }
    previewParent.append(el('h3', '선택한 규칙의 곡선'));
    previewParent.append(el(
        'p',
        typeof node === 'number'
            ? '고정 출력값입니다.'
            : `가로축: ${axisNames[node.axis]} · 세로축: ${
                  splineNames
                      [gridKind]} 원시 출력. 하위 곡선은 위의 고정 입력으로 평가합니다. 점 드래그: 위치·숫자 출력 / Shift+드래그: 접선.`,
        'help'));
    localCanvas = el('canvas', undefined, 'curve');
    localCanvas.setAttribute('aria-label', '선택한 스플라인 곡선');
    localCanvas.evaluationLabels = [...parent.querySelectorAll('.point-evaluation') ];
    previewParent.append(localCanvas);
    if (typeof node !== 'number')
        attachNodeDrag(localCanvas, node);
}
async function refreshLocalCurve() {
    const canvas = localCanvas;
    if (tab !== 'spline' || splineMode !== 'curve' || !canvas || !canvas.isConnected)
        return;
    const job = canvas.requestId = (canvas.requestId || 0) + 1;
    try {
        const r = await request('curve', {
            config : copy(config),
            kind : gridKind,
            cell,
            node_path : [...nodePath ],
            preview_weirdness : previewWeirdness,
            groundness : previewGroundness,
            smoothness : previewSmoothness
        });
        if (canvas === localCanvas && canvas.isConnected && job === canvas.requestId) {
            canvas.controls = r.controls;
            for (const label of canvas.evaluationLabels || []) {
                const value = r.controls[Number(label.dataset.point)]?.[1];
                label.textContent = value === undefined ? '' : '고정 입력에서 출력: ' + numberLabel(value);
            }
            drawGraph(canvas, r.points, r.controls);
        }
    } catch (error) {
        if (canvas.isConnected && job === canvas.requestId)
            status('곡선: ' + error.message, true);
    }
}
function attachNodeDrag(canvas, node) {
    let drag = null;
    const pointAt = event => {
        const r = canvas.getBoundingClientRect();
        return [ (event.clientX - r.left) / r.width * 720, (event.clientY - r.top) / r.height * 320 ];
    };
    canvas.onpointerdown = event => {
        if (event.button !== 0 || !canvas.graph || !canvas.controls)
            return;
        const [px, py] = pointAt(event);
        let index = -1, distance = 24;
        canvas.controls.forEach((point, i) => {
            const [x, y] = canvas.graph.pos(point), d = Math.hypot(px - x, py - y);
            if (d < distance) {
                index = i;
                distance = d;
            }
        });
        if (index < 0)
            return;
        changed();
        canvas.dragGraph = {...canvas.graph};
        drag = {index, tangent : event.shiftKey, x : px, y : py, derivative : node.points[index].derivative};
        canvas.setPointerCapture(event.pointerId);
    };
    canvas.onpointermove = event => {
        if (!drag)
            return;
        const g = canvas.dragGraph, [ px, py ] = pointAt(event), point = node.points[drag.index];
        if (drag.tangent) {
            point.derivative =
                Math.max(-64, Math.min(64, drag.derivative + (drag.y - py) / 250 * (g.high - g.low) / .2));
        } else {
            const x = g.xlow + (px - 45) / 650 * (g.xhigh - g.xlow),
                  y = g.low + (285 - py) / 250 * (g.high - g.low);
            const min = drag.index ? node.points[drag.index - 1].location + .001 : -2;
            const max = drag.index + 1 < node.points.length ? node.points[drag.index + 1].location - .001 : 2;
            point.location = Math.max(min, Math.min(max, x));
            if (typeof point.value === 'number')
                point.value = Math.max(-64, Math.min(64, y));
        }
        clearTimeout(localTimer);
        localTimer = setTimeout(refreshLocalCurve, 60);
    };
    const finish = event => {
        if (!drag)
            return;
        drag = null;
        canvas.dragGraph = null;
        if (canvas.hasPointerCapture(event.pointerId))
            canvas.releasePointerCapture(event.pointerId);
        clearTimeout(localTimer);
        syncRaw();
        refreshIndicators();
        refreshPreviewState();
        renderSpline();
        if ($('auto-preview').checked)
            schedulePreview();
    };
    canvas.onpointerup = finish;
    canvas.onpointercancel = finish;
}
function refreshSplineCurve() {
    if (tab !== 'spline' || !splineCanvas)
        return;
    const g = config.splines[gridKind];
    if (g.tree !== undefined) {
        nestedCurve();
        return;
    }
    graph(splineCanvas, gridKind, null, g.groundness[Math.floor(cell / g.smoothness.length)],
          g.smoothness[cell % g.smoothness.length]);
}
function renderSpline() {
    nodePath = nodePath || [];
    localCanvas = null;
    splineCanvas = null;
    $('spline-preview').replaceChildren();
    $('spline-form').replaceChildren();
    splineForm($('spline-form'));
    renderRelationships();
    refreshLocalCurve();
}
async function nestedCurve() {
    const canvas = splineCanvas;
    if (!canvas)
        return;
    const job = canvas.requestId = (canvas.requestId || 0) + 1;
    try {
        const r = await request('curve', {
            config : copy(config),
            kind : gridKind,
            axis : previewAxis,
            groundness : previewGroundness,
            smoothness : previewSmoothness,
            preview_weirdness : previewWeirdness
        });
        if (canvas === splineCanvas && canvas.isConnected && canvas.requestId === job)
            drawGraph(canvas, r.points, []);
    } catch (e) {
        if (canvas === splineCanvas && canvas.isConnected && canvas.requestId === job)
            status(e.message, true);
    }
}
function nestedSplineForm(parent) {
    splineMode = 'curve';
    const preview = $('spline-preview');
    preview.hidden = false;
    section(
        parent, '스플라인 연결 관계',
        '왼쪽 트리에서 규칙을 고르고, 제어점 표에서 숫자와 연결된 하위 곡선을 편집하세요. 경로에는 각 단계의 입력축과 제어점 위치가 표시됩니다.');
    const actions = el('div', undefined, 'spline-toolbar');
    actions.append(button('전체 규칙 복사', () => {
        cellClipboard = {value : copy(config.splines[gridKind].tree)};
        renderSpline();
        status(splineNames[gridKind] + ' 전체 규칙을 복사했습니다.');
    }));
    const paste = button('전체 규칙 붙여넣기', () => {
        if (cellClipboard?.value == null || !confirm('현재 전체 규칙을 복사한 규칙으로 교체할까요?'))
            return;
        mutate(() => config.splines[gridKind].tree = copy(cellClipboard.value));
        nodePath = [];
        renderSpline();
    });
    paste.disabled = cellClipboard?.value == null;
    actions.append(
        paste,
        button('원본 스플라인 불러오기',
               () => run(async () => {
                   if (!confirm(
                           'Offset, Factor, Jaggedness와 높이·잔굴곡 배율을 모두 원본 설정으로 교체할까요?'))
                       return;
                   const version = generation,
                         r = await request('preset', {config : copy(config), name : 'original'});
                   if (version !== generation)
                       throw Error('편집 내용이 변경됐습니다. 다시 선택하세요.');
                   mutate(() => config = r.config);
                   nodePath = [];
                   renderForm();
               })));
    parent.append(actions);
    preview.append(
        el('h2', '곡선 미리보기'),
        el('p',
           '아래 값은 미리보기용입니다. 생성 규칙이나 월드의 노이즈를 변경하지 않습니다. PV는 Weirdness에서 계산됩니다.',
           'help'));
    const row = el('div', undefined, 'preview-inputs');
    for (const [name, get, set] of [[ 'GND', () => previewGroundness, v => previewGroundness = v ],
                                    [ 'Smoothness', () => previewSmoothness, v => previewSmoothness = v ],
                                    [ 'Weirdness', () => previewWeirdness, v => previewWeirdness = v ]]) {
        const label = el('label', '고정 입력 · ' + name);
        const sample = input(get(), v => {
            set(Math.max(-2, Math.min(2, v)));
            sample.value = get();
            nestedCurve();
            refreshLocalCurve();
        });
        sample.min = -2;
        sample.max = 2;
        label.append(sample);
        row.append(label);
    }
    preview.append(row);
    nodeEditor(parent, preview);
    preview.append(el('h3', `${splineNames[gridKind]} 전체 규칙의 최종 단면`));
    const axisLabel = el('label', '변화시킬 입력축');
    axisLabel.append(
        select({groundness : 'GND', smoothness : 'Smoothness', weirdness : 'Weirdness'}, previewAxis, v => {
            previewAxis = v;
            nestedCurve();
        }));
    preview.append(
        axisLabel,
        el('p',
           '선택한 입력축만 변화시키고 나머지 입력은 고정합니다. 전체 중첩 규칙을 계산한 원시 출력이며 블록 높이가 아닙니다. 아래 지도는 월드 좌표에서의 결과입니다.',
           'help'));
    splineCanvas = el('canvas', undefined, 'curve final-curve');
    splineCanvas.setAttribute('aria-label', '전체 스플라인 최종 단면');
    preview.append(splineCanvas);
    nestedCurve();
}
function splineForm(parent) {
    if (config.splines[gridKind].tree !== undefined) {
        nestedSplineForm(parent);
        return;
    }

    const toolbar = el('div', undefined, 'spline-toolbar');
    for (const [mode, title] of [[ 'grid', '▦ 격자' ], [ 'curve', '⌁ 곡선' ]]) {
        const b = button(title, () => {
            splineMode = mode;
            renderSpline();
        });
        b.classList.toggle('active', splineMode === mode);
        toolbar.append(b);
    }
    const copyCell = button('칸 복사', () => {
        cellClipboard = {value : copy(config.splines[gridKind].cells[cell])};
        renderSpline();
    });
    const pasteCell = button('칸 붙여넣기', () => {
        const g = config.splines[gridKind];
        if (cellClipboard.value === null && g.cells[cell] !== null &&
            g.cells.filter(v => v !== null).length === 1) {
            status('최소 한 칸에는 값을 남겨 두세요.', true);
            return;
        }
        mutate(() => g.cells[cell] = copy(cellClipboard.value));
        nodePath = [];
        renderSpline();
    });
    pasteCell.disabled = !cellClipboard;
    toolbar.append(copyCell, pasteCell, el('span', 'C = Groundness · E = Smoothness', 'help'));
    parent.append(toolbar);
    const layout = el('div', undefined, 'spline-layout'), left = el('section', undefined, 'spline-region'),
          right = el('section', undefined, 'spline-detail');
    layout.append(left, right);
    left.hidden = splineMode !== 'grid';
    right.hidden = splineMode !== 'curve';
    parent.append(layout);
    section(
        left, '1. 지역 선택',
        '행은 대륙성, 열은 완만함입니다. 칸을 누르면 곡선을 엽니다. W/PV는 곡선, 숫자는 고정값, ·은 이웃 보간입니다.');
    const g = config.splines[gridKind];
    cell = Math.max(0, Math.min(cell, g.cells.length - 1));
    const wrap = el('div', undefined, 'grid-wrap'), table = el('table'), head = el('tr');
    head.append(el('th', 'C / E'));
    g.smoothness.forEach(v => head.append(el('th', String(v))));
    table.append(head);
    g.groundness.forEach((c, ci) => {
        const tr = el('tr');
        tr.append(el('th', String(c)));
        g.smoothness.forEach((e, ei) => {
            const index = ci * g.smoothness.length + ei, n = g.cells[index], td = el('td');
            const b = button(n === null              ? '·'
                             : typeof n === 'number' ? Number(n).toFixed(2)
                             : n.axis === 'pv'       ? 'PV'
                                                     : 'W',
                             () => {
                                 cell = index;
                                 splineMode = 'curve';
                                 nodePath = [];
                                 renderSpline();
                             });
            b.classList.toggle('active', index === cell);
            b.classList.add(n === null              ? 'cell-empty'
                            : typeof n === 'number' ? 'cell-constant'
                                                    : 'cell-curve');
            b.title = `C ${c} / E ${e} · 클릭하여 편집`;
            b.setAttribute('aria-label', `대륙성 ${c}, 완만함 ${e}`);
            b.setAttribute('aria-pressed', String(index === cell));
            td.append(b);
            tr.append(td);
        });
        table.append(tr);
    });
    wrap.append(table);
    left.append(wrap);
    const c = g.groundness[Math.floor(cell / g.smoothness.length)],
          e = g.smoothness[cell % g.smoothness.length];
    const coordinates = el('div', undefined, 'cell-coordinates');
    const choose = (key, value) => {
        const options = Object.fromEntries(g[key].map((v, i) => [i, v]));
        const label = el('label', key === 'groundness' ? 'C · Groundness' : 'E · Smoothness');
        label.append(select(options, value, v => {
            cell = key === 'groundness'
                       ? Number(v) * g.smoothness.length + cell % g.smoothness.length
                       : Math.floor(cell / g.smoothness.length) * g.smoothness.length + Number(v);
            nodePath = [];
            renderSpline();
        }));
        coordinates.append(label);
    };
    choose('groundness', Math.floor(cell / g.smoothness.length));
    choose('smoothness', cell % g.smoothness.length);
    right.append(coordinates);
    section(right, `곡선 편집 · C ${c} / E ${e}`,
            '그래프는 선택한 칸의 최종 Weirdness 단면입니다. PV 및 하위 곡선의 결과도 함께 반영됩니다.');
    const finalCurve = el('details');
    finalCurve.open = true;
    finalCurve.append(el('summary', '선택한 칸의 최종 결과 · Weirdness 단면'));
    splineCanvas = el('canvas', undefined, 'curve final-curve');
    finalCurve.append(splineCanvas);
    right.append(finalCurve);
    if (splineMode === 'curve')
        graph(splineCanvas, gridKind, null, c, e);
    const empty = button(g.cells[cell] === null ? '이 칸에 값 지정' : '이 칸을 이웃 보간으로', () => {
        if (g.cells[cell] !== null && g.cells.filter(v => v !== null).length === 1) {
            status('최소 한 칸에는 값을 남겨 두세요.', true);
            return;
        }
        if (g.cells[cell] !== null && !confirm('이 칸의 곡선을 지우고 이웃 값으로 보간할까요?'))
            return;
        mutate(() => g.cells[cell] = g.cells[cell] === null ? 0 : null);
        nodePath = [];
        renderSpline();
    });
    right.append(empty);
    if (g.cells[cell] !== null)
        nodeEditor(right);
    else
        right.append(el('p', '이 칸은 주변에 지정된 값을 이어서 계산합니다.', 'help'));
    const axes = el('details');
    axes.append(el('summary', '표의 C/E 경계 편집'));
    axes.append(el('p',
                   '쉼표로 구분한 오름차순 숫자를 입력하세요. 같은 좌표의 칸은 유지하고 새 칸은 비웁니다.',
                   'help'));
    const inputs = {};
    for (const key of ['groundness', 'smoothness']) {
        const l = el('label', key === 'groundness' ? 'C 경계' : 'E 경계'), i = el('input');
        i.value = g[key].join(', ');
        inputs[key] = i;
        l.append(i);
        axes.append(l);
    }
    axes.append(button('경계 반영', () => {
        try {
            const axes2 = {};
            for (const k of ['groundness', 'smoothness']) {
                const vals = inputs[k].value.split(',').map(x => Number(x.trim()));
                if (!vals.length || vals.length > 24 ||
                    vals.some((v, i) => !Number.isFinite(v) || v < -2 || v > 2 || (i && v <= vals[i - 1])))
                    throw Error('경계는 -2~2의 오름차순 숫자 1~24개입니다.');
                axes2[k] = vals;
            }
            mutate(() => {
                const cells = [];
                for (const c of axes2.groundness)
                    for (const e of axes2.smoothness) {
                        const ci = g.groundness.indexOf(c), ei = g.smoothness.indexOf(e);
                        cells.push(ci < 0 || ei < 0 ? null : copy(g.cells[ci * g.smoothness.length + ei]));
                    }
                Object.assign(g, axes2, {cells});
                cell = 0;
            });
            nodePath = [];
            renderSpline();
        } catch (err) {
            status(err.message, true);
        }
    }));
    left.append(axes);
    const presets = el('details');
    presets.append(el('summary', '스플라인 프리셋'));
    for (const [n, label] of [[ 'original', '원본' ], [ 'diverse', '다채로운 내륙' ],
                              [ 'ridged', '능선·급경사' ]])
        presets.append(button(label, () => run(async () => {
                                         const startGeneration = generation;
                                         const r = await request('preset', {config, name : n});
                                         if (startGeneration !== generation)
                                             throw Error(
                                                 '계산 중 작업이 변경됐어요. 프리셋을 다시 선택해 주세요.');
                                         mutate(() => config = r.config);
                                         nodePath = [];
                                         renderSpline();
                                         status('스플라인 표만 바꿨습니다. 저장 전 지도를 확인하세요.');
                                     })));
    left.append(presets);
}
const signalInfo = {
    groundness : [ '대륙성', 'Groundness', '바다와 내륙의 큰 틀을 정합니다.' ],
    smoothness : [ '완만함', 'Smoothness', '평지와 거친 지역을 나누는 입력입니다.' ],
    weirdness : [ '봉우리와 계곡', 'Weirdness → PV', '지역별 능선과 계곡의 배치를 정합니다.' ],
    jagged : [ '산의 잔굴곡', 'Jagged noise', '산 표면의 작은 굴곡을 만듭니다.' ],
    warp : [ '공유 Shift', 'Shared Shift', '주기적 3D 노이즈의 두 단면으로 좌표를 변형합니다.' ],
    temperature : [ '온도', 'Temperature', '적도에서 따뜻하고 극지에서 차가워집니다.' ],
    precipitation : [ '강수량', 'Precipitation', '건조하고 습윤한 지역을 나눕니다.' ]
};
function navigate(next, key) {
    tab = next;
    nodePath = [];
    if (next === 'spline') {
        gridKind = key;
        cell = 0;
        splineMode = 'grid';
    } else if (next === 'climate')
        climateSignal = key;
    else if (next === 'noise')
        selectedSignal = key;
    $('map').value = next === 'spline'    ? key
                     : next === 'terrain' ? 'effective_height'
                     : key === 'jagged'   ? 'jagged_noise'
                     : key === 'warp'     ? 'groundness'
                                          : key;
    renderForm();
    schedulePreview();
}
function renderNavigation() {
    const list = $('tabs');
    list.replaceChildren();
    const group = (title, entries) => {
        list.append(el('h3', title, 'nav-heading'));
        for (const [section, key, title, subtitle] of entries) {
            const b = button('', () => navigate(section, key));
            const mark = el('span',
                            section === 'spline'    ? '▦'
                            : section === 'terrain' ? '⌁'
                                                    : '≋',
                            'nav-icon');
            const label = el('span');
            label.append(el('strong', title), el('small', subtitle));
            b.append(mark, label);
            const active = tab === section && (tab === 'spline'    ? gridKind === key
                                               : tab === 'noise'   ? selectedSignal === key
                                               : tab === 'climate' ? climateSignal === key
                                                                   : true);
            b.classList.toggle('active', active);
            b.setAttribute('aria-pressed', String(active));
            list.append(b);
            if (active && section === 'spline' && config.splines[key].tree !== undefined) {
                const relations = el('div', undefined, 'spline-relations');
                relations.id = 'spline-relations';
                list.append(relations);
            }
        }
    };
    group('지형', [
        [ 'spline', 'offset', '높이 오프셋', 'Offset' ], [ 'spline', 'factor', '압축', 'Factor' ],
        [ 'spline', 'jaggedness', '잔굴곡 강도', 'Jaggedness' ],
        [ 'terrain', 'effective_height', '생성 설정', '배율 · 3D 노이즈' ]
    ]);
    group('노이즈', [ 'groundness', 'smoothness', 'weirdness', 'jagged', 'warp' ].map(
                        key => ['noise', key, signalInfo[key][0], signalInfo[key][1]]));
    group('기후', [ 'temperature', 'precipitation' ].map(
                      key => ['climate', key, signalInfo[key][0], signalInfo[key][1]]));
}
function renderForm() {
    if (!config)
        return;
    const parent = $('form');
    if (parent.dataset.tab)
        openSections.set(parent.dataset.tab, new Set([...parent.querySelectorAll('details[open]') ].map(
                                                 d => d.querySelector('summary').textContent)));
    parent.dataset.tab = tab;
    parent.replaceChildren();
    $('editor-tools').replaceChildren();
    $('spline-panel').hidden = tab !== 'spline';
    parent.hidden = tab === 'spline';
    renderNavigation();
    $('spline-preview').hidden = true;
    document.body.classList.toggle('relationships-open', tab === 'spline');
    if (tab === 'spline') {
        $('editor-category').textContent = '지형 / 스플라인';
        $('editor-title').textContent = maps[gridKind];
        renderSpline();
    } else if (tab === 'noise' || tab === 'climate') {
        const key = tab === 'climate' ? climateSignal : selectedSignal;
        const [title, subtitle, description] = signalInfo[key];
        $('editor-category').textContent = tab === 'climate' ? '기후' : '노이즈';
        $('editor-title').textContent = title + ' · ' + subtitle;
        parent.append(el('p', description, 'help'));
        if (key === 'warp')
            warpList(parent);
        else
            noise(parent, key, '노이즈 설정');
        if (key === 'temperature') {
            const d = el('details');
            d.append(el('summary', '위도별 온도'));
            scalarFields(d, config.temperature_bands);
            parent.append(d);
        }
        if (tab === 'noise') {
            const d = el('details');
            d.append(el('summary', '월드 시드'));
            field(d, config, 'seed');
            parent.append(d);
        }
    } else {
        $('editor-category').textContent = '지형 / 생성 설정';
        $('editor-title').textContent = '생성 설정';
        const scale = el('section');
        section(scale, '스플라인 배율');
        scalarFields(scale, config.splines);
        parent.append(scale);
        const shape = el('details');
        shape.append(el('summary', '3D 굴곡과 압축'));
        scalarFields(shape, config, [ 'shape_enabled', 'amplitude', 'squash' ]);
        section(shape, '주기적 Blended Noise',
                '하한16겹 / 상한16겹 / 혼합8겹. 원본의 혼합과 세로 Smear를 사용합니다.');
        scalarFields(shape, config.blended);
        field(shape, config.shape, 'seed_offset');
        parent.append(shape);
    }
    for (const detail of parent.querySelectorAll('details'))
        detail.open = openSections.get(tab)?.has(detail.querySelector('summary').textContent) ?? false;
    syncRaw();
    updateOctaves();
    refreshIndicators();
    refreshPreviewState();
}
function refreshIndicators() {
    if (!config)
        return;
    const signature = JSON.stringify(config), dirty = signature !== savedSignature;
    $('save-state').textContent = rawEdited                          ? 'JSON 반영 대기'
                                  : dirty                            ? '● 저장하지 않은 변경'
                                  : signature !== publishedSignature ? '작업 저장됨 · 게임 미확정'
                                                                     : '게임 규칙과 같음';
    $('save-state').classList.toggle('dirty', dirty || rawEdited);
    $('undo').disabled = !undo.length;
}
function updateOctaves() {
    if (!config)
        return;
    const old = $('octave').value;
    $('octave').replaceChildren();
    const o = el('option', '전체 합성');
    o.value = -1;
    $('octave').append(o);
    for (let i = 0; i < Math.min(config.groundness.octaves, 16); i++) {
        const o = el('option', `${i + 1}번 옥타브`);
        o.value = i;
        $('octave').append(o);
    }
    $('octave').value = Number(old) < config.groundness.octaves ? old : '-1';
    $('octave').disabled = $('map').value !== 'groundness';
    $('weighted').disabled = $('octave').disabled || $('octave').value === '-1';
}
async function run(fn) {
    try {
        await fn();
    } catch (e) {
        status(e.message, true);
    }
}
function download(blob, name) {
    const a = el('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
function colour(kind, v) {
    let t = Math.max(0, Math.min(1, v * .5 + .5)), a = [ 0, 0, 0 ], b = [ 255, 255, 255 ];
    if (kind === 'temperature') {
        a = [ 30, 80, 230 ];
        b = [ 240, 55, 25 ];
    }
    if (kind === 'precipitation') {
        a = [ 165, 105, 45 ];
        b = [ 35, 120, 235 ];
    }
    if (kind === 'factor')
        t = Math.max(0, Math.min(1, v / .05));
    if (kind === 'jaggedness')
        t = Math.max(0, Math.min(1, v));
    if (kind === 'base_height' || kind === 'effective_height') {
        if (v < 192) {
            a = [ 10, 30, 95 ];
            b = [ 60, 155, 225 ];
            t = Math.max(0, Math.min(1, v / 192));
        } else {
            a = [ 65, 130, 55 ];
            b = [ 250, 250, 245 ];
            t = Math.max(0, Math.min(1, (v - 192) / 320));
        }
    }
    return a.map((x, i) => Math.round(x + (b[i] - x) * t));
}
async function loadState() {
    const r = await request('state');
    if (config)
        changed();
    config = r.config;
    revision = r.revision;
    savedSignature = JSON.stringify(config);
    publishedSignature = savedSignature;
    comparisonConfig = copy(config);
    $('compare-label').textContent = '비교 기준: 불러온 확정 규칙';
    rawEdited = false;
    renderForm();
    schedulePreview();
    status('확정 규칙을 불러왔습니다. 저장 위치: ' + r.published_path);
}
$('reload').onclick = () => {
    if (confirm('저장하지 않은 초안을 확정 파일로 바꿀까요?'))
        run(loadState);
};
$('draft-load').onclick = () => {
    if (confirm('저장된 초안을 불러올까요?'))
        run(async () => {
            const r = await request('draft');
            mutate(() => config = r.config);
            savedSignature = JSON.stringify(config);
            rawEdited = false;
            renderForm();
            status('초안을 불러왔습니다. 확정 파일은 바뀌지 않았습니다.');
        });
};
$('draft-save').onclick = () => run(async () => {
    if (rawEdited)
        throw Error('JSON 편집을 먼저 작업에 반영하거나 현재 작업으로 되돌려 주세요.');
    const snapshot = copy(config);
    const r = await request('draft', {config : snapshot});
    savedSignature = JSON.stringify(snapshot);
    status(r.message);
});
$('publish').onclick = () => {
    if (rawEdited) {
        status('JSON 편집을 먼저 작업에 반영하거나 현재 작업으로 되돌려 주세요.', true);
        return;
    }
    if (!confirm('이 규칙을 게임용으로 확정 저장할까요? 기존 확정 파일은 백업됩니다.'))
        return;
    run(async () => {
        const g = generation;
        $('publish').disabled = true;
        try {
            const snapshot = copy(config);
            const r = await request('publish', {config : snapshot, revision});
            revision = r.revision;
            savedSignature = JSON.stringify(snapshot);
            publishedSignature = savedSignature;
            status(r.message + (g === generation ? '' : ' 저장 중 수정한 내용은 아직 미저장입니다.'));
        } finally {
            $('publish').disabled = false;
        }
    });
};
$('validate').onclick = () => run(async () => {
    await request('validate', {config});
    status('게임 생성기 검증을 통과했습니다.');
});
$('undo').onclick = () => {
    if (!undo.length)
        return;
    config = undo.pop();
    generation++;
    renderForm();
    if ($('auto-preview').checked)
        schedulePreview();
    else
        $('map-state').textContent = '되돌린 초안으로 지도를 다시 생성하세요.';
};
$('export-json').onclick = () =>
    download(new Blob([ JSON.stringify(config, null, 2) ], {type : 'application/json'}), 'worldgen.json');
$('raw').oninput = () => {
    rawEdited = true;
    refreshIndicators();
};
$('refresh-json').onclick = () => {
    if (rawEdited && !confirm('직접 편집한 JSON을 현재 초안으로 바꿀까요?'))
        return;
    rawEdited = false;
    syncRaw();
    refreshIndicators();
};
$('apply-json').onclick = () => run(async () => {
    if (rawGeneration !== generation && !confirm('다른 항목도 수정됐습니다. 이 JSON으로 초안을 교체할까요?'))
        return;
    const r = await request('validate', {config : JSON.parse($('raw').value)});
    mutate(() => config = r.config);
    rawEdited = false;
    renderForm();
    status('JSON을 초안에 반영했습니다.');
});
$('import-json').onchange = () => run(async () => {
    const file = $('import-json').files[0];
    if (!file)
        return;
    if (file.size > 4194304)
        throw Error('설정 파일은 4 MiB 이하입니다.');
    const r = await request('validate', {config : JSON.parse(await file.text())});
    mutate(() => config = r.config);
    rawEdited = false;
    renderForm();
    status('파일을 초안으로 가져왔습니다.');
    $('import-json').value = '';
});
$('shutdown').onclick = () => {
    if (confirm('편집기를 종료할까요? 저장하지 않은 내용은 사라집니다.'))
        run(async () => status((await request('shutdown', {})).message));
};
$('theme-toggle').onclick = () => {
    const dark = document.body.classList.toggle('dark');
    document.body.classList.toggle('light', !dark);
    $('theme-toggle').textContent = dark ? '밝은 화면' : '어두운 화면';
    $('theme-toggle').setAttribute('aria-pressed', String(dark));
    if (config)
        renderForm();
};
$('map-toggle').onclick = () => {
    const closed = document.body.classList.toggle('map-closed');
    document.body.classList.remove('map-expanded');
    $('map-expand').textContent = '지도 크게';
    $('map-expand').setAttribute('aria-pressed', 'false');
    $('map-toggle').textContent = closed ? '지도 펼치기' : '지도 접기';
    $('map-toggle').setAttribute('aria-expanded', String(!closed));
};
$('map-expand').onclick = () => {
    const expanded = document.body.classList.toggle('map-expanded');
    $('map-expand').textContent = expanded ? '편집 화면으로' : '지도 크게';
    $('map-expand').setAttribute('aria-pressed', String(expanded));
};
$('auto-preview').onchange = () => {
    if ($('auto-preview').checked)
        schedulePreview();
};
$('files-open').onclick = () => $('files-dialog').showModal();
window.referenceSnapshot = () => config ? {config : copy(config), generation} : null;
$('minecraft-open').onclick = () => { window.open('/minecraft.html#' + token, 'minecraft-reference'); };
$('files-close').onclick = () => $('files-dialog').close();
for (const [k, label] of Object.entries(maps)) {
    const o = el('option', label);
    o.value = k;
    $('map').append(o);
}
$('map').value = gridKind;
window.addEventListener('beforeunload', e => {
    if (config && (rawEdited || JSON.stringify(config) !== savedSignature)) {
        e.preventDefault();
        e.returnValue = '';
    }
});
