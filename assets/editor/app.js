"use strict";
const $ = id => document.getElementById(id);
const token = location.hash.slice(1);
let config, revision, publishedText, draftText, generation = 0, shown;
let mutationBusy = false, masterSeed, workspaceState;
const clone = value => structuredClone(value);
const text = () => JSON.stringify(config);
function status(message) { $("status").textContent = message; }
function stateLabel() {
    status(text() === publishedText ? "게임 규칙과 같은 설정입니다." :
        text() === draftText ? "작업 저장됨 · 게임 규칙으로는 아직 확정하지 않았습니다." : "저장하지 않은 변경이 있습니다.");
}
function changed() {
    masterSeed?.set(config.seed);
    generation++;
    $("json").value = JSON.stringify(config, null, 2);
    stateLabel();
    $("map-status").textContent = "편집값이 바뀌었습니다. 미리보기 생성으로 갱신하세요.";
}
async function api(path, body) {
    const response = await fetch("/api/" + path, {method: body === undefined ? "GET" : "POST",
        headers: {"X-Editor-Token": token, "Content-Type": "application/json"},
        body: body === undefined ? undefined : JSON.stringify(body)});
    if (!response.ok) { const error = await response.json().catch(() => ({})); throw Error(error.error || `요청 실패 (${response.status})`); }
    return response.headers.get("content-type")?.includes("octet-stream") ? response.arrayBuffer() : response.json();
}
function action(id, handler, mutation = false) {
    $(id).addEventListener("click", async () => {
        if (mutation && mutationBusy) return;
        if (mutation) mutationBusy = true;
        $(id).disabled = true;
        try { await handler(); } catch (e) { status(e.message); }
        finally { $(id).disabled = false; if (mutation) mutationBusy = false; }
    });
}
function numeric(parent, label, object, key, min, max, step, help) {
    const row = document.createElement("label"); row.append(document.createTextNode(label));
    const input = document.createElement("input"); input.type = "number";
    input.min = min; input.max = max; input.step = step; input.value = object[key]; input.title = help || label;
    input.addEventListener("change", () => {
        if (!input.checkValidity() || !Number.isFinite(input.valueAsNumber)) { input.reportValidity(); return; }
        object[key] = input.valueAsNumber; changed();
    }); row.append(input); parent.append(row); return input;
}
function checkbox(parent, label, object, key) {
    const row = document.createElement("label"), input = document.createElement("input"); input.type = "checkbox"; input.checked = object[key];
    input.onchange = () => {object[key] = input.checked; changed();}; row.append(document.createTextNode(label), input); parent.append(row);
}
function section(title) {
    const section = document.createElement("details"), heading = document.createElement("summary"); heading.textContent = title;
    section.append(heading); $("controls").append(section); return section;
}
function noise(parent, n, allowWarp = true) {
    numeric(parent, "격자 간격 지수", n, "spacing_log2", 2, 17, 1, "간격은 2의 지수승 블록입니다. 다음 옥타브마다 절반이 됩니다.");
    const octaves = numeric(parent, "옥타브 수", n, "octaves", 1, 16, 1, "작은 무늬를 더하는 겹의 수입니다. 많을수록 계산량이 늘어납니다.");
    octaves.addEventListener("change", () => {
        if (!octaves.checkValidity()) return;
        if (n.weights) n.weights = Array.from({length:n.octaves}, (_, i) => n.weights[i] ?? 0);
        changed(); render();
    });
    numeric(parent, "공통 가중치 감쇄", n, "gain", 0, 1, .01, "개별 가중치를 쓰지 않을 때 다음 겹의 입력 가중치 비율입니다.");
    numeric(parent, "주파수 배율", n, "frequency_multiplier", .001, 1500, "any", "높을수록 무늬가 촘촘해집니다. 순환 주기에 맞춰 실제 간격은 조정됩니다.");
    const row = document.createElement("label"), explicit = document.createElement("input"); explicit.type = "checkbox"; explicit.checked = !!n.weights;
    row.append(document.createTextNode("옥타브별 가중치"), explicit); parent.append(row);
    explicit.onchange = () => { if (explicit.checked) n.weights = Array.from({length:n.octaves}, (_, i) => n.gain ** i); else delete n.weights; changed(); render(); };
    if (n.weights) for (let i=0; i<n.octaves; i++) numeric(parent, `옥타브 ${i+1} 가중치`, n.weights, i, 0, 1000000, "any", "0이면 해당 겹을 제외합니다. Double Perlin의 옥타브 감쇄가 추가로 적용됩니다.");
    if (allowWarp) {
        const row = document.createElement("label"), select = document.createElement("select");
        for (const [id,name] of [["","없음"],["shift","공유 Shift"]]) { const option = document.createElement("option"); option.value=id; option.textContent=name; select.append(option); }
        select.value=n.warp; select.onchange=()=>{n.warp=select.value; changed();}; row.append(document.createTextNode("적용 워핑"),select);parent.append(row);
    }
}
function render() {
    const open = [...$("controls").children].map(e=>e.open);
    $("controls").replaceChildren(); $("seed").value=config.seed;
    const temp=section("온도 · 위도 띠");noise(temp,config.temperature);
    const b=config.temperature_bands;
    numeric(temp,"적도 온도",b,"equator",-1,1,.01);numeric(temp,"양 끝 온도",b,"poles",-1,1,.01);
    numeric(temp,"따뜻한 띠 집중도",b,"latitude_power",1,8,.01);numeric(temp,"지역 온도 변화",b,"variation",0,1,.01);
    const rain=section("강수량 · X/Z 순환");noise(rain,config.precipitation);
    const warp=section("기후 공유 Shift"), w=config.warps[0];checkbox(warp,"워핑 사용",w,"enabled");numeric(warp,"변위 강도 (블록)",w,"strength",0,8192,"any");noise(warp,w.noise,false);
    [...$("controls").children].forEach((e,i)=>e.open=open[i]??(i===0));
    $("json").value=JSON.stringify(config,null,2);stateLabel();
}
function hasUnsaved(){ return config && text()!==publishedText && text()!==draftText; }
async function loadPublished(initial=false) {
    const [result,workspace]=await Promise.all([api("state"),api("workspace")]);
    config=result.config;revision=result.revision;publishedText=text();workspaceState=workspace;
    draftText=JSON.stringify(workspace.config);
    if(initial)config=workspace.config;
    if(!masterSeed) {
        masterSeed=createMasterSeed(token,config.seed,seed=>{config.seed=seed;changed();render();});
        config.seed=masterSeed.value;
    } else masterSeed.set(config.seed);
    generation++;render();
    $("load-draft").disabled=false;
}
function download(blob,name){const url=URL.createObjectURL(blob), a=document.createElement("a");a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function range(){return Object.fromEntries(["x0","z0","x1","z1"].map(id=>[id,$(id).valueAsNumber]));}
function setRange(r){for(const [id,v] of Object.entries(r))$(id).value=v;invalidateMap();}
function invalidateMap(){generation++;$("map-status").textContent="표시 조건이 바뀌었습니다. 미리보기를 다시 생성하세요.";}
async function preview(){
    const id=++generation, kind=$("kind").value, r=range(), before=$("before").checked;
    $("map-status").textContent="기후 지도를 계산하는 중…";
    const start=performance.now();
    const buffer=await api("preview",{config:clone(config),kind,...r,resolution:Number($("resolution").value),before_warp:before});
    if(id!==generation){$("map-status").textContent="계산 중 편집값이 바뀌었습니다. 다시 생성하세요.";return;}
    const data=new Float32Array(buffer),width=data[0],height=data[1], canvas=$("map");
    if(data.length!==2+width*height)throw Error("잘못된 미리보기 응답");
    canvas.width=width;canvas.height=height;
    const ctx=canvas.getContext("2d"),image=ctx.createImageData(width,height);
    const a=kind==="temperature"?[30,80,230]:[165,105,45],b=kind==="temperature"?[240,55,25]:[35,120,235];
    let lo=Infinity,hi=-Infinity;
    for(let i=0;i<width*height;i++){const v=data[i+2],t=Math.max(0,Math.min(1,v*.5+.5));lo=Math.min(lo,v);hi=Math.max(hi,v);for(let c=0;c<3;c++)image.data[i*4+c]=Math.round(a[c]+(b[c]-a[c])*t);image.data[i*4+3]=255;}
    ctx.putImageData(image,0,0);shown={data,width,height,kind,r};
    $("legend").textContent=kind==="temperature"?"온도: 파랑 −1 (추움) → 빨강 +1 (더움)":"강수량: 갈색 −1 (건조) → 파랑 +1 (습윤)";
    $("map-status").textContent=`${width} × ${height} 표본 · ${(performance.now()-start).toFixed(0)} ms · 최솟값 ${lo.toFixed(4)} / 최댓값 ${hi.toFixed(4)}${before?" · 워핑 전":""}`;
}
$("seed").onchange=()=>{if($("seed").checkValidity()&&Number.isFinite($("seed").valueAsNumber)){config.seed=$("seed").valueAsNumber;changed();}};
action("random-seed",()=>{config.seed=crypto.getRandomValues(new Uint32Array(1))[0];changed();render();});
action("load",async()=>{if(hasUnsaved()&&!confirm("저장하지 않은 편집을 버리고 확정 설정을 불러올까요?"))return;await loadPublished();invalidateMap();},true);
action("load-draft",async()=>{
    if(hasUnsaved()&&!confirm("저장하지 않은 편집을 버리고 마지막 작업 저장을 불러올까요?"))return;
    workspaceState=await api("workspace");config=workspaceState.config;draftText=text();
    masterSeed?.set(config.seed);generation++;render();invalidateMap();
},true);
action("save",async()=>{
    const snapshot=clone(config);
    workspaceState=await api("workspace/climate",{config:snapshot,revision:workspaceState.climate_revision,base_seed:workspaceState.base_seed});
    draftText=JSON.stringify(snapshot);stateLabel();
},true);
action("publish",async()=>{const snapshot=clone(config);const result=await api("publish",{config:snapshot,revision});revision=result.revision;publishedText=JSON.stringify(snapshot);stateLabel();},true);
action("apply-json",async()=>{const result=await api("validate",{config:JSON.parse($("json").value)});config=result.config;changed();render();},true);
action("export",()=>download(new Blob([JSON.stringify(config,null,2)+"\n"],{type:"application/json"}),"worldgen.json"));
action("refresh",preview);
action("whole",()=>setRange({x0:0,z0:0,x1:131072,z1:131072}));
action("random-area",()=>{const r=range(),w=r.x1-r.x0,h=r.z1-r.z0;if(!(w>0&&h>0&&w<=131072&&h<=131072))throw Error("범위를 확인해 주세요.");const x=Math.floor(Math.random()*(131072-w+1)),z=Math.floor(Math.random()*(131072-h+1));setRange({x0:x,z0:z,x1:x+w,z1:z+h});});
action("png",()=>{if(!shown)throw Error("미리보기를 먼저 생성하세요.");$("map").toBlob(blob=>{if(blob)download(blob,`${shown.kind}.png`);});});
action("quit",async()=>{if(hasUnsaved()&&!confirm("저장하지 않은 편집이 있습니다. 종료할까요?"))return;await api("shutdown",{});status("편집기가 종료되었습니다. 이 탭을 닫아도 됩니다.");},true);
for(const id of ["kind","resolution","before","x0","z0","x1","z1"])$(id).addEventListener("change",invalidateMap);
$("map").addEventListener("mousemove",event=>{if(!shown)return;const rect=$("map").getBoundingClientRect(),s=shown,x=Math.max(0,Math.min(s.width-1,Math.floor((event.clientX-rect.left)/rect.width*s.width))),z=Math.max(0,Math.min(s.height-1,Math.floor((event.clientY-rect.top)/rect.height*s.height)));$("sample").textContent=`X ${(s.r.x0+(s.r.x1-s.r.x0)*x/(s.width-1)).toFixed(1)} · Z ${(s.r.z0+(s.r.z1-s.r.z0)*z/(s.height-1)).toFixed(1)} · 값 ${s.data[2+z*s.width+x].toFixed(5)}`;});
window.addEventListener("beforeunload",e=>{if(hasUnsaved()){e.preventDefault();e.returnValue="";}});
loadPublished(true).catch(e=>status(e.message));

action("voronoi", () => window.open("voronoi.html#" + encodeURIComponent(token), "_blank", "noopener"));
