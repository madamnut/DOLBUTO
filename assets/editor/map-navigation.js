"use strict";
// Display navigation is independent of native sampling. Images retain their world bounds.
window.createMapNavigator = function({canvas, readRange, changeRange, settled, hover, select, periodic=false}) {
    const WORLD=131072, ctx=canvas.getContext("2d");
    let view={...readRange()}, target={...view}, frames=[], contentKey, dragging, animation=0, previousTime=0, notifySettled=false;
    const span=r=>({w:r.x1-r.x0,h:r.z1-r.z0});
    const valid=r=>Object.values(r).every(Number.isFinite)&&r.x1-r.x0>=1&&r.z1-r.z0>=1&&r.x1-r.x0<=WORLD&&r.z1-r.z0<=WORLD;
    const wrap=n=>((n%WORLD)+WORLD)%WORLD;
    function bounded(r, canonical=false) {
        const {w,h}=span(r);let x=r.x0,z=r.z0;
        if(periodic) {
            if(canonical){x=wrap(x+w/2)-w/2;z=wrap(z+h/2)-h/2;}
        } else {x=Math.max(0,Math.min(WORLD-w,x));z=Math.max(0,Math.min(WORLD-h,z));}
        return {x0:x,z0:z,x1:x+w,z1:z+h};
    }
    function commit() { changeRange(bounded(view,true)); }
    function copies(frame) {
        if(!periodic)return [[0,0]];
        const r=frame.range, ox=Math.round(((view.x0+view.x1)-(r.x0+r.x1))/(2*WORLD))*WORLD,
            oz=Math.round(((view.z0+view.z1)-(r.z0+r.z1))/(2*WORLD))*WORLD;
        const result=[];
        for(let z=-1;z<=1;z++)for(let x=-1;x<=1;x++)result.push([ox+x*WORLD,oz+z*WORLD]);
        return result;
    }
    function paint() {
        if(!valid(view))return;
        ctx.clearRect(0,0,canvas.width,canvas.height);
        ctx.fillStyle="#12171b";ctx.fillRect(0,0,canvas.width,canvas.height);
        ctx.imageSmoothingEnabled=!!animation||!!dragging;
        const {w,h}=span(view);
        // Coarse coverage first, then more recent detailed images.
        for(const frame of frames) {
            const r=frame.range;
            for(const [ox,oz] of copies(frame)) {
                const x=(r.x0+ox-view.x0)/w*canvas.width,z=(r.z0+oz-view.z0)/h*canvas.height;
                const fw=(r.x1-r.x0)/w*canvas.width,fh=(r.z1-r.z0)/h*canvas.height;
                if(x>=canvas.width||z>=canvas.height||x+fw<=0||z+fh<=0)continue;
                ctx.drawImage(frame.image,x,z,fw,fh);
            }
        }
    }
    function stopAnimation() {
        if(animation)cancelAnimationFrame(animation);
        animation=0;previousTime=0;
    }
    function finish() {
        stopAnimation();view={...target};commit();paint();
        if(notifySettled&&!dragging){notifySettled=false;settled();}
    }
    function animate(time) {
        const dt=previousTime?Math.min(50,time-previousTime):16;
        previousTime=time;
        const t=1-Math.exp(-dt/55);
        for(const key of ["x0","z0","x1","z1"])view[key]+=(target[key]-view[key])*t;
        const {w,h}=span(target);
        const error=Math.max(Math.abs(view.x0-target.x0)/w,Math.abs(view.x1-target.x1)/w,
            Math.abs(view.z0-target.z0)/h,Math.abs(view.z1-target.z1)/h);
        if(error<.0002){finish();return;}
        commit();paint();animation=requestAnimationFrame(animate);
    }
    function zoom(factor,ax=.5,az=.5) {
        if(!frames.length||dragging||!Number.isFinite(factor)||factor<=0)return;
        const current=span(view), next=span(target);
        // Accumulate wheel deltas but keep the point currently under the cursor fixed.
        factor=Math.max(Math.max(1/next.w,1/next.h),Math.min(Math.min(WORLD/next.w,WORLD/next.h),factor));
        const w=next.w*factor,h=next.h*factor;
        const x=view.x0+ax*current.w-ax*w,z=view.z0+az*current.h-az*h;
        target=bounded({x0:x,z0:z,x1:x+w,z1:z+h});notifySettled=true;
        if(!animation){previousTime=0;animation=requestAnimationFrame(animate);}
    }
    function position(event) {
        const rect=canvas.getBoundingClientRect();
        return {ax:Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width)),
            az:Math.max(0,Math.min(1,(event.clientY-rect.top)/rect.height))};
    }
    function hit(event) {
        const {ax,az}=position(event),{w,h}=span(view),wx=view.x0+ax*w,wz=view.z0+az*h;
        for(let i=frames.length-1;i>=0;i--) {
            const f=frames[i],r=f.range;
            for(const [ox,oz] of copies(f)) {
                const x=wx-ox,z=wz-oz;
                if(x<r.x0||x>r.x1||z<r.z0||z>r.z1)continue;
                return {payload:f.payload,x:Math.min(f.image.width-1,Math.floor((x-r.x0)/(r.x1-r.x0)*f.image.width)),
                    z:Math.min(f.image.height-1,Math.floor((z-r.z0)/(r.z1-r.z0)*f.image.height)),
                    worldX:periodic?wrap(wx):wx,worldZ:periodic?wrap(wz):wz};
            }
        }
        return null;
    }
    function move(event) {
        if(!dragging||dragging.id!==event.pointerId)return;
        const dx=event.clientX-dragging.x,dz=event.clientY-dragging.z;
        if(!dragging.moved&&Math.hypot(dx,dz)<4)return;
        dragging.moved=true;
        const r=dragging.range,{w,h}=span(r);
        view=bounded({x0:r.x0-dx/dragging.width*w,x1:r.x1-dx/dragging.width*w,
            z0:r.z0-dz/dragging.height*h,z1:r.z1-dz/dragging.height*h});
        target={...view};notifySettled=true;commit();paint();
    }
    function release(event, cancelled=false) {
        if(!dragging||(event&&event.pointerId!==dragging.id))return;
        if(event&&!cancelled)move(event);
        const d=dragging;dragging=undefined;canvas.classList.remove("dragging");
        if(canvas.hasPointerCapture(d.id))canvas.releasePointerCapture(d.id);
        paint();
        if(notifySettled){notifySettled=false;settled();}
        if(!cancelled&&!d.moved&&event)select?.(hit(event));
    }
    canvas.classList.add("navigable-map");
    canvas.addEventListener("wheel",event=>{
        if(!frames.length)return;
        event.preventDefault();
        const unit=event.deltaMode===1?16:event.deltaMode===2?canvas.getBoundingClientRect().height:1;
        const delta=Math.max(-600,Math.min(600,event.deltaY*unit));
        if(!delta)return;
        const {ax,az}=position(event);zoom(Math.exp(delta*.0018),ax,az);
    },{passive:false});
    canvas.addEventListener("pointerdown",event=>{
        if(event.button!==0||!event.isPrimary||!frames.length||dragging)return;
        event.preventDefault();stopAnimation();target={...view};
        const rect=canvas.getBoundingClientRect();
        dragging={id:event.pointerId,x:event.clientX,z:event.clientY,width:rect.width,height:rect.height,range:{...view},moved:false};
        canvas.setPointerCapture(event.pointerId);canvas.classList.add("dragging");
    });
    canvas.addEventListener("pointermove",event=>{if(dragging)move(event);else hover?.(hit(event));});
    canvas.addEventListener("pointerup",event=>release(event));
    canvas.addEventListener("pointercancel",event=>release(event,true));
    canvas.addEventListener("lostpointercapture",event=>release(event,true));
    window.addEventListener("blur",()=>{release(null,true);if(animation)finish();});
    document.addEventListener("visibilitychange",()=>{if(document.hidden){release(null,true);if(animation)finish();}});
    return {
        zoom,
        sync(range=readRange()) {
            if(!valid(range))return;
            stopAnimation();notifySettled=false;
            // Input forms and presets deliberately replace the current gesture's view.
            if(dragging){const id=dragging.id;dragging=undefined;canvas.classList.remove("dragging");if(canvas.hasPointerCapture(id))canvas.releasePointerCapture(id);}
            view=bounded({...range});target={...view};paint();
        },
        present(source,range,key,payload) {
            const image=document.createElement("canvas");image.width=source.width;image.height=source.height;
            image.getContext("2d").drawImage(source,0,0);
            if(contentKey!==key){frames=[];contentKey=key;}
            frames=frames.filter(f=>JSON.stringify(f.range)!==JSON.stringify(range));
            frames.push({image,range:{...range},payload});
            // Keep broad coverage plus the latest three frames, bounded in memory.
            if(frames.length>4){const broad=frames.reduce((a,b)=>span(a.range).w*span(a.range).h>span(b.range).w*span(b.range).h?a:b);frames=[broad,...frames.filter(f=>f!==broad).slice(-3)];}
            canvas.width=source.width;canvas.height=source.height;paint();
        }
    };
};
