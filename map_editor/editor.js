"use strict";

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const clone = (value) => JSON.parse(JSON.stringify(value));
const FACES = ["top", "north", "east", "south", "west", "plane"];
const COLORS = { selected: [0.71, 0.87, 0.33], override: [0.94, 0.62, 0.30] };
const state = {
  document: null, rules: null, revision: null, selection: new Set(), tool: "point",
  view: "original", dirty: false, history: [], future: [], dragStart: null,
  atlases: {}, layerAtlases: {}, faceMaterials: {}, materialFace: null, materialRole: "primary", pickFace: null,
  draftRule: null,
};

function status(message, error = false) {
  const node = $("#status");
  node.textContent = message;
  node.classList.toggle("error", error);
}

async function request(url, options) {
  const response = await fetch(url, options);
  const value = await response.json();
  if (!response.ok) throw new Error(value.error || `HTTP ${response.status}`);
  return value;
}

async function initialize() {
  bindControls();
  try {
    const { maps } = await request("/api/maps");
    $("#map-select").replaceChildren(...maps.map((map) => {
      const option = document.createElement("option");
      option.value = map.symbol;
      option.textContent = map.symbol.replace(/^MAP_/, "").replaceAll("_", " ");
      return option;
    }));
    const preferred = maps.find((map) => map.symbol === "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F");
    if (preferred) $("#map-select").value = preferred.symbol;
    if (maps.length) await openMap();
  } catch (error) { status(error.message, true); }
}

async function openMap() {
  if (state.dirty && !confirm("Discard unsaved visual rule changes?")) return;
  status("Opening map...");
  try {
    const document = await request(`/api/map?symbol=${encodeURIComponent($("#map-select").value)}`);
    state.document = document;
    state.rules = clone(document.rules);
    state.revision = document.editor.revision;
    state.selection.clear(); state.history = []; state.future = []; state.dirty = false; state.draftRule = null;
    state.atlases = {}; state.layerAtlases = {};
    await Promise.all(Object.entries(document.tilesets).map(async ([role, info]) => {
      const loadImage = async (source) => {
        const image = new Image();
        const loaded = new Promise((resolve, reject) => { image.onload = resolve; image.onerror = reject; });
        image.src = source; await loaded; return image;
      };
      const [full, base, foreground] = await Promise.all([
        loadImage(info.atlas), loadImage(info.atlasLayers.base), loadImage(info.atlasLayers.foreground),
      ]);
      state.atlases[role] = full;
      state.layerAtlases[role] = {full, base, foreground};
    }));
    $("#map-title").textContent = document.map.symbol.replace(/^MAP_/, "").replaceAll("_", " ");
    $("#map-size").textContent = `${document.map.width} x ${document.map.height}`;
    $("#rule-path").textContent = document.ruleSource;
    const templates = Object.keys(document.editor.templates);
    $("#building-template").replaceChildren(...templates.map((name) => new Option(name.replaceAll("_", " "), name)));
    resetCamera(); updateAll(); status("Map ready");
  } catch (error) { status(error.message, true); }
}

function bindControls() {
  $("#open-map").addEventListener("click", openMap);
  $("#save").addEventListener("click", saveRules);
  $("#validate").addEventListener("click", validateRules);
  $("#regenerate").addEventListener("click", regenerateRules);
  $("#undo").addEventListener("click", undo);
  $("#redo").addEventListener("click", redo);
  $("#apply-geometry").addEventListener("click", applyGeometry);
  $("#remove-overrides").addEventListener("click", removeOverrides);
  $("#clear-faces").addEventListener("click", clearFaces);
  $("#place-building").addEventListener("click", placeBuilding);
  $("#remove-building").addEventListener("click", removeBuilding);
  $("#apply-sign-preset").addEventListener("click", applySignPreset);
  $("#reset-camera").addEventListener("click", resetCamera);
  ["#shape", "#ground-height", "#height", "#axis", "#profile", "#base-metatile"].forEach((id) => {
    $(id).addEventListener("input", updateDraftPreview);
    $(id).addEventListener("change", updateDraftPreview);
  });
  $$("[data-tool]").forEach((button) => button.addEventListener("click", () => setTool(button.dataset.tool)));
  $$("[data-view]").forEach((button) => button.addEventListener("click", () => {
    state.view = button.dataset.view;
    $$("[data-view]").forEach((item) => item.classList.toggle("active", item === button));
    drawMap();
  }));
  ["#show-events", "#show-elevation", "#show-collision"].forEach((id) => $(id).addEventListener("change", drawMap));
  $("#eyedropper").addEventListener("click", () => {
    state.pickFace = state.pickFace || "top";
    $("#eyedropper").classList.toggle("active", Boolean(state.pickFace));
    status(`Pick a map cell for the ${state.pickFace} face`);
  });
  $$(".catalog-tabs button").forEach((button) => button.addEventListener("click", () => {
    state.materialRole = button.dataset.role;
    $$(".catalog-tabs button").forEach((item) => item.classList.toggle("active", item === button));
    renderMaterialCatalog();
  }));
  $("#material-search").addEventListener("input", renderMaterialCatalog);
  window.addEventListener("keydown", keyboardShortcuts);
  window.addEventListener("beforeunload", (event) => { if (state.dirty) event.preventDefault(); });
  bindMapPointer(); bindPreviewPointer();
}

function keyboardShortcuts(event) {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "z") {
    event.preventDefault(); event.shiftKey ? redo() : undo(); return;
  }
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
    event.preventDefault(); saveRules(); return;
  }
  if (/^(INPUT|SELECT)$/.test(event.target.tagName)) return;
  const tools = { v: "point", r: "rectangle", f: "fill" };
  if (tools[event.key.toLowerCase()]) setTool(tools[event.key.toLowerCase()]);
}

function setTool(tool) {
  state.tool = tool; state.pickFace = null;
  $$("[data-tool]").forEach((button) => button.classList.toggle("active", button.dataset.tool === tool));
  $("#eyedropper").classList.remove("active");
}

function cellAtCanvas(event) {
  if (!state.document) return null;
  const canvas = $("#map-canvas");
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor((event.clientX - rect.left) * canvas.width / rect.width / 32);
  const y = Math.floor((event.clientY - rect.top) * canvas.height / rect.height / 32);
  if (x < 0 || y < 0 || x >= state.document.map.width || y >= state.document.map.height) return null;
  return { x, y, id: `${x},${y}` };
}

function bindMapPointer() {
  const canvas = $("#map-canvas");
  canvas.addEventListener("pointerdown", (event) => {
    const cell = cellAtCanvas(event); if (!cell) return;
    if (state.pickFace) { pickMaterialFromCell(cell); return; }
    state.dragStart = cell; canvas.setPointerCapture(event.pointerId);
    if (state.tool === "point") selectCells([cell.id], event.ctrlKey || event.metaKey);
    if (state.tool === "fill") fillSelection(cell, event.ctrlKey || event.metaKey);
  });
  canvas.addEventListener("pointerup", (event) => {
    const end = cellAtCanvas(event);
    if (state.tool === "rectangle" && state.dragStart && end) {
      const ids = [];
      for (let y = Math.min(end.y, state.dragStart.y); y <= Math.max(end.y, state.dragStart.y); y++)
        for (let x = Math.min(end.x, state.dragStart.x); x <= Math.max(end.x, state.dragStart.x); x++) ids.push(`${x},${y}`);
      selectCells(ids, event.ctrlKey || event.metaKey);
    }
    state.dragStart = null;
  });
}

function fillSelection(start, additive) {
  const width = state.document.map.width;
  const target = state.document.cells[start.y * width + start.x].metatile;
  const pending = [start], visited = new Set(), ids = [];
  while (pending.length) {
    const cell = pending.pop(), id = `${cell.x},${cell.y}`;
    if (visited.has(id) || cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= state.document.map.height) continue;
    visited.add(id);
    if (state.document.cells[cell.y * width + cell.x].metatile !== target) continue;
    ids.push(id);
    pending.push({x:cell.x-1,y:cell.y},{x:cell.x+1,y:cell.y},{x:cell.x,y:cell.y-1},{x:cell.x,y:cell.y+1});
  }
  selectCells(ids, additive);
}

function selectCells(ids, additive = false) {
  if (!additive) state.selection.clear();
  ids.forEach((id) => state.selection.add(id));
  state.draftRule = null;
  updateAll();
}

function cellById(id) {
  const [x, y] = id.split(",").map(Number);
  return state.document.cells[y * state.document.map.width + x];
}

function overrideAt(cell) { return state.rules.overrides.find((item) => item.x === cell.x && item.y === cell.y); }

function effectiveAt(cell) {
  if (state.draftRule && state.selection.has(cell.id)) return { source: "inspector draft", rule: state.draftRule };
  const override = overrideAt(cell);
  if (override) return { source: "map override", rule: normalizeClientRule(override) };
  const sign = state.document.events.background.some((event) => event.type === "sign" && event.x === cell.x && event.y === cell.y);
  if (sign && state.rules.eventRules?.sign) return { source: "event sign", rule: normalizeClientRule(state.rules.eventRules.sign) };
  const inherited = state.document.editor.resolved[state.document.cells.indexOf(cell)];
  if (inherited.source === "tileset") return inherited;
  const building = state.rules.buildings.find((item) => {
    const template = state.document.editor.templates[item.template];
    return cell.x >= item.x && cell.x < item.x + template.width && cell.y >= item.y && cell.y < item.y + template.height;
  });
  if (building) {
    const template = state.document.editor.templates[building.template];
    const roof = cell.y - building.y < template.roofRows;
    return { source: `building:${building.template}`, rule: normalizeClientRule({shape:roof?"roof":"building-part",profile:template.profile,height:template.bodyHeight+(roof?template.roofHeight:0),faces:template.faces||{}}) };
  }
  return inherited;
}

function normalizeClientRule(rule) {
  const faces = Object.fromEntries(FACES.map((face) => [face, {metatile:"self",layer:face === "plane"?"foreground":"full"}]));
  Object.assign(faces, clone(rule.faces || {}));
  return {shape:rule.shape||"flat",profile:rule.profile||"none",axis:rule.axis||"x",baseMetatile:rule.baseMetatile??"self",groundHeight:rule.groundHeight||0,height:rule.height||0,faces};
}

function drawMap() {
  if (!state.document) return;
  const canvas = $("#map-canvas"), context = canvas.getContext("2d");
  const scale = 32, { width, height } = state.document.map;
  canvas.width = width * scale; canvas.height = height * scale;
  context.imageSmoothingEnabled = false;
  state.document.cells.forEach((cell, index) => {
    const image = state.atlases[cell.tilesetRole];
    const sourceX = (cell.localMetatile % 16) * 16, sourceY = Math.floor(cell.localMetatile / 16) * 16;
    context.drawImage(image, sourceX, sourceY, 16, 16, cell.x * scale, cell.y * scale, scale, scale);
    if (state.view === "resolved") {
      const source = effectiveAt(cell).source;
      context.fillStyle = source === "map override" ? "#ef9d4d66" : source.startsWith("building") ? "#5ecbc766" : "#151a1644";
      context.fillRect(cell.x * scale, cell.y * scale, scale, scale);
    }
    if (state.view === "collision" || $("#show-collision").checked) {
      context.fillStyle = ["#0000", "#ed5f5677", "#ef9d4d77", "#9e62d677"][cell.collision];
      context.fillRect(cell.x * scale, cell.y * scale, scale, scale);
    }
    if ($("#show-elevation").checked) {
      context.fillStyle = "#080b09cc"; context.fillRect(cell.x*scale+1,cell.y*scale+1,12,11);
      context.fillStyle = "#fff"; context.font = "9px monospace"; context.fillText(String(cell.elevation),cell.x*scale+3,cell.y*scale+10);
    }
    if (state.selection.has(cell.id)) {
      context.strokeStyle = "#b5df55"; context.lineWidth = 3; context.strokeRect(cell.x*scale+2,cell.y*scale+2,scale-4,scale-4);
    }
    if (index % width !== width - 1) { context.strokeStyle="#0003"; context.lineWidth=1; context.strokeRect(cell.x*scale,cell.y*scale,scale,scale); }
  });
  if ($("#show-events").checked) drawEvents(context, scale);
}

function drawEvents(context, scale) {
  const types = [["objects","#5ecbc7"],["warps","#c57ce8"],["background","#ef9d4d"],["coordinates","#f0d15f"]];
  types.forEach(([type,color]) => state.document.events[type].forEach((event) => {
    context.fillStyle=color; context.beginPath(); context.arc((event.x+.5)*scale,(event.y+.5)*scale,5,0,Math.PI*2); context.fill();
    context.strokeStyle="#111"; context.lineWidth=2; context.stroke();
  }));
}

function updateInspector() {
  const cells = [...state.selection].map(cellById);
  $("#empty-inspector").hidden = cells.length > 0; $("#inspector-content").hidden = !cells.length;
  $("#selection-count").textContent = `${cells.length} cell${cells.length === 1 ? "" : "s"}`;
  if (!cells.length) { state.draftRule=null; $("#selection-bounds").textContent="No selection"; $("#cell-heading").textContent="No cells selected"; return; }
  const xs=cells.map(c=>c.x), ys=cells.map(c=>c.y), first=cells[0], effective=effectiveAt(first), rule=normalizeClientRule(effective.rule);
  $("#selection-bounds").textContent=`${Math.min(...xs)},${Math.min(...ys)} → ${Math.max(...xs)},${Math.max(...ys)}`;
  $("#cell-heading").textContent=cells.length===1?`Cell ${first.id}`:`${cells.length} cells`;
  $("#rule-source").textContent=effective.source;
  $("#cell-facts").innerHTML=[["Metatile",`0x${first.metatile.toString(16).toUpperCase()}`],["Local ID",`0x${first.localMetatile.toString(16).toUpperCase()}`],["Behavior",state.document.editor.behaviorNames[first.behavior]||first.behavior],["Layer / collision",`${first.layerType} / ${first.collision}`]].map(([key,value])=>`<div class="fact"><small>${key}</small><strong>${value}</strong></div>`).join("");
  $("#shape").value=rule.shape; $("#ground-height").value=rule.groundHeight; $("#height").value=rule.height; $("#axis").value=rule.axis; $("#profile").value=rule.profile; $("#base-metatile").value=rule.baseMetatile === "self" ? "" : rule.baseMetatile;
  state.faceMaterials=clone(rule.faces); renderFaces(first);
  state.draftRule=captureDraftRule();
}

function renderFaces(cell) {
  $("#face-list").replaceChildren(...FACES.map((face) => {
    const row=document.createElement("div"); row.className="face-row";
    const label=document.createElement("span"); label.textContent=face;
    const swatch=document.createElement("span"); swatch.className="swatch";
    const material=state.faceMaterials[face]; const id=material === "none" ? null : material.metatile;
    if (id !== null) setSwatch(swatch,id === "self" ? cell.metatile : Number(id));
    const button=document.createElement("button"); button.textContent=material === "none" ? "none" : `${id} · ${material.layer}`;
    button.addEventListener("click",()=>openMaterial(face)); row.append(label,swatch,button); return row;
  }));
}

function setSwatch(node, globalId) {
  const role=globalId>=512?"secondary":"primary", local=globalId-(role==="secondary"?512:0), image=state.atlases[role];
  node.style.backgroundImage=`url(${image.src})`; node.style.backgroundSize=`${image.width*2}px ${image.height*2}px`; node.style.backgroundPosition=`-${(local%16)*32}px -${Math.floor(local/16)*32}px`;
}

function openMaterial(face) { state.materialFace=face; $("#material-title").textContent=`Choose ${face} material`; renderMaterialCatalog(); $("#material-dialog").showModal(); }
function renderMaterialCatalog() {
  if (!state.document) return;
  const role=state.materialRole, info=state.document.tilesets[role], query=$("#material-search").value.trim().toLowerCase(), fragment=document.createDocumentFragment();
  for(let local=0;local<info.count;local++){
    const metadata=info.catalog[local],global=metadata.global,text=`${local} 0x${local.toString(16)} ${global} 0x${global.toString(16)} ${metadata.behavior.toLowerCase()}`;
    if(query&&!text.includes(query))continue;
    const button=document.createElement("button");button.type="button";button.className="material";
    const preview=document.createElement("canvas");preview.width=16;preview.height=16;preview.getContext("2d").drawImage(state.atlases[role],(local%16)*16,Math.floor(local/16)*16,16,16,0,0,16,16);
    const strong=document.createElement("strong");strong.textContent=`0x${local.toString(16).toUpperCase().padStart(3,"0")}`;const small=document.createElement("small");small.textContent=`global 0x${global.toString(16).toUpperCase().padStart(3,"0")} · L${metadata.layerType} · ${metadata.behavior}`;
    button.append(preview,strong,small);button.addEventListener("click",()=>chooseMaterial(global));fragment.append(button);
  }
  $("#material-grid").replaceChildren(fragment);
}

function refreshDraftMaterials() { const first=state.selection.size?cellById(state.selection.values().next().value):null; if(first)renderFaces(first); updateDraftPreview(); }
function chooseMaterial(globalId) { const layer=$("#material-layer").value; state.faceMaterials[state.materialFace]=layer==="none"?"none":{metatile:globalId,layer}; $("#material-dialog").close(); refreshDraftMaterials(); }
function pickMaterialFromCell(cell) { state.faceMaterials[state.pickFace]={metatile:cell.metatile,layer:"full"}; status(`Picked 0x${cell.metatile.toString(16)} for ${state.pickFace}`); state.pickFace=null; $("#eyedropper").classList.remove("active"); refreshDraftMaterials(); }
function clearFaces() { FACES.forEach((face)=>state.faceMaterials[face]={metatile:"self",layer:face==="plane"?"foreground":"full"}); refreshDraftMaterials(); }

function mutateRules(action, callback) { state.history.push(clone(state.rules)); if(state.history.length>100)state.history.shift(); state.future=[]; callback(); state.draftRule=null; state.dirty=true; updateAll(); status(action); }
function undo(){if(!state.history.length)return;state.future.push(clone(state.rules));state.rules=state.history.pop();state.draftRule=null;state.dirty=true;updateAll();status("Undid change")}
function redo(){if(!state.future.length)return;state.history.push(clone(state.rules));state.rules=state.future.pop();state.draftRule=null;state.dirty=true;updateAll();status("Redid change")}

function ruleFromInspector() {
  const shape=$("#shape").value, height=Number($("#height").value), groundHeight=Number($("#ground-height").value), base=$("#base-metatile").value;
  if((shape==="extruded"||shape==="cutout")&&height<=0)throw new Error(`${shape} requires a positive height`);
  if(shape==="cutout"&&base==="")throw new Error("Cutout requires a base metatile");
  const rule={shape,groundHeight,height,axis:$("#axis").value,profile:$("#profile").value,faces:clone(state.faceMaterials)};
  if(shape==="cutout")rule.baseMetatile=Number(base);
  return rule;
}

function captureDraftRule(){const base=$("#base-metatile").value;const rule={shape:$("#shape").value,groundHeight:Number($("#ground-height").value)||0,height:Math.max(0,Number($("#height").value)||0),axis:$("#axis").value,profile:$("#profile").value,faces:clone(state.faceMaterials)};if(base!=="")rule.baseMetatile=Number(base);return rule}
function updateDraftPreview(){if(!state.selection.size)return;state.draftRule=captureDraftRule();renderPreview();status("Previewing unsaved inspector changes")}

function applyGeometry(){if(!state.selection.size)return;try{const definition=ruleFromInspector();mutateRules("Applied map overrides",()=>{for(const id of state.selection){const cell=cellById(id),existing=overrideAt(cell),next={x:cell.x,y:cell.y,...clone(definition)};if(existing)Object.assign(existing,next);else state.rules.overrides.push(next)}state.rules.overrides.sort((a,b)=>a.y-b.y||a.x-b.x)})}catch(error){status(error.message,true)}}
function removeOverrides(){if(!state.selection.size)return;mutateRules("Removed selected overrides",()=>{state.rules.overrides=state.rules.overrides.filter((item)=>!state.selection.has(`${item.x},${item.y}`))})}

function selectionOrigin(){const cells=[...state.selection].map(cellById);return cells.length?{x:Math.min(...cells.map(c=>c.x)),y:Math.min(...cells.map(c=>c.y))}:null}
function placeBuilding(){const origin=selectionOrigin(),name=$("#building-template").value;if(!origin||!name)return;const template=state.document.editor.templates[name];if(origin.x+template.width>state.document.map.width||origin.y+template.height>state.document.map.height){status("Building does not fit in the map",true);return}const overlap=state.rules.buildings.some((item)=>{const other=state.document.editor.templates[item.template];return origin.x<item.x+other.width&&origin.x+template.width>item.x&&origin.y<item.y+other.height&&origin.y+template.height>item.y});if(overlap){status("Building overlaps another building",true);return}mutateRules("Placed building template",()=>state.rules.buildings.push({template:name,x:origin.x,y:origin.y}))}
function removeBuilding(){if(!state.selection.size)return;mutateRules("Removed building",()=>{state.rules.buildings=state.rules.buildings.filter((item)=>{const t=state.document.editor.templates[item.template];return ![...state.selection].some((id)=>{const c=cellById(id);return c.x>=item.x&&c.x<item.x+t.width&&c.y>=item.y&&c.y<item.y+t.height})})})}
function applySignPreset(){mutateRules("Applied reusable panel rule to signs",()=>{state.rules.eventRules=state.rules.eventRules||{};state.rules.eventRules.sign={shape:"cutout",axis:"cross",height:.85,baseMetatile:1,faces:{top:{metatile:"self",layer:"base"},plane:{metatile:"self",layer:"foreground"}}};const signs=new Set(state.document.events.background.filter(e=>e.type==="sign").map(e=>`${e.x},${e.y}`));state.rules.overrides=state.rules.overrides.filter(item=>!signs.has(`${item.x},${item.y}`))})}

function payload(){return{symbol:state.document.map.symbol,revision:state.revision,rules:state.rules}}
async function validateRules(){if(!state.document)return;status("Validating candidate...");try{const result=await request("/api/validate",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload())});status(result.message)}catch(error){status(error.message,true)}}
async function regenerateRules(){status("Regenerating rule tables...");try{const result=await request("/api/regenerate",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"});status(result.message)}catch(error){status(error.message,true)}}
async function saveRules(){if(!state.document)return;status("Validating and regenerating...");try{const result=await request("/api/save",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload())});state.revision=result.revision;state.dirty=false;state.history=[];state.future=[];updateAll();status(result.message)}catch(error){status(error.message,true)}}

function updateAll(){if(!state.document)return;drawMap();updateInspector();renderPreview();$("#undo").disabled=!state.history.length;$("#redo").disabled=!state.future.length;$("#dirty-badge").textContent=state.dirty?"Unsaved":"Clean";$("#dirty-badge").classList.toggle("dirty",state.dirty);$("#rule-counts").textContent=`${state.rules.overrides.length} overrides · ${state.rules.buildings.length} structures`;updateWarnings()}
function updateWarnings(){const groups={};state.rules.overrides.forEach(({x,y,...rule})=>{const key=JSON.stringify(rule);groups[key]=(groups[key]||0)+1});const repeated=Object.values(groups).filter(count=>count>=4).length;$("#warning-count").textContent=repeated?`${repeated} repeated rule${repeated>1?"s":""}; consider a tileset rule`:"No warnings"}

// Minimal WebGL terrain preview. The game renderer remains authoritative.
const preview={gl:null,program:null,buffers:{},yaw:-.72,pitch:.76,distance:24,viewProjection:null,drag:null};
function initGL(){if(preview.gl)return true;const gl=$("#preview-canvas").getContext("webgl",{antialias:true,alpha:true});if(!gl){$("#webgl-error").hidden=false;return false}const vertex=`attribute vec3 p;attribute vec2 t;attribute float s;attribute float h;uniform mat4 m;varying vec2 uv;varying float shade;varying float hi;void main(){gl_Position=m*vec4(p,1.0);uv=t;shade=s;hi=h;}`;const fragment=`precision mediump float;uniform sampler2D tex;varying vec2 uv;varying float shade;varying float hi;void main(){vec4 c=texture2D(tex,uv);if(c.a<.1)discard;c.rgb*=shade;if(hi>.5)c.rgb=mix(c.rgb,vec3(.71,.87,.33),.55);gl_FragColor=c;}`;const shader=(type,source)=>{const item=gl.createShader(type);gl.shaderSource(item,source);gl.compileShader(item);if(!gl.getShaderParameter(item,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(item));return item};const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vertex));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);preview.gl=gl;preview.program=program;preview.buffers={vertex:gl.createBuffer(),index:gl.createBuffer()};return true}

function perspective(fov,aspect,near,far){const f=1/Math.tan(fov/2),nf=1/(near-far);return[f/aspect,0,0,0,0,f,0,0,0,0,(far+near)*nf,-1,0,0,2*far*near*nf,0]}
function lookAt(eye,target){let zx=eye[0]-target[0],zy=eye[1]-target[1],zz=eye[2]-target[2],l=Math.hypot(zx,zy,zz);zx/=l;zy/=l;zz/=l;let xx=-zz,xz=zx;l=Math.hypot(xx,xz);xx/=l;xz/=l;const yx=-zy*xz,yy=zx*xz-zz*xx,yz=zy*xx;return[xx,yx,zx,0,0,yy,zy,0,xz,yz,zz,0,-xx*eye[0]-xz*eye[2],-yx*eye[0]-yy*eye[1]-yz*eye[2],-zx*eye[0]-zy*eye[1]-zz*eye[2],1]}
function multiply(a,b){const o=new Array(16);for(let c=0;c<4;c++)for(let r=0;r<4;r++)o[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];return o}
function atlasUV(id,role){const local=id-(role==="secondary"?512:0),rows=Math.ceil(state.document.tilesets[role].count/16),u=(local%16)/16,v=Math.floor(local/16)/rows;return[u,v,1/16,1/rows]}
function previewMaterial(material,cell){if(material==="none"||material?.layer==="none")return null;const raw=material?.metatile??"self";return{id:raw==="self"?cell.metatile:Number(raw),layer:material?.layer||"full"}}
function addFace(batch,corners,material,shade,selected){if(material===null)return;const role=material.id>=512?"secondary":"primary",uv=atlasUV(material.id,role),base=batch[`${role}:${material.layer}`],start=base.vertices.length/7;[[0,0],[1,0],[1,1],[0,1]].forEach(([u,v],i)=>base.vertices.push(...corners[i],uv[0]+u*uv[2],uv[1]+v*uv[3],shade,selected?1:0));base.indices.push(start,start+1,start+2,start,start+2,start+3)}
function buildingAtCell(cell) {
  const placement=state.rules.buildings.find((item)=>{
    const template=state.document.editor.templates[item.template];
    return cell.x>=item.x&&cell.x<item.x+template.width&&cell.y>=item.y&&cell.y<item.y+template.height;
  });
  return placement?{placement,template:state.document.editor.templates[placement.template]}:null;
}

function roofFactor(position,span) {
  let factor=1-Math.abs(2*position/span-1);
  if(span%2)factor/=1-1/span;
  return Math.max(0,Math.min(1,factor));
}

function sameBuildingNeighbor(building,x,y) {
  if(!building||x<0||y<0||x>=state.document.map.width||y>=state.document.map.height)return false;
  const neighbor=state.document.cells[y*state.document.map.width+x];
  const effective=effectiveAt(neighbor);
  return effective.source.startsWith("building:")&&buildingAtCell(neighbor)?.placement===building.placement;
}

function previewGeometry() {
  const batch={};
  for(const role of ["primary","secondary"])
    for(const layer of ["full","base","foreground"])
      batch[`${role}:${layer}`]={vertices:[],indices:[]};

  state.document.cells.forEach((cell)=>{
    const effective=effectiveAt(cell),r=normalizeClientRule(effective.rule);
    if(r.shape==="hidden")return;
    const left=cell.x-state.document.map.width/2,right=left+1;
    const north=state.document.map.height/2-cell.y,south=north-1;
    const centerX=(left+right)/2,centerZ=(north+south)/2;
    const ground=Number(r.groundHeight)||0;
    const featureHeight=["extruded","cutout","roof","building-part"].includes(r.shape)?Math.max(Number(r.height)||.05,.05):.04;
    const selected=state.selection.has(cell.id),faces=r.faces;
    const building=effective.source.startsWith("building:")?buildingAtCell(cell):null;
    let topNW=ground+featureHeight,topNE=topNW,topSE=topNW,topSW=topNW;

    if(building&&r.shape==="roof") {
      const {placement,template}=building;
      const localX=cell.x-placement.x,localY=cell.y-placement.y;
      const body=template.bodyHeight,roof=template.roofHeight;
      if(template.profile==="gable-x") {
        topNW=topSW=body+roof*roofFactor(localX,template.width);
        topNE=topSE=body+roof*roofFactor(localX+1,template.width);
      } else if(template.profile==="gable-z") {
        topNW=topNE=body+roof*roofFactor(localY,template.roofRows);
        topSW=topSE=body+roof*roofFactor(localY+1,template.roofRows);
      } else {
        topNW=topNE=topSE=topSW=body+roof;
      }
    }

    if(r.shape==="cutout") {
      const top=ground+featureHeight,plane=previewMaterial(faces.plane,cell);
      if(r.axis==="x"||r.axis==="cross")addFace(batch,[[left,top,centerZ],[right,top,centerZ],[right,ground,centerZ],[left,ground,centerZ]],plane,1,selected);
      if(r.axis==="z"||r.axis==="cross")addFace(batch,[[centerX,top,south],[centerX,top,north],[centerX,ground,north],[centerX,ground,south]],plane,.88,selected);
      addFace(batch,[[left,ground,north],[right,ground,north],[right,ground,south],[left,ground,south]],previewMaterial(faces.top,cell),.75,selected);
      return;
    }

    addFace(batch,[[left,topNW,north],[right,topNE,north],[right,topSE,south],[left,topSW,south]],previewMaterial(faces.top,cell),1,selected);
    if(featureHeight<=.05)return;
    if(!sameBuildingNeighbor(building,cell.x,cell.y-1))addFace(batch,[[right,topNE,north],[left,topNW,north],[left,ground,north],[right,ground,north]],previewMaterial(faces.north,cell),.72,selected);
    if(!sameBuildingNeighbor(building,cell.x+1,cell.y))addFace(batch,[[right,topSE,south],[right,topNE,north],[right,ground,north],[right,ground,south]],previewMaterial(faces.east,cell),.84,selected);
    if(!sameBuildingNeighbor(building,cell.x,cell.y+1))addFace(batch,[[left,topSW,south],[right,topSE,south],[right,ground,south],[left,ground,south]],previewMaterial(faces.south,cell),.9,selected);
    if(!sameBuildingNeighbor(building,cell.x-1,cell.y))addFace(batch,[[left,topNW,north],[left,topSW,south],[left,ground,south],[left,ground,north]],previewMaterial(faces.west,cell),.78,selected);
  });
  return batch;
}
function glTexture(image,role){const gl=preview.gl;if(preview[`texture_${role}`]?.image===image)return preview[`texture_${role}`].texture;const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,0);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,image);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);preview[`texture_${role}`]={image,texture};return texture}
function renderPreview(){if(!state.document||!initGL())return;const canvas=$("#preview-canvas"),gl=preview.gl,dpr=Math.min(devicePixelRatio,2),width=Math.max(1,canvas.clientWidth*dpr|0),height=Math.max(1,canvas.clientHeight*dpr|0);if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height}gl.viewport(0,0,width,height);gl.clearColor(.055,.08,.065,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.enable(gl.CULL_FACE);gl.frontFace(gl.CW);gl.useProgram(preview.program);const center=[0,0,0],eye=[Math.cos(preview.yaw)*Math.cos(preview.pitch)*preview.distance,Math.sin(preview.pitch)*preview.distance,Math.sin(preview.yaw)*Math.cos(preview.pitch)*preview.distance],matrix=multiply(perspective(.72,width/height,.1,200),lookAt(eye,center));preview.viewProjection=matrix;gl.uniformMatrix4fv(gl.getUniformLocation(preview.program,"m"),false,new Float32Array(matrix));const batch=previewGeometry();for(const role of ["primary","secondary"])for(const layer of ["full","base","foreground"]){const data=batch[`${role}:${layer}`];if(!data.indices.length)continue;gl.bindBuffer(gl.ARRAY_BUFFER,preview.buffers.vertex);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(data.vertices),gl.DYNAMIC_DRAW);[["p",3,0],["t",2,12],["s",1,20],["h",1,24]].forEach(([name,size,offset])=>{const location=gl.getAttribLocation(preview.program,name);gl.enableVertexAttribArray(location);gl.vertexAttribPointer(location,size,gl.FLOAT,false,28,offset)});gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,preview.buffers.index);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array(data.indices),gl.DYNAMIC_DRAW);gl.bindTexture(gl.TEXTURE_2D,glTexture(state.layerAtlases[role][layer],`${role}_${layer}`));gl.drawElements(gl.TRIANGLES,data.indices.length,gl.UNSIGNED_SHORT,0)}}
function resetCamera(){if(!state.document)return;const camera=state.rules.camera||{profile:"exterior"},pitch=camera.pitch??(camera.profile==="interior"?60:40.542);preview.yaw=-Math.PI/2;preview.pitch=pitch*Math.PI/180;preview.distance=Math.max(state.document.map.width,state.document.map.height)*1.25;renderPreview()}
function bindPreviewPointer(){const canvas=$("#preview-canvas");canvas.addEventListener("pointerdown",e=>{preview.drag={x:e.clientX,y:e.clientY,moved:false};canvas.setPointerCapture(e.pointerId)});canvas.addEventListener("pointermove",e=>{if(!preview.drag)return;const dx=e.clientX-preview.drag.x,dy=e.clientY-preview.drag.y;if(Math.abs(dx)+Math.abs(dy)>2)preview.drag.moved=true;preview.yaw+=dx*.008;preview.pitch=Math.max(.2,Math.min(1.4,preview.pitch+dy*.006));preview.drag.x=e.clientX;preview.drag.y=e.clientY;renderPreview()});canvas.addEventListener("pointerup",e=>{if(preview.drag&&!preview.drag.moved)selectPreviewCell(e);preview.drag=null});canvas.addEventListener("wheel",e=>{e.preventDefault();preview.distance=Math.max(4,Math.min(80,preview.distance*Math.exp(e.deltaY*.001)));renderPreview()},{passive:false})}
function selectPreviewCell(event){if(!preview.viewProjection)return;const rect=$("#preview-canvas").getBoundingClientRect();let best=null,bestDistance=Infinity;state.document.cells.forEach(cell=>{const rule=normalizeClientRule(effectiveAt(cell).rule),point=[cell.x-state.document.map.width/2+.5,(Number(rule.groundHeight)||0)+(Number(rule.height)||0),state.document.map.height/2-cell.y-.5,1],m=preview.viewProjection,clip=[m[0]*point[0]+m[4]*point[1]+m[8]*point[2]+m[12],m[1]*point[0]+m[5]*point[1]+m[9]*point[2]+m[13],0,m[3]*point[0]+m[7]*point[1]+m[11]*point[2]+m[15]],sx=rect.left+(clip[0]/clip[3]+1)*rect.width/2,sy=rect.top+(1-clip[1]/clip[3])*rect.height/2,d=Math.hypot(event.clientX-sx,event.clientY-sy);if(d<bestDistance){bestDistance=d;best=cell.id}});if(best&&bestDistance<28)selectCells([best],event.ctrlKey||event.metaKey)}

initialize();
