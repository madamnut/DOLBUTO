"use strict";
// One master seed shared by tabs belonging to the same running editor; no game-file writes.
window.createMasterSeed = function(session, initial, onChange) {
    const key = "dolbuto-editor-master-seed";
    const valid = n => Number.isInteger(n) && n >= 0 && n <= 4294967295;
    let value = initial;
    try { const saved=JSON.parse(localStorage.getItem(key)); if(saved?.session===session && valid(saved.seed)) value=saved.seed; } catch {}
    const channel = typeof BroadcastChannel === "function" ? new BroadcastChannel("dolbuto-seed-"+session) : null;
    function accept(seed) { if(valid(seed) && seed!==value) {value=seed;onChange(seed);} }
    function store() { try {localStorage.setItem(key,JSON.stringify({session,seed:value}));} catch {} }
    store();
    window.addEventListener("storage", event=>{
        if(event.key!==key)return;
        try {const data=JSON.parse(event.newValue);if(data?.session===session)accept(data.seed);} catch {}
    });
    if(channel) channel.onmessage=event=>{
        if(event.data?.type==="request")channel.postMessage({type:"value",seed:value});
        else if(event.data?.type==="value")accept(event.data.seed);
    };
    channel?.postMessage({type:"request"});
    return {get value(){return value;},set(seed){if(!valid(seed)||seed===value)return;value=seed;store();channel?.postMessage({type:"value",seed});}};
};
