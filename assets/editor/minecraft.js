'use strict';
(() => {
    const $ = id => document.getElementById(id);
    const token = location.hash.slice(1) || sessionStorage.getItem('editor-token') || '';
    if (token)
        sessionStorage.setItem('editor-token', token);
    history.replaceState(null, '', location.pathname);
    const fields = {
        groundness : [ 'Continentalness / Groundness', '대륙과 바다의 큰 배치를 정하는 신호를 비교합니다.' ],
        smoothness : [ 'Erosion / Smoothness', '침식에 대응하는 원본 신호와 우리 Smoothness를 비교합니다.' ],
        weirdness : [ 'Weirdness', '봉우리와 계곡 배치의 원본 신호입니다.' ],
        pv : [ 'PV · 봉우리 / 계곡', 'Weirdness를 접어서 만든 값입니다. 별도 노이즈가 아닙니다.' ],
        temperature :
            [ 'Temperature · 온도', '원본은 기후 노이즈, 우리 월드는 위도와 기후 노이즈를 합친 값입니다.' ],
        precipitation :
            [ 'Humidity / 강수량', '원본 Vegetation(습도)과 우리 Precipitation(강수량)을 비교합니다.' ],
        jagged_noise : [ 'Jagged · 잔굴곡 노이즈', '잔굴곡 강도와 곱하기 전의 원본 신호입니다.' ],
        offset : [
            'Offset · 높이 오프셋',
            '양쪽 스플라인 자체 출력입니다. 원본의 −0.50375 보정과 우리 높이 배율은 제외합니다.'
        ],
        factor :
            [ 'Factor · 압축 계수', '양쪽 스플라인 자체 출력입니다. 우리 전체 압축 강도와 곱하기 전입니다.' ],
        jaggedness : [
            'Jaggedness · 잔굴곡 강도',
            '양쪽 스플라인 자체 출력입니다. 잔굴곡 노이즈·배율·사용 여부를 적용하기 전입니다.'
        ]
    };
    let config = null, configSource = '', view = 'map', shown = null, curveData = null, probeData = null;
    let timer, busy = false, queued = false, version = 0, probeVersion = 0, drag = null;
    const limit = 30000000;
    function status(s, error = false) {
        $('status').textContent = s;
        $('status').classList.toggle('error', error);
    }
    async function api(path, body, binary = false) {
        const r = await fetch('/api/' + path, {
            method : body === undefined ? 'GET' : 'POST',
            headers : {'X-Editor-Token' : token, 'Content-Type' : 'application/json'},
            body : body === undefined ? undefined : JSON.stringify(body)
        });
        if (!r.ok) {
            let message = await r.text();
            try {
                message = JSON.parse(message).error || message;
            } catch {
            }
            throw Error(message);
        }
        return binary ? r.arrayBuffer() : r.json();
    }
    function seed() {
        const s = $('seed').value.trim();
        if (!/^-?\d+$/.test(s))
            throw Error('Minecraft 시드는 정수로 입력해 주세요.');
        const n = BigInt(s);
        if (n < -(1n << 63n) || n >= (1n << 63n))
            throw Error('Minecraft 시드의 64비트 범위를 벗어났습니다.');
        return n.toString();
    }
    function readRange() {
        const r = {};
        for (const k of ['x0', 'z0', 'x1', 'z1'])
            r[k] = $(k).valueAsNumber;
        if (Object.values(r).some(n => !Number.isFinite(n) || Math.abs(n) > limit) || r.x1 <= r.x0 ||
            r.z1 <= r.z0)
            throw Error('좌표는 ±30,000,000 이내이며 끝 좌표가 시작보다 커야 합니다.');
        return r;
    }
    function setRange(r) {
        for (const [a, b] of [[ 'x0', 'x1' ], [ 'z0', 'z1' ]]) {
            const size = Math.min(2 * limit, Math.max(1, r[b] - r[a]));
            r[a] = Math.max(-limit, Math.min(limit - size, r[a]));
            r[b] = r[a] + size;
        }
        for (const k of ['x0', 'z0', 'x1', 'z1'])
            $(k).value = String(Math.round(r[k] * 100) / 100);
    }
    function options() {
        return {
            seed : view === 'map' ? seed() : '0',
            compare : $('compare').checked,
            config : structuredClone(config)
        };
    }
    function fixedInputs() {
        const v = {
            groundness : $('curve-c').valueAsNumber,
            smoothness : $('curve-e').valueAsNumber,
            weirdness : $('curve-w').valueAsNumber
        };
        if (Object.values(v).some(n => !Number.isFinite(n) || n < -2 || n > 2))
            throw Error('곡선 입력값은 −2~2입니다.');
        return v;
    }
    function sync() {
        const compare = $('compare').checked;
        $('ours-pane').hidden = !compare;
        $('map-pair').classList.toggle('solo', !compare);
        $('ours-curve-key').hidden = !compare;
        $('kind-help').textContent = fields[$('kind').value][1];
        const axis = $('curve-axis').value;
        for (const [id, key] of [[ 'curve-c', 'groundness' ], [ 'curve-e', 'smoothness' ],
                                 [ 'curve-w', 'weirdness' ]])
            $(id).disabled = axis === key;
        $('ours-info').textContent =
            config ? `${configSource} · 시드 ${config.seed.toLocaleString()}` : '설정 없음';
        try {
            const r = readRange();
            $('range-label').textContent =
                `X ${Math.floor(r.x0).toLocaleString()} ~ ${Math.floor(r.x1).toLocaleString()} / Z ${
                    Math.floor(r.z0).toLocaleString()} ~ ${Math.floor(r.z1).toLocaleString()}`;
        } catch {
        }
        $('refresh').textContent = busy ? '계산 중…' : '미리보기 갱신';
        $('refresh').disabled = busy || !config;
    }
    async function loadOurs(initial = false) {
        let snapshot;
        try {
            snapshot = window.opener && !window.opener.closed ? window.opener.referenceSnapshot?.() : null;
        } catch {
        }
        if (snapshot) {
            config = structuredClone(snapshot.config);
            configSource = '편집기 작업 사본';
        } else {
            config = (await api('state')).config;
            configSource = '게임 확정 규칙';
        }
        if (initial)
            $('seed').value = String(config.seed);
        sync();
        schedule();
    }
    function schedule() {
        ++version;
        ++probeVersion;
        probeData = null;
        $('probe-curves').disabled = true;
        $('probe').textContent = '지도에서 위치를 클릭하면 현재 결과의 값을 조회합니다.';
        clearTimeout(timer);
        sync();
        status('설정이 바뀌었어요. 미리보기를 준비합니다…');
        timer = setTimeout(() => void render(), 220);
    }
    async function render() {
        clearTimeout(timer);
        if (!config)
            return;
        if (busy) {
            queued = true;
            return;
        }
        const job = version, target = view;
        busy = true;
        queued = false;
        sync();
        status('계산 중 · 표시된 지도와 곡선은 직전 결과입니다.');
        const start = performance.now();
        try {
            const body = options();
            if (target === 'map') {
                const r = readRange(), kind = $('kind').value;
                const data = new Float32Array(
                    await api('minecraft/preview',
                              {...body, ...r, kind, resolution : Number($('resolution').value)}, true));
                const w = data[0], h = data[1], n = w * h;
                if (!Number.isInteger(w) || !Number.isInteger(h) || w < 2 || h < 2 ||
                    data.length !== 2 + n * (body.compare ? 2 : 1))
                    throw Error('지도 응답 크기가 올바르지 않습니다.');
                if (job === version) {
                    shown = {
                        w,
                        h,
                        range : r,
                        kind,
                        body,
                        mc : data.slice(2, 2 + n),
                        ours : body.compare ? data.slice(2 + n) : null,
                        version : job
                    };
                    drawMaps();
                }
            } else {
                const input = fixedInputs(), axis = $('curve-axis').value, result = {};
                for (const kind of ['offset', 'factor', 'jaggedness']) {
                    if (job !== version)
                        break;
                    result[kind] = (await api('minecraft/curve', {...body, ...input, axis, kind})).points;
                }
                if (job === version) {
                    curveData = {result, compare : body.compare, axis};
                    drawCurves();
                }
            }
            if (job === version)
                status(`현재 설정으로 계산 완료 · ${((performance.now() - start) / 1000).toFixed(2)}초`);
        } catch (e) {
            if (job === version)
                status(e.message, true);
        } finally {
            busy = false;
            sync();
            if (queued || job !== version) {
                queued = false;
                void render();
            }
        }
    }
    function bounds(values) {
        let low = Infinity, high = -Infinity;
        for (const v of values) {
            low = Math.min(low, v);
            high = Math.max(high, v);
        }
        return [ low, high ];
    }
    function sizeCanvas(canvas) {
        const ratio = Math.min(2, devicePixelRatio || 1),
              w = Math.max(200, Math.round(canvas.clientWidth * ratio)),
              h = Math.max(120, Math.round(canvas.clientHeight * ratio));
        if (canvas.width !== w || canvas.height !== h) {
            canvas.width = w;
            canvas.height = h;
        }
        return canvas.getContext('2d');
    }
    function imageRect(canvas) {
        const scale = Math.min(canvas.width / shown.w, canvas.height / shown.h), w = shown.w * scale,
              h = shown.h * scale;
        return {x : (canvas.width - w) / 2, y : (canvas.height - h) / 2, w, h};
    }
    function drawMap(canvas, values, lo, hi) {
        const ctx = sizeCanvas(canvas);
        ctx.fillStyle = '#202b35';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        const image = document.createElement('canvas');
        image.width = shown.w;
        image.height = shown.h;
        const out = image.getContext('2d'), pixels = out.createImageData(shown.w, shown.h);
        for (let i = 0; i < values.length; i++) {
            const b = Math.round(Math.max(0, Math.min(1, (values[i] - lo) / (hi - lo))) * 255);
            pixels.data.set([ b, b, b, 255 ], i * 4);
        }
        out.putImageData(pixels, 0, 0);
        const r = imageRect(canvas);
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(image, r.x, r.y, r.w, r.h);
        if (probeData) {
            const x = r.x + (probeData.x - shown.range.x0) / (shown.range.x1 - shown.range.x0) * r.w,
                  y = r.y + (probeData.z - shown.range.z0) / (shown.range.z1 - shown.range.z0) * r.h;
            ctx.strokeStyle = '#ee942e';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(x - 8, y);
            ctx.lineTo(x + 8, y);
            ctx.moveTo(x, y - 8);
            ctx.lineTo(x, y + 8);
            ctx.stroke();
        }
    }
    function drawMaps() {
        if (!shown || view !== 'map')
            return;
        const a = bounds(shown.mc), b = shown.ours ? bounds(shown.ours) : a;
        let [lo, hi] = shown.kind === 'factor'       ? [ 0, 8 ]
                       : shown.kind === 'jaggedness' ? [ 0, 1 ]
                       : shown.kind === 'offset'     ? [ -.3, 1.2 ]
                                                     : [ -1, 1 ];
        if ($('auto-range').checked) {
            lo = Math.min(a[0], b[0]);
            hi = Math.max(a[1], b[1]);
            if (hi - lo < 1e-9) {
                lo -= .5;
                hi += .5;
            }
        }
        $('low-label').textContent = lo.toFixed(3);
        $('high-label').textContent = hi.toFixed(3);
        drawMap($('mc-map'), shown.mc, lo, hi);
        if (shown.ours)
            drawMap($('ours-map'), shown.ours, lo, hi);
        $('mc-stats').textContent =
            `최소 ${a[0].toFixed(5)} · 최대 ${a[1].toFixed(5)} · ${shown.w} × ${shown.h} 표본`;
        $('ours-stats').textContent = shown.ours ? `최소 ${b[0].toFixed(5)} · 최대 ${b[1].toFixed(5)}` : '';
    }
    function drawCurves() {
        if (!curveData || view !== 'curves')
            return;
        const dark = document.body.classList.contains('dark');
        for (const [kind, points] of Object.entries(curveData.result)) {
            const canvas = $('curve-' + kind), ctx = sizeCanvas(canvas), w = canvas.width, h = canvas.height;
            const margin = {l : 65, r : 18, t : 18, b : 34}, pw = w - margin.l - margin.r,
                  ph = h - margin.t - margin.b;
            let lo = Infinity, hi = -Infinity;
            for (const p of points)
                for (let i = 1; i < p.length; i++) {
                    lo = Math.min(lo, p[i]);
                    hi = Math.max(hi, p[i]);
                }
            const padding = Math.max((hi - lo) * .1, .03);
            lo -= padding;
            hi += padding;
            const x = v => margin.l + (v + 1.2) / 2.4 * pw, y = v => margin.t + (hi - v) / (hi - lo) * ph;
            ctx.clearRect(0, 0, w, h);
            ctx.font = '12px system-ui';
            ctx.fillStyle = dark ? '#b7cadb' : '#5f7082';
            ctx.strokeStyle = dark ? '#34495a' : '#d9e2e9';
            ctx.lineWidth = 1;
            for (let i = 0; i <= 4; i++) {
                const yy = margin.t + i * ph / 4, val = hi - i * (hi - lo) / 4;
                ctx.beginPath();
                ctx.moveTo(margin.l, yy);
                ctx.lineTo(w - margin.r, yy);
                ctx.stroke();
                ctx.fillText(val.toFixed(3), 5, yy + 4);
            }
            for (const xx of [-1, -.5, 0, .5, 1])
                ctx.fillText(String(xx), x(xx) - 8, h - 10);
            for (let series = 1; series <= (curveData.compare ? 2 : 1); series++) {
                ctx.strokeStyle = series === 1 ? '#2096a4' : '#d78325';
                ctx.lineWidth = 2.5;
                ctx.setLineDash(series === 1 ? [] : [ 7, 5 ]);
                ctx.beginPath();
                points.forEach((p, i) => {
                    if (i)
                        ctx.lineTo(x(p[0]), y(p[series]));
                    else
                        ctx.moveTo(x(p[0]), y(p[series]));
                });
                ctx.stroke();
            }
            ctx.setLineDash([]);
        }
    }
    function useView(next) {
        view = next;
        $('map-view').hidden = next !== 'map';
        $('curves-view').hidden = next !== 'curves';
        for (const v of ['map', 'curves']) {
            $('tab-' + v).classList.toggle('active', v === next);
            $('tab-' + v).setAttribute('aria-pressed', String(v === next));
        }
        schedule();
    }
    function zoom(scale, fx = .5, fz = .5) {
        try {
            const r = readRange(), w = (r.x1 - r.x0), h = (r.z1 - r.z0), cx = r.x0 + w * fx,
                  cz = r.z0 + h * fz;
            setRange({
                x0 : cx - w * scale * fx,
                x1 : cx + w * scale * (1 - fx),
                z0 : cz - h * scale * fz,
                z1 : cz + h * scale * (1 - fz)
            });
            schedule();
        } catch (e) {
            status(e.message, true);
        }
    }
    function pointer(canvas, e) {
        const b = canvas.getBoundingClientRect(), r = imageRect(canvas),
              x = (e.clientX - b.left) * canvas.width / b.width,
              y = (e.clientY - b.top) * canvas.height / b.height;
        return {fx : (x - r.x) / r.w, fz : (y - r.y) / r.h};
    }
    async function probe(fx, fz) {
        if (!shown || shown.version !== version)
            return;
        const job = ++probeVersion, map = shown;
        const x = Math.floor(map.range.x0 + (map.range.x1 - map.range.x0) * fx),
              z = Math.floor(map.range.z0 + (map.range.z1 - map.range.z0) * fz);
        $('probe').textContent = '좌표 값을 계산하는 중…';
        try {
            const result = await api('minecraft/probe', {...map.body, x, z});
            if (job !== probeVersion || shown !== map)
                return;
            probeData = result;
            $('probe-title').textContent = `X ${result.x.toLocaleString()} / Z ${result.z.toLocaleString()}`;
            const table = document.createElement('table'), head = document.createElement('tr');
            for (const name of ['항목', 'Minecraft 원본', ...(result.ours ? [ '우리 월드' ] : [])]) {
                const th = document.createElement('th');
                th.textContent = name;
                head.append(th);
            }
            table.append(head);
            for (const [key, [ name ]] of Object.entries(fields)) {
                const row = document.createElement('tr');
                for (const value of [name, result.minecraft[key].toFixed(7),
                                     ...(result.ours ? [ result.ours[key].toFixed(7) ] : [])]) {
                    const cell = document.createElement('td');
                    cell.textContent = value;
                    row.append(cell);
                }
                table.append(row);
            }
            $('probe').replaceChildren(table);
            $('probe-curves').disabled = false;
            drawMaps();
        } catch (e) {
            if (job === probeVersion)
                $('probe').textContent = e.message;
        }
    }
    for (const id of ['mc-map', 'ours-map']) {
        const canvas = $(id);
        canvas.addEventListener('pointerdown', e => {
            if (e.button !== 0 || !shown || shown.version !== version)
                return;
            const p = pointer(canvas, e);
            if (p.fx < 0 || p.fx > 1 || p.fz < 0 || p.fz > 1)
                return;
            drag = {
                id : e.pointerId,
                canvas,
                x : e.clientX,
                y : e.clientY,
                p,
                r : {...shown.range},
                moved : false
            };
            canvas.setPointerCapture(e.pointerId);
        });
        canvas.addEventListener('pointermove', e => {
            if (drag?.canvas === canvas && drag.id === e.pointerId)
                drag.moved ||= Math.hypot(e.clientX - drag.x, e.clientY - drag.y) > 4;
        });
        canvas.addEventListener('pointerup', e => {
            if (drag?.canvas !== canvas || drag.id !== e.pointerId)
                return;
            const d = drag;
            drag = null;
            canvas.releasePointerCapture(e.pointerId);
            if (!d.moved) {
                void probe(d.p.fx, d.p.fz);
                return;
            }
            const p = pointer(canvas, e), dx = (p.fx - d.p.fx) * (d.r.x1 - d.r.x0),
                  dz = (p.fz - d.p.fz) * (d.r.z1 - d.r.z0);
            setRange({x0 : d.r.x0 - dx, x1 : d.r.x1 - dx, z0 : d.r.z0 - dz, z1 : d.r.z1 - dz});
            schedule();
        });
        canvas.addEventListener('pointercancel', () => drag = null);
        canvas.addEventListener('wheel', e => {
            if (!shown)
                return;
            e.preventDefault();
            const p = pointer(canvas, e);
            if (p.fx >= 0 && p.fx <= 1 && p.fz >= 0 && p.fz <= 1)
                zoom(e.deltaY < 0 ? .8 : 1.25, p.fx, p.fz);
        }, {passive : false});
    }
    for (const [key, [ name ]] of Object.entries(fields)) {
        const option = document.createElement('option');
        option.value = key;
        option.textContent = name;
        $('kind').append(option);
    }
    $('kind').value = 'groundness';
    for (const id of ['kind', 'resolution', 'seed', 'compare', 'curve-axis', 'curve-c', 'curve-e', 'curve-w'])
        $(id).addEventListener('change', schedule);
    for (const id of ['x0', 'z0', 'x1', 'z1'])
        $(id).addEventListener('change', schedule);
    $('refresh').onclick = () => {
        schedule();
        void render();
    };
    $('apply-range').onclick = schedule;
    $('refresh-ours').onclick = () => void loadOurs().catch(e => status(e.message, true));
    $('seed-from-ours').onclick = () => {
        if (config) {
            $('seed').value = String(config.seed);
            schedule();
        }
    };
    $('tab-map').onclick = () => useView('map');
    $('tab-curves').onclick = () => useView('curves');
    $('zoom-in').onclick = () => zoom(.5);
    $('zoom-out').onclick = () => zoom(2);
    $('origin').onclick = () => {
        setRange({x0 : -2048, z0 : -2048, x1 : 2048, z1 : 2048});
        schedule();
    };
    $('whole').onclick = () => {
        setRange({x0 : 0, z0 : 0, x1 : 131072, z1 : 131072});
        schedule();
    };
    $('auto-range').onchange = drawMaps;
    $('probe-curves').onclick = () => {
        if (!probeData)
            return;
        const v = probeData.minecraft;
        $('curve-c').value = v.groundness;
        $('curve-e').value = v.smoothness;
        $('curve-w').value = v.weirdness;
        useView('curves');
    };
    $('theme').onclick = () => {
        const dark = document.body.classList.toggle('dark');
        $('theme').textContent = dark ? '밝은 화면' : '어두운 화면';
        drawCurves();
    };
    $('back').onclick = () => {
        if (window.opener && !window.opener.closed) {
            window.opener.focus();
        } else
            location.href = '/#' + token;
    };
    new ResizeObserver(() => {
        drawMaps();
        drawCurves();
    }).observe(document.querySelector('main'));
    void loadOurs(true).catch(e => status(e.message, true));
})();
