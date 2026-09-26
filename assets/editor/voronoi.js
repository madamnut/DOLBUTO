"use strict";
const $ = id => document.getElementById(id);
const WORLD = 131072;
const token = decodeURIComponent(location.hash.slice(1));
const keys = ["seed", "spacing", "jitter", "warp_enabled", "warp_strength", "warp_spacing_log2", "warp_octaves", "warp_gain", "land_spacing", "land_threshold", "arch_enabled", "arch_spacing", "arch_threshold", "subdivisions", "island_spacing", "island_threshold", "arch_edge_fade"];
const landDefaults = {land_spacing: 32768, land_threshold: 0};
const archDefaults = {arch_enabled:true,arch_spacing:16384,arch_threshold:.15,subdivisions:4,island_spacing:2048,island_threshold:0,arch_edge_fade:.18};
const booleanKeys = new Set(["warp_enabled","arch_enabled"]);
let masterSeed;
let storageState, savedText, storageReady = false, storageBusy = false;
const rangeKeys = ["x0", "z0", "x1", "z1"];
let revision = 0, busy = false, pending = false, timer, shown, selected = -1, drag;
function message(text) { $("status").textContent = text; }
function readParameters() {
    return Object.fromEntries(keys.map(key => [key, booleanKeys.has(key) ? $(key).checked : $(key).valueAsNumber]));
}
function readRange() { return Object.fromEntries(rangeKeys.map(key => [key, $(key).valueAsNumber])); }
function settings() {
    return {format:"dolbuto-voronoi-experiment",version:3,parameters:readParameters(),range:readRange(),resolution:Number($("resolution").value),
        display:{view:$("view").value,edges:$("edges").checked,sites:$("sites").checked,auto:$("auto").checked}};
}
function unsaved() { return storageReady && JSON.stringify(settings()) !== savedText; }
function saveLabel() {
    if(!storageReady)return;
    const label=unsaved()?"저장하지 않은 변경이 있습니다.":storageState.voronoi?"저장된 설정과 같습니다.":"아직 저장한 보로노이 설정이 없습니다.";
    $("save-state").textContent=`${label} · ${storageState.path}`;
}
async function workspaceApi(body) {
    const response=await fetch(body?"/api/workspace/voronoi":"/api/workspace",{
        method:body?"POST":"GET",headers:{"X-Editor-Token":token,"Content-Type":"application/json"},
        body:body?JSON.stringify(body):undefined});
    const result=await response.json();
    if(!response.ok)throw Error(result.error||`저장 요청 실패 (${response.status})`);
    return result;
}
function applySettings(data) {
    if(data.format!=="dolbuto-voronoi-experiment"||![1,2,3].includes(data.version))throw Error("보로노이 실험 JSON이 아닙니다. 게임 설정 파일과는 별개입니다.");
    if(data.version===1)data.parameters={...data.parameters,...landDefaults};
    if(data.version<3)data.parameters={...data.parameters,...archDefaults};
    for(const key of keys){const v=data.parameters?.[key];if(booleanKeys.has(key)?typeof v!=="boolean":typeof v!=="number"||!Number.isFinite(v))throw Error("실험 파라미터를 확인해 주세요.");}
    for(const key of rangeKeys)if(typeof data.range?.[key]!=="number"||!Number.isFinite(data.range[key]))throw Error("표시 범위를 확인해 주세요.");
    if(![256,512,1024].includes(data.resolution))throw Error("해상도는 256/512/1024 중 하나여야 합니다.");
    const display=data.display??{view:"land",edges:true,sites:true,auto:true};
    if(![...$("view").options].some(o=>o.value===display.view)||["edges","sites","auto"].some(k=>typeof display[k]!=="boolean"))throw Error("지도 표시 설정을 확인해 주세요.");
    const previous=settings();
    const assign=s=>{
        for(const key of keys)if(booleanKeys.has(key))$(key).checked=s.parameters[key];else $(key).value=s.parameters[key];
        for(const key of rangeKeys)$(key).value=s.range[key];$("resolution").value=s.resolution;
        const d=s.display??display;$("view").value=d.view;for(const key of ["edges","sites","auto"])$(key).checked=d[key];
    };
    assign(data);
    if(!valid()){assign(previous);throw Error("실험 설정이 허용 범위를 벗어났습니다.");}
}
async function storageAction(action) {
    if(!storageReady||storageBusy)return;
    storageBusy=true;$("save-settings").disabled=$("load-settings").disabled=true;
    try{await action();}catch(error){message(error.message);}
    finally{storageBusy=false;$("save-settings").disabled=$("load-settings").disabled=false;saveLabel();}
}
function valid() {
    if (!$("parameters").reportValidity() || !$("range").reportValidity()) return false;
    const r = readRange(), dx = r.x1-r.x0, dz = r.z1-r.z0;
    if (!(dx >= 1 && dz >= 1 && dx <= WORLD && dz <= WORLD)) {
        message("표시 범위의 가로·세로 길이는 1~131072블록이어야 합니다."); return false;
    }
    return true;
}
function updateHints() {
    const n = Math.max(4, Math.min(256, Math.round(WORLD / $("spacing").valueAsNumber)));
    $("spacing-info").textContent = Number.isFinite(n)
        ? `순환에 맞춘 실제 간격 ${(WORLD/n).toFixed(2)}블록 · ${n} × ${n} = ${(n*n).toLocaleString()}개 셀`
        : "셀 크기를 입력해 주세요.";
    const landSpacing = $("land_spacing").valueAsNumber;
    const actual = ratio => WORLD / Math.max(1, Math.round(WORLD / landSpacing * ratio));
    $("land-spacing-info").textContent = Number.isFinite(landSpacing) && landSpacing > 0
        ? `순환에 맞춘 첫 옥타브 간격: ${actual(1).toFixed(2)} / ${actual(1.0181268882175227).toFixed(2)}블록`
        : "대륙 크기를 입력해 주세요.";
    $("fine-spacing-info").textContent = `군도 내부의 작은 셀 간격: ${(WORLD/n/$("subdivisions").valueAsNumber).toFixed(2)}블록. 작은 셀은 이웃 군도와 이어집니다.`;
    // Site locations are defined before the coordinate warp, not in the warped display plane.
    $("site-note").textContent = "생성점은 워핑 전 지도에서만 표시합니다. 워핑 후의 거리는 변형된 좌표에서 측정합니다.";
}
function invalidate() {
    masterSeed?.set($("seed").valueAsNumber);
    revision++; updateHints(); saveLabel();
    $("map-status").textContent = "편집값이 바뀌었습니다. 새 미리보기를 기다리는 중입니다.";
    clearTimeout(timer);
    if ($("auto").checked) timer = setTimeout(generate, 300);
}
async function generate() {
    clearTimeout(timer);
    if (!valid()) return;
    pending = true;
    if (busy) return;
    busy = true;
    $("generate").textContent = "계산 중 · 다시 누르면 최신 요청 예약";
    try {
        while (pending) {
            pending = false;
            const requestRevision = revision;
            const params = readParameters(), r = readRange(), resolution = Number($("resolution").value);
            $("map-status").textContent = "보로노이 지도를 계산하는 중…";
            const response = await fetch("/api/voronoi/preview", {
                method: "POST", headers: {"X-Editor-Token": token, "Content-Type": "application/json"},
                body: JSON.stringify({parameters: params, ...r, resolution})
            });
            if (!response.ok) {
                const error = await response.json().catch(() => ({}));
                throw Error(error.error || `미리보기 요청 실패 (${response.status})`);
            }
            const data = new Float32Array(await response.arrayBuffer());
            if (requestRevision !== revision) continue;
            const [version,width,height,cells,spacing,siteCount,fineCells,fineSpacing,leafCount,milliseconds,
                coarseStride,leafStride,pixelStride,coarseOffset,leafOffset,offset] = data;
            if(version!==3 || coarseStride!==6 || leafStride!==6 || pixelStride!==4 ||
                data.length!==offset+width*height*4) throw Error("새 편집기를 실행하고 이 탭을 새로고침해 주세요.");
            if(!shown || shown.params.seed!==params.seed || shown.cells!==cells || shown.fineCells!==fineCells) selected=-1;
            shown={data,width,height,cells,spacing,siteCount,fineCells,fineSpacing,leafCount,milliseconds,
                coarseOffset,leafOffset,offset,params,r,revision:requestRevision};
            draw();
            message("편집기 전용 결과입니다. 게임 설정이나 지형은 변경하지 않았습니다.");
        }
    } catch (error) {
        message(error.message); $("map-status").textContent = "계산하지 못했습니다. 입력값을 확인해 주세요.";
        pending = false;
    } finally {
        busy = false; $("generate").textContent = "미리보기 생성";
        if (shown && shown.revision !== revision) $("map-status").textContent = "이전 설정의 이미지입니다. 미리보기를 다시 생성하세요.";
    }
}
function color(id) {
    let v = (id ^ 0xa511e9b3) >>> 0;
    v = Math.imul(v ^ (v >>> 16), 0x7feb352d);
    v = Math.imul(v ^ (v >>> 15), 0x846ca68b);
    v = (v ^ (v >>> 16)) >>> 0;
    return [75+(v&127), 75+((v>>>8)&127), 75+((v>>>16)&127)];
}
function draw() {
    if(!shown)return;
    const s=shown,canvas=$("map"),data=s.data;
    canvas.width=s.width;canvas.height=s.height;
    const ctx=canvas.getContext("2d"),image=ctx.createImageData(s.width,s.height);
    const view=$("view").value,edges=$("edges").checked;
    const coarseView=["regions","land-noise","arch-noise"].includes(view);
    const parent=id=>s.coarseOffset+6*id,leaf=id=>s.leafOffset+6*id;
    const cellKey=at=>data[at+1]>=0?s.siteCount+data[leaf(data[at+1])]:data[at];
    const edgeKey=at=>coarseView?data[at]:cellKey(at);
    const palette=Array.from({length:s.siteCount},(_,i)=>color(i));
    const leafPalette=Array.from({length:s.leafCount},(_,i)=>color(s.siteCount+data[leaf(i)]));
    const regionColors=[[34,72,120],[112,155,79],[152,111,186]];
    const materialColors=[[34,72,120],[112,155,79],[171,187,91],[40,112,151]];
    const regions=[0,0,0],kinds=[0,0,0,0];
    for(let id=0;id<s.siteCount;id++)regions[data[parent(id)+3]]++;
    for(let z=0;z<s.height;z++)for(let x=0;x<s.width;x++){
        const px=z*s.width+x,at=s.offset+4*px,pid=data[at],lid=data[at+1],kind=data[at+3],key=cellKey(at);
        const pa=parent(pid),la=lid>=0?leaf(lid):-1;
        kinds[kind]++;
        const edge=(x>0&&edgeKey(at-4)!==edgeKey(at))||(z>0&&edgeKey(at-4*s.width)!==edgeKey(at));
        let rgb=lid>=0?leafPalette[lid]:palette[pid];
        if(view==="land")rgb=materialColors[kind];
        if(view==="regions")rgb=regionColors[data[pa+3]];
        if(["land-noise","arch-noise","island-noise"].includes(view)){
            const value=view==="land-noise"?data[pa+2]:view==="arch-noise"?data[pa+4]:(la>=0?data[la+3]:-1);
            const v=Math.round(Math.max(0,Math.min(1,value*.5+.5))*255);rgb=[v,v,v];
        }
        if(view==="distance"){const v=Math.round(Math.min(1,data[at+2]/Math.SQRT2)*255);rgb=[v,v,v];}
        if(view==="edges")rgb=edge?[214,228,222]:[22,29,34];
        else if(edges&&edge)rgb=[20,28,33];
        if(selected>=0&&key!==selected)rgb=rgb.map(v=>Math.round(v*.35));
        if(selected===key&&edge)rgb=[255,238,160];
        image.data.set([...rgb,255],px*4);
    }
    ctx.putImageData(image,0,0);
    if($("sites").checked&&(!s.params.warp_enabled||s.params.warp_strength===0)){
        ctx.fillStyle="#fff";ctx.strokeStyle="#152027";ctx.lineWidth=1;
        const dot=(sx,sz)=>{
            for(let oz=-1;oz<=1;oz++)for(let ox=-1;ox<=1;ox++){
                const a=sx+ox*WORLD,b=sz+oz*WORLD;
                if(a<s.r.x0||a>s.r.x1||b<s.r.z0||b>s.r.z1)continue;
                ctx.beginPath();ctx.arc((a-s.r.x0)/(s.r.x1-s.r.x0)*(s.width-1),(b-s.r.z0)/(s.r.z1-s.r.z0)*(s.height-1),2,0,Math.PI*2);ctx.fill();ctx.stroke();
            }
        };
        for(let id=0;id<s.siteCount;id++){const a=parent(id);if(coarseView||data[a+3]!==2)dot(data[a],data[a+1]);}
        if(!coarseView)for(let i=0;i<s.leafCount;i++){const a=leaf(i);if(data[a+4]>=0)dot(data[a+1],data[a+2]);}
    }
    $("map-status").textContent=`${s.width} × ${s.height} 표본 · 큰 셀 ${s.siteCount.toLocaleString()}개 · 작은 셀 간격 ${s.fineSpacing.toFixed(1)}블록 · 계산 ${s.milliseconds.toFixed(1)} ms${s.revision!==revision?" · 이전 설정 이미지":""}`;
    const total=s.width*s.height;
    $("land-stats").textContent=`큰 셀: 대륙 ${regions[1]} / 군도 ${regions[2]} / 외해 ${regions[0]} · 현재 범위 표본 면적: 총 육지 ${(100*(kinds[1]+kinds[2])/total).toFixed(1)}% (섬 ${(100*kinds[2]/total).toFixed(1)}%)`;
    const legends={land:"초록: 대륙 · 연두: 섬 · 밝은 파랑: 군도 바다 · 짙은 파랑: 외해. 높이·해안 형태는 아직 지정하지 않습니다.",
        regions:"큰 셀만 표시: 초록 대륙 · 보라 군도 구역 · 파랑 외해. 군도 구역 안에서만 작은 셀을 사용합니다.",
        "land-noise":"큰 셀 생성점의 대륙 분포: 검정 −1 → 흰색 +1. 기준보다 높으면 대륙입니다.",
        "arch-noise":"큰 셀 생성점의 군도 구역 분포: 검정 −1 → 흰색 +1. 대륙이 아닌 곳에서만 군도 판정을 적용합니다.",
        "island-noise":"작은 셀 생성점의 섬 분포: 검정 −1 → 흰색 +1. 가장자리에서는 섬 판정 기준이 높아집니다.",
        distance:"현재 단계의 생성점까지 거리: 검정 가까움 → 흰색 멀어짐. 군도 내부는 작은 셀 간격을 기준으로 표시합니다."};
    $("legend").textContent=legends[view]||"군도 안에서는 작은 셀, 대륙과 외해에서는 큰 셀 경계를 표시합니다. 색은 셀 ID 구분용입니다.";
}
const wrap = x => ((x%WORLD)+WORLD)%WORLD;
function setView(cx,cz,width,height) {
    width=Math.max(1,Math.min(WORLD,width));height=Math.max(1,Math.min(WORLD,height));
    cx=wrap(cx);cz=wrap(cz);
    const values={x0:cx-width/2,z0:cz-height/2,x1:cx+width/2,z1:cz+height/2};
    for(const [key,value] of Object.entries(values)) $(key).value=Number(value.toFixed(4));
    invalidate();
}
function zoom(factor,anchorX=.5,anchorZ=.5) {
    const r=readRange(),width=r.x1-r.x0,height=r.z1-r.z0;
    if(!Number.isFinite(width+height)||width<=0||height<=0) return;
    const nextW=Math.max(1,Math.min(WORLD,width*factor)), nextH=Math.max(1,Math.min(WORLD,height*factor));
    setView(r.x0+width*anchorX+nextW*(.5-anchorX),r.z0+height*anchorZ+nextH*(.5-anchorZ),nextW,nextH);
}
function download(blob,name) {
    const url=URL.createObjectURL(blob),a=document.createElement("a");a.href=url;a.download=name;a.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
}
$("parameters").onsubmit=$("range").onsubmit=event=>{event.preventDefault();generate();};
for(const id of [...keys,...rangeKeys,"resolution"]) $(id).addEventListener("change",invalidate);
for(const id of [...keys,...rangeKeys]) $(id).addEventListener("input",saveLabel);
for(const id of ["view","edges","sites"]) $(id).addEventListener("change",()=>{draw();saveLabel();});
$("auto").onchange=()=>{saveLabel();if($("auto").checked)generate();else clearTimeout(timer);};
$("generate").onclick=generate;
$("random-seed").onclick=()=>{$("seed").value=crypto.getRandomValues(new Uint32Array(1))[0];invalidate();};
$("whole").onclick=()=>setView(WORLD/2,WORLD/2,WORLD,WORLD);
$("seam-x").onclick=()=>setView(0,WORLD/2,16384,16384);
$("seam-z").onclick=()=>setView(WORLD/2,0,16384,16384);
$("zoom-in").onclick=()=>zoom(.5);$("zoom-out").onclick=()=>zoom(2);
$("clear-selection").onclick=()=>{selected=-1;draw();};
$("climate").onclick=()=>{location.href="/#"+encodeURIComponent(token);};
$("png").onclick=()=>{
    if(!shown){message("미리보기를 먼저 생성하세요.");return;}
    $("map").toBlob(blob=>{if(blob)download(blob,`voronoi-${shown.params.seed}.png`);});
};
$("export-settings").onclick=()=>{
    if(!valid())return;
    const data=settings();
    download(new Blob([JSON.stringify(data,null,2)+"\n"],{type:"application/json"}),"voronoi-experiment.json");
};
$("import-settings").onchange=async event=>{
    const input=event.target,file=input.files[0];if(!file)return;
    try{
        if(file.size>65536)throw Error("실험 JSON은 64 KiB 이하여야 합니다.");
        const data=JSON.parse(await file.text());
        applySettings(data);
        invalidate();message(data.version<3?"이전 실험을 마스터 시드 하나로 전환했습니다. 별도 육지 시드는 사용하지 않습니다.":"실험 설정을 불러왔습니다. 마스터 시드는 기후 편집기와 공유합니다.");
    }catch(error){
        message(error.message);updateHints();
    }finally{input.value="";}
};
$("save-settings").onclick=()=>storageAction(async()=>{
    if(!valid())return;
    const snapshot=settings();
    storageState=await workspaceApi({voronoi:snapshot,revision:storageState.voronoi_revision,base_seed:storageState.base_seed});
    savedText=JSON.stringify(snapshot);
    message("보로노이 설정과 마스터 시드를 저장했습니다. 다음 실행 때 자동으로 불러옵니다.");
});
$("load-settings").onclick=()=>storageAction(async()=>{
    if(unsaved()&&!confirm("저장하지 않은 변경을 버리고 마지막 저장을 불러올까요?"))return;
    const latest=await workspaceApi();
    if(!latest.voronoi)throw Error("아직 저장한 보로노이 설정이 없습니다.");
    applySettings(latest.voronoi);storageState=latest;savedText=JSON.stringify(settings());
    selected=-1;invalidate();generate();message("마지막 저장을 불러왔습니다.");
});
window.addEventListener("beforeunload",event=>{if(unsaved()){event.preventDefault();event.returnValue="";}});
const canvas=$("map");
canvas.addEventListener("wheel",event=>{
    event.preventDefault();if(!shown)return;
    const rect=canvas.getBoundingClientRect();
    zoom(event.deltaY<0?.8:1.25,(event.clientX-rect.left)/rect.width,(event.clientY-rect.top)/rect.height);
},{passive:false});
canvas.addEventListener("pointerdown",event=>{
    if(event.button!==0||!shown)return;
    drag={x:event.clientX,z:event.clientY,r:readRange(),moved:false};canvas.setPointerCapture(event.pointerId);canvas.classList.add("dragging");
});
canvas.addEventListener("pointermove",event=>{
    if(!shown)return;
    const rect=canvas.getBoundingClientRect(),s=shown;
    if(drag){if(Math.hypot(event.clientX-drag.x,event.clientY-drag.z)>4)drag.moved=true;return;}
    const x=Math.max(0,Math.min(s.width-1,Math.floor((event.clientX-rect.left)/rect.width*s.width)));
    const z=Math.max(0,Math.min(s.height-1,Math.floor((event.clientY-rect.top)/rect.height*s.height)));
    const at=s.offset+4*(z*s.width+x),pid=s.data[at],lid=s.data[at+1],kind=s.data[at+3];
    const pa=s.coarseOffset+6*pid,la=lid>=0?s.leafOffset+6*lid:-1;
    const label=["외해","대륙","섬","군도 바다"][kind];
    const detail=la>=0?` · 작은 셀 #${s.data[la]} · 섬 분포 ${s.data[la+3].toFixed(4)} · 가장자리 유지량 ${Math.max(0,s.data[la+4]).toFixed(3)}`:"";
    $("sample").textContent=`X ${wrap(s.r.x0+(s.r.x1-s.r.x0)*x/(s.width-1)).toFixed(1)} · Z ${wrap(s.r.z0+(s.r.z1-s.r.z0)*z/(s.height-1)).toFixed(1)} · ${label} · 큰 셀 #${pid} · 대륙 분포 ${s.data[pa+2].toFixed(4)} · 군도 분포 ${s.data[pa+4].toFixed(4)}${detail}`;

});
canvas.addEventListener("pointerup",event=>{
    if(!drag)return;
    const d=drag;drag=undefined;canvas.classList.remove("dragging");canvas.releasePointerCapture(event.pointerId);
    const rect=canvas.getBoundingClientRect();
    if(d.moved){const w=d.r.x1-d.r.x0,h=d.r.z1-d.r.z0;setView((d.r.x0+d.r.x1)/2-(event.clientX-d.x)/rect.width*w,(d.r.z0+d.r.z1)/2-(event.clientY-d.z)/rect.height*h);}
    else if(shown){const x=Math.max(0,Math.min(shown.width-1,Math.floor((event.clientX-rect.left)/rect.width*shown.width))),z=Math.max(0,Math.min(shown.height-1,Math.floor((event.clientY-rect.top)/rect.height*shown.height)));const at=shown.offset+4*(z*shown.width+x),lid=shown.data[at+1];selected=lid>=0?shown.siteCount+shown.data[shown.leafOffset+6*lid]:shown.data[at];draw();}
});
canvas.addEventListener("pointercancel",()=>{drag=undefined;canvas.classList.remove("dragging");});
async function start() {
    try {
        storageState=await workspaceApi();
        if(storageState.voronoi)applySettings(storageState.voronoi);
        $("seed").value=storageState.base_seed;savedText=JSON.stringify(settings());
        masterSeed=createMasterSeed(token,storageState.base_seed,seed=>{$("seed").value=seed;invalidate();});
        $("seed").value=masterSeed.value;storageReady=true;
        $("save-settings").disabled=$("load-settings").disabled=false;
        updateHints();saveLabel();generate();
    }catch(error){message(error.message);$("save-state").textContent="저장된 작업을 읽지 못했습니다. 파일은 변경하지 않았습니다.";}
}
start();
