"use strict";
const $ = id => document.getElementById(id);
const WORLD = 131072;
const token = decodeURIComponent(location.hash.slice(1));
const keys = ["seed", "spacing", "jitter", "warp_enabled", "warp_strength", "warp_spacing_log2", "warp_octaves", "warp_gain", "growth_seed_count", "growth_percent", "growth_steps", "growth_noise_enabled", "growth_noise_strength", "growth_large_log2", "growth_small_log2", "growth_detail_mix", "trend_distance", "trend_distance_strength", "trend_shared_strength", "trend_local_strength", "trend_spacing_log2", "trend_neighbor_mix", "trend_neighbor_passes", "trend_compression", "trend_compression_variation", "trend_transition"];
const growthDefaults = {growth_seed_count:32,growth_percent:50,growth_steps:12};
const noiseDefaults = {growth_noise_enabled:true,growth_noise_strength:8,growth_large_log2:14,growth_small_log2:12,growth_detail_mix:.25};
const trendDefaults={"trend_distance": 8192, "trend_distance_strength": 0.45, "trend_shared_strength": 0.65, "trend_local_strength": 0.15, "trend_spacing_log2": 14, "trend_neighbor_mix": 0.55, "trend_neighbor_passes": 3, "trend_compression": 0.45, "trend_compression_variation": 0.35, "trend_transition": 0.65};
const booleanKeys = new Set(["warp_enabled","growth_noise_enabled"]);
let masterSeed;
let storageState, savedText, storageReady = false, storageBusy = false;
const rangeKeys = ["x0", "z0", "x1", "z1"];

let revision = 0, busy = false, pending = false, timer, shown, selected = -1;
function message(text) { $("status").textContent = text; }
function readParameters() {
    return Object.fromEntries(keys.map(key => [key, booleanKeys.has(key) ? $(key).checked : $(key).valueAsNumber]));
}
function readRange() { return Object.fromEntries(rangeKeys.map(key => [key, $(key).valueAsNumber])); }
function settings() {
    return {format:"dolbuto-voronoi-experiment",version:6,parameters:readParameters(),range:readRange(),resolution:Number($("resolution").value),
        display:{view:$("view").value,distance_scale:$("distance-scale").valueAsNumber,edges:$("edges").checked,sites:$("sites").checked,auto:$("auto").checked}};
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
    if(data.format!=="dolbuto-voronoi-experiment"||![1,2,3,4,5,6].includes(data.version))throw Error("보로노이 실험 JSON이 아닙니다. 게임 설정 파일과는 별개입니다.");
    data=structuredClone(data);
    if(data.version<5){data.parameters={...data.parameters,...growthDefaults};if(data.display)data.display.view="land";}
    for(const [key,value] of [...Object.entries(noiseDefaults),...Object.entries(trendDefaults)])if(data.parameters && !Object.hasOwn(data.parameters,key))data.parameters[key]=value;
    for(const key of keys){const v=data.parameters?.[key];if(booleanKeys.has(key)?typeof v!=="boolean":typeof v!=="number"||!Number.isFinite(v))throw Error("실험 파라미터를 확인해 주세요.");}
    for(const key of rangeKeys)if(typeof data.range?.[key]!=="number"||!Number.isFinite(data.range[key]))throw Error("표시 범위를 확인해 주세요.");
    if(![256,512,1024].includes(data.resolution))throw Error("해상도는 256/512/1024 중 하나여야 합니다.");
    if(data.display?.view==="height")data.display.view="trend-altitude";
    const display=data.display??{view:"land",edges:true,sites:true,auto:true};
    if(![...$("view").options].some(o=>o.value===display.view)||["edges","sites","auto"].some(k=>typeof display[k]!=="boolean"))throw Error("지도 표시 설정을 확인해 주세요.");
    const scale=display.distance_scale??8192;
    if(typeof scale!=="number"||!Number.isFinite(scale)||scale<1||scale>WORLD)throw Error("거리 지도 기준은 1~131072블록이어야 합니다.");
    const previous=settings();
    const assign=s=>{
        for(const key of keys)if(booleanKeys.has(key))$(key).checked=s.parameters[key];else $(key).value=s.parameters[key];
        for(const key of rangeKeys)$(key).value=s.range[key];$("resolution").value=s.resolution;
        const d=s.display??display;$("view").value=d.view;$("distance-scale").value=d.distance_scale??8192;for(const key of ["edges","sites","auto"])$(key).checked=d[key];
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
    if (!$("parameters").reportValidity() || !$("range").reportValidity() || !$("distance-scale").reportValidity()) return false;
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
    // Site locations are defined before the coordinate warp, not in the warped display plane.
    $("site-note").textContent = "생성점은 워핑 전 지도에서만 표시합니다. 워핑 후의 거리는 변형된 좌표에서 측정합니다.";
}
function invalidate(interactive=false) {
    masterSeed?.set($("seed").valueAsNumber);
    revision++; updateHints(); saveLabel();
    if(!interactive)mapNavigation.sync();
    else pending=false;
    $("map-status").textContent = "편집값이 바뀌었습니다. 새 미리보기를 기다리는 중입니다.";
    clearTimeout(timer);
    if (!interactive && $("auto").checked) timer = setTimeout(generate, 300);
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
            const params = readParameters(), r = readRange(), resolution = Number($("resolution").value), interpolate=$("view").value.endsWith("-smooth");
            $("map-status").textContent = "보로노이 지도를 계산하는 중…";
            const response = await fetch("/api/voronoi/preview", {
                method: "POST", headers: {"X-Editor-Token": token, "Content-Type": "application/json"},
                body: JSON.stringify({parameters: params, ...r, resolution, coast_distance:$("view").value.startsWith("coast-"),trend_map:$("view").value.startsWith("trend-"),interpolate})
            });
            if (!response.ok) {
                const error = await response.json().catch(() => ({}));
                throw Error(error.error || `미리보기 요청 실패 (${response.status})`);
            }
            const data = new Float32Array(await response.arrayBuffer());
            if (requestRevision !== revision) continue;
            const decoded=decodePreview(data);
            if(!shown || shown.params.seed!==params.seed || shown.cells!==decoded.cells)selected=-1;
            shown={...decoded,params,r,interpolate,revision:requestRevision};
            draw();
            message("편집기 전용 결과입니다. 게임 설정이나 지형은 변경하지 않았습니다.");
        }
    } catch (error) {
        message(error.message); $("map-status").textContent = "계산하지 못했습니다. 입력값을 확인해 주세요.";
        pending = false;
    } finally {
        busy = false; $("generate").textContent = "지정 단계 실행 · 미리보기";
        if (shown && shown.revision !== revision) $("map-status").textContent = "이전 설정의 이미지입니다. 미리보기를 다시 생성하세요.";
    }
}
function decodePreview(data) {
    const [version,width,height,cells,spacing,siteCount,steps,milliseconds,geometryMs,caMs,components,landCells,landArea,largestShare,averageDegree,geometryHit,siteStride,pixelStride,siteOffset,offset,edgeOffset,edgeCount,caHit,totalArea,initialSeeds,survivingSeeds,coastMs,coastHit,coastSegments,maxLandDistance,maxSeaDistance,hasDistances,hasTrends,trendMs,trendHit,trendBytes,boundaryOffset,boundaryCount,boundaryStride,transition]=data;
    if(version!==10||siteStride!==16||pixelStride!==5||siteOffset!==40||offset!==40+16*siteCount||edgeOffset!==offset+5*width*height||boundaryOffset!==edgeOffset+2*edgeCount||boundaryStride!==20||data.length!==boundaryOffset+20*boundaryCount)throw Error("미리보기 데이터 형식이 맞지 않습니다. 편집기를 다시 실행하세요.");
    return {data,width,height,cells,spacing,siteCount,steps,milliseconds,geometryMs,caMs,components,landCells,landArea,largestShare,averageDegree,geometryHit,siteOffset,offset,edgeOffset,edgeCount,caHit,totalArea,initialSeeds,survivingSeeds,coastMs,coastHit,coastSegments,maxLandDistance,maxSeaDistance,hasDistances,hasTrends,trendMs,trendHit,trendBytes,boundaryOffset,boundaryCount,transition};
}
const altitudeStops=[[-1,12,26,73],[-.5,25,91,141],[-.001,88,199,198],[0,167,204,106],[.4,142,133,80],[.75,167,137,112],[1,247,241,230]];
const compressionStops=[[0,55,81,140],[.5,142,104,171],[1,251,189,104]];
function trendColor(v,compression){
    const stops=compression?compressionStops:altitudeStops;
    for(let i=1;i<stops.length;i++)if(v<=stops[i][0]){const a=stops[i-1],b=stops[i],t=Math.max(0,(v-a[0])/(b[0]-a[0]));return [1,2,3].map(k=>Math.round(a[k]+(b[k]-a[k])*t));}
    return stops.at(-1).slice(1);
}
function showCell(){
    const s=shown;if(!s||selected<0){$("cell-info").textContent="지도의 셀을 클릭하면 대표값과 공유 경계·꼭짓점 값을 봅니다.";$("cell-boundaries").replaceChildren();return;}
    if(!s.hasTrends){$("cell-info").textContent=`셀 #${selected} · 경향 보기를 선택하면 전처리 정보를 계산합니다.`;$("cell-boundaries").replaceChildren();return;}
    const at=s.siteOffset+16*selected;
    $("cell-info").textContent=`셀 #${selected} · ${s.data[at+4]?"육지":"바다"} · 대표 고도 경향 ${s.data[at+14].toFixed(3)} · 압축 ${s.data[at+15].toFixed(3)} · 중심 X ${s.data[at].toFixed(1)}, Z ${s.data[at+1].toFixed(1)} (워핑 전). 실제 표면 높이 또는 바이옴 구분이 아닙니다.`;
    const table=document.createElement("table"),head=table.createTHead().insertRow();
    for(const name of ["이웃 셀","경계 중점 고도 / 압축","꼭짓점 A 고도 / 압축","꼭짓점 B 고도 / 압축"]){const th=document.createElement("th");th.textContent=name;head.append(th);}
    const body=table.createTBody();
    for(let i=0;i<s.boundaryCount;i++){const a=s.boundaryOffset+i*20,d=s.data;if(d[a]!==selected&&d[a+1]!==selected)continue;
        const own=d[a]===selected?0:1,neighbor=d[a+1-own],coast=(d[a+4]>0)!==(d[a+14]>0);
        for(const side of coast?[own,1-own]:[own]){
            const row=body.insertRow();row.insertCell().textContent=coast?(side===own?`내 쪽 → #${neighbor}`:`해안 반대쪽 #${neighbor}`):`공유 #${neighbor}`;
            for(const [v,coord] of (side===0?[[12,10],[4,2],[8,6]]:[[18,10],[14,2],[16,6]])){const td=row.insertCell();td.textContent=`${d[a+v].toFixed(3)} / ${d[a+v+1].toFixed(3)}`;td.title=`X ${d[a+coord].toFixed(2)}, Z ${d[a+coord+1].toFixed(2)} · ${coast?"육해 별도 값":"동종 공유 값"}`;}
        }
    }$("cell-boundaries").replaceChildren(table);
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
    const s=shown,canvas=document.createElement("canvas"),data=s.data;
    canvas.width=s.width;canvas.height=s.height;
    const ctx=canvas.getContext("2d"),image=ctx.createImageData(s.width,s.height);
    const view=$("view").value,edges=$("edges").checked;
    const distanceView=view.startsWith("coast-"),trendView=view.startsWith("trend-");
    const distanceScale=$("distance-scale").valueAsNumber;
    if(!Number.isFinite(distanceScale)||distanceScale<1||distanceScale>WORLD)return;
    if(distanceView&&!s.hasDistances){$("map-status").textContent="경계 거리 지도를 계산하는 중…";return;}
    if(trendView&&(!s.hasTrends||s.interpolate!==view.endsWith("-smooth"))){$("map-status").textContent="셀 경향 지도를 계산하는 중…";return;}
    const site=id=>s.siteOffset+16*id;
    const palette=Array.from({length:s.siteCount},(_,i)=>color(i));
    const materialColors=[[34,72,120],[112,155,79]];
    let landPixels=0;
    for(let z=0;z<s.height;z++)for(let x=0;x<s.width;x++){
        const px=z*s.width+x,at=s.offset+5*px,id=data[at],sa=site(id);
        landPixels+=data[sa+4];
        const edge=(x>0&&data[at-5]!==id)||(z>0&&data[at-5*s.width]!==id);
        let rgb=palette[id];
        if(view==="land"||view==="initial")rgb=materialColors[data[sa+(view==="land"?4:3)]];
        if(view==="continents")rgb=data[sa+8]?palette[data[sa+8]-1]:materialColors[0];
        if(view==="seed-count"){
            const t=Math.log2(Math.max(1,data[sa+9]))/Math.log2(Math.max(2,s.initialSeeds));
            rgb=data[sa+9]?[Math.round(207-197*t),Math.round(229-131*t),Math.round(147-66*t)]:materialColors[0];
        }
        if(view==="growth-rate"){const v=Math.round(data[sa+10]*255);rgb=[v,v,v];}
        if(view==="growth-large"||view==="growth-small"){const v=Math.round(255*Math.max(0,Math.min(1,.5+.5*data[sa+(view==="growth-large"?11:12)])));rgb=[v,v,v];}
        if(view==="random"){const v=Math.round(data[sa+2]*255);rgb=[v,v,v];}
        if(view==="neighbors"){const v=Math.round(Math.min(1,data[sa+5]/12)*255);rgb=[v,v,v];}
        if(view==="distance"){const v=Math.round(Math.min(1,data[at+1]/Math.SQRT2)*255);rgb=[v,v,v];}
        if(distanceView){
            const land=data[sa+4]!==0,active=view==="coast-both"||(view==="coast-land"?land:!land);
            const d=data[at+2],v=d<0?0:Math.round(255*Math.min(1,d/distanceScale));
            rgb=!active?(land?[26,43,29]:[23,37,56]):d<0?[87,65,105]:[v,v,v];
        }
        if(trendView)rgb=trendColor(data[at+(view.includes("compression")?4:3)],view.includes("compression"));
        if(view==="edges")rgb=edge?[214,228,222]:[22,29,34];
        else if(edges&&edge&&!distanceView&&!(trendView&&view.endsWith("-smooth")))rgb=trendView?rgb.map(v=>Math.round(v*.8)):[20,28,33];
        if(selected===id&&edge)rgb=[255,238,160];
        const dim=selected>=0&&id!==selected ? .35 : 1;
        image.data[px*4]=Math.round(rgb[0]*dim);image.data[px*4+1]=Math.round(rgb[1]*dim);
        image.data[px*4+2]=Math.round(rgb[2]*dim);image.data[px*4+3]=255;
    }
    ctx.putImageData(image,0,0);
    if(!distanceView&&!(trendView&&view.endsWith("-smooth"))&&$("sites").checked&&(!s.params.warp_enabled||s.params.warp_strength===0)){
        ctx.fillStyle="#fff";ctx.strokeStyle="#152027";ctx.lineWidth=1;
        const dot=(sx,sz)=>{
            for(let oz=-1;oz<=1;oz++)for(let ox=-1;ox<=1;ox++){
                const a=sx+ox*WORLD,b=sz+oz*WORLD;
                if(a<s.r.x0||a>s.r.x1||b<s.r.z0||b>s.r.z1)continue;
                ctx.beginPath();ctx.arc((a-s.r.x0)/(s.r.x1-s.r.x0)*(s.width-1),(b-s.r.z0)/(s.r.z1-s.r.z0)*(s.height-1),2,0,Math.PI*2);ctx.fill();ctx.stroke();
            }
        };
        for(let id=0;id<s.siteCount;id++){const a=site(id);dot(data[a],data[a+1]);}
    }
    mapNavigation.present(canvas,s.r,JSON.stringify([s.params,view,edges,$("sites").checked,selected,distanceScale]),s);
    $("map-status").textContent=`${s.steps}단계 · ${s.siteCount.toLocaleString()}개 셀 · ${s.width} × ${s.height} 표본 · 전체 ${s.milliseconds.toFixed(1)} ms (이웃 계산 ${s.geometryHit?"캐시":s.geometryMs.toFixed(1)+" ms"}, 성장 ${s.caHit?"캐시":s.caMs.toFixed(1)+" ms"}, 거리 준비 ${(s.hasDistances||s.hasTrends)?(s.coastHit?"캐시":s.coastMs.toFixed(1)+" ms"):"생략"})${s.revision!==revision?" · 이전 설정 이미지":""}`;
    $("land-stats").textContent=`${s.steps}단계 전체 월드: 초기 씨앗 ${s.initialSeeds}/${s.params.growth_seed_count}개 · 육지 ${(100*s.landArea).toFixed(1)}% · 육지 셀 ${s.landCells.toLocaleString()}개 · 연결된 육지 덩어리 ${s.components.toLocaleString()}개 · 가장 큰 덩어리는 육지의 ${(100*s.largestShare).toFixed(1)}% · 평균 이웃 ${s.averageDegree.toFixed(2)}개. 면적·연결 통계는 워핑 전 기준입니다. 현재 표시 범위의 최종 육지 표본 ${(100*landPixels/(s.width*s.height)).toFixed(1)}%.`;
    const coastLegend=`경계 0블록: 검정 → ${distanceScale.toLocaleString()}블록 이상: 흰색. 워핑 전 경계까지의 직선 최단거리이며 높이가 아닙니다. 경계선·생성점 표시는 잠시 생략합니다.${s.coastSegments?"":" 현재 월드에는 육지·바다 경계가 없어 거리를 정의할 수 없습니다(보라색)."}`;
    const legends={"trend-altitude":"셀 대표 고도 경향: 바다 −1(깊음, 남색) → 0(얕음, 청록), 육지 0(낮음, 연두) → +1(높음, 갈색·흰색). 연속값이며 실제 블록 높이가 아닙니다.","trend-compression":"셀 대표 압축 경향: 0(약함, 파랑) → 1(강함, 주황). 최종 지형의 밀도 수식에는 아직 연결하지 않았습니다.","trend-altitude-smooth":"육지는 육지끼리, 바다는 바다끼리 연결한 고도 경향입니다. 해안 양쪽 값은 독립적으로 유지하며, 실제 지형의 연결은 이후 생성기가 담당합니다.","trend-compression-smooth":"같은 육해 종류의 중심·경계·꼭짓점을 연결한 압축 경향입니다. 해안에서는 양쪽 값을 따로 유지합니다. 최종 지형이나 바이옴 경계가 아닙니다.","coast-land":coastLegend+" 바다는 짙은 파랑으로 제외합니다.","coast-sea":coastLegend+" 육지는 짙은 초록으로 제외합니다.","coast-both":coastLegend,"growth-rate":"검정 0% → 흰색 100%: 실제 셀별 성장 확률입니다. 같은 좌표에서는 모든 단계에 동일하게 적용합니다.","growth-large":"큰 굴곡 원신호: 검정 −1 → 흰색 +1. 씨앗이 아닌 월드 좌표 기준이며 범위 밖 색상만 포화됩니다.","growth-small":"작은 굴곡 원신호: 검정 −1 → 흰색 +1. 원신호는 큰 굴곡과 독립된 시드 스트림을 사용합니다.",land:"초록: 육지 · 파랑: 바다. 셀 경향 보기에서 지역의 성향을 확인할 수 있습니다.",initial:"0단계 씨앗 위치입니다. 위 통계는 지정한 최종 단계 기준입니다.",continents:"같은 색은 병합된 한 대륙입니다. 파랑은 바다입니다. 마우스를 올리면 대륙 ID와 씨앗 수가 나옵니다.","seed-count":"밝은 연두(1개) → 짙은 초록(전체 초기 씨앗 수): 대륙에 포함된 원래 씨앗 수를 로그 비율로 표시합니다. 파랑은 바다입니다.",random:"검정에 가까울수록 씨앗 배치 우선순위가 높습니다. 이미 배치한 씨앗의 이웃은 건너뜁니다.",neighbors:"워핑 전 실제 이웃 개수: 검정 0 → 흰색 12 이상. 마우스를 올리면 정확한 수를 봅니다.",distance:"생성점까지 거리: 검정 가까움 → 흰색 멀어짐. 셀 간격으로 정규화합니다."};
    if(s.hasTrends)$("map-status").textContent+=` · 전역 전처리 ${s.trendHit?"캐시 재사용":s.trendMs.toFixed(1)+" ms"} · 데이터 약 ${(s.trendBytes/1048576).toFixed(2)} MiB`;
    showCell();
    $("legend").textContent=legends[view]||"색은 셀 ID 구분용입니다. 선은 표시 해상도에서 이웃 표본의 셀 ID가 달라지는 곳입니다.";

}
const wrap = x => ((x%WORLD)+WORLD)%WORLD;
function setView(cx,cz,width,height) {
    width=Math.max(1,Math.min(WORLD,width));height=Math.max(1,Math.min(WORLD,height));
    cx=wrap(cx);cz=wrap(cz);
    const values={x0:cx-width/2,z0:cz-height/2,x1:cx+width/2,z1:cz+height/2};
    for(const [key,value] of Object.entries(values)) $(key).value=Number(value.toFixed(4));
    invalidate();
}
function zoom(factor) { mapNavigation.zoom(factor); }
function download(blob,name) {
    const url=URL.createObjectURL(blob),a=document.createElement("a");a.href=url;a.download=name;a.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
}
$("parameters").onsubmit=$("range").onsubmit=event=>{event.preventDefault();generate();};
for(const id of [...keys,...rangeKeys,"resolution"]) $(id).addEventListener("change",()=>invalidate());
for(const id of [...keys,...rangeKeys]) $(id).addEventListener("input",saveLabel);
for(const id of ["edges","sites","distance-scale"]) $(id).addEventListener("change",()=>{draw();saveLabel();});
$("view").addEventListener("change",()=>{
    const view=$("view").value,trend=view.startsWith("trend-"),distance=view.startsWith("coast-");
    if((distance&&!shown?.hasDistances)||(trend&&(!shown?.hasTrends||shown.interpolate!==view.endsWith("-smooth")))||((trend||distance)&&(busy||shown?.revision!==revision))){invalidate();generate();}
    else {draw();saveLabel();}
});
$("auto").onchange=()=>{saveLabel();if($("auto").checked)generate();else clearTimeout(timer);};
$("generate").onclick=generate;
$("initial-state").onclick=()=>{$("growth_steps").value=0;invalidate();generate();};
$("step").onclick=()=>{if(!valid())return;$("growth_steps").value=Math.min(100,$("growth_steps").valueAsNumber+1);invalidate();generate();};
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
        invalidate();message(data.version<5?"이전 실험의 셀 배치·워핑을 유지하고 씨앗 성장 초기값으로 전환했습니다.":"실험 설정을 불러왔습니다. 마스터 시드는 기후 편집기와 공유합니다.");
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
const mapNavigation=createMapNavigator({
    canvas:$("map"),readRange,periodic:true,
    changeRange:r=>{
        for(const key of rangeKeys)$(key).value=Number(r[key].toFixed(4));
        invalidate(true);
    },
    settled:()=>{if($("auto").checked)generate();},
    hover:hit=>{
        if(!hit){$("sample").textContent="이 부분은 아직 계산된 표본이 없습니다.";return;}
        const {payload:s,x,z,worldX,worldZ}=hit;
        const at=s.offset+5*(z*s.width+x),id=s.data[at],sa=s.siteOffset+16*id;
        $("sample").textContent=`X ${worldX.toFixed(1)} · Z ${worldZ.toFixed(1)} · 셀 #${id} · ${s.data[sa+4]?"육지":"바다"} (초기 ${s.data[sa+3]?"육지":"바다"}) · 이웃 ${s.data[sa+5]}개 · 현재 육지 이웃 ${(s.data[sa+7]*100).toFixed(1)}% · ${s.data[sa+8]?`대륙 #${s.data[sa+8]} · 원래 씨앗 ${s.data[sa+9]}개`:"대륙 없음"} · 성장 확률 ${(s.data[sa+10]*100).toFixed(2)}% · 경계 거리 ${!s.hasDistances?"거리 보기에서 계산":s.data[at+2]<0?"경계 없음":s.data[at+2].toFixed(1)+"블록"} · 고도 경향 ${s.hasTrends?s.data[at+3].toFixed(3):"경향 보기에서 계산"} · 압축 ${s.hasTrends?s.data[at+4].toFixed(3):"경향 보기에서 계산"} · 표시된 지도 표본`;
    },
    select:hit=>{
        if(!hit)return;
        const {payload:s,x,z}=hit;
        selected=s.data[s.offset+5*(z*s.width+x)];draw();
    }
});
async function start() {
    try {
        storageState=await workspaceApi();
        if(storageState.voronoi)applySettings(storageState.voronoi);
        $("seed").value=storageState.base_seed;savedText=JSON.stringify(settings());
        masterSeed=createMasterSeed(token,storageState.base_seed,seed=>{$("seed").value=seed;invalidate();});
        $("seed").value=masterSeed.value;storageReady=true;
        $("save-settings").disabled=$("load-settings").disabled=false;
        mapNavigation.sync();updateHints();saveLabel();generate();
    }catch(error){message(error.message);$("save-state").textContent="저장된 작업을 읽지 못했습니다. 파일은 변경하지 않았습니다.";}
}
start();
