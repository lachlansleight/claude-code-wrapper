// Sultana assembly viewer. Loads ../out/viewer/manifest.json (written by
// sultana_body.py) and steps through the assembly.

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import { toCreasedNormals } from "three/addons/utils/BufferGeometryUtils.js";

const DATA = "../out/viewer/";
const SHELLS = new Set(["bezel", "back_shell"]);
const MOVE_SECONDS = 0.9;

THREE.Object3D.DEFAULT_UP.set(0, 0, 1);

const $ = (id) => document.getElementById(id);
const stageEl = $("stage");

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.localClippingEnabled = true;
stageEl.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1d2025);
scene.add(new THREE.HemisphereLight(0xffffff, 0x3a3f47, 1.6));
const key = new THREE.DirectionalLight(0xffffff, 1.6);
key.position.set(-120, -160, 200);
scene.add(key);
const rim = new THREE.DirectionalLight(0xffffff, 0.7);
rim.position.set(160, 140, 60);
scene.add(rim);

const camera = new THREE.PerspectiveCamera(35, 1, 1, 5000);
camera.position.set(-170, -240, 120);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.target.set(0, 10, 0);

const clipPlane = new THREE.Plane(new THREE.Vector3(1, 0, 0), 0);
const clipping = [];

const state = {
  manifest: null,
  parts: new Map(),   // name -> { info, mesh, material, visibleByUser }
  step: -1,           // -1 = nothing placed yet
  progress: 1,        // 0..1 through the current step
  playing: false,
  explode: 0,
  ghost: false,
  showBought: true,
  showHardware: true,
  selected: null,
  isolated: null,
  autoframe: true,
};

// ---------------------------------------------------------------------------
// Loading

async function load() {
  let manifest;
  try {
    const res = await fetch(DATA + "manifest.json", { cache: "no-store" });
    if (!res.ok) throw new Error(res.status + " " + res.statusText);
    manifest = await res.json();
  } catch (err) {
    showError(
      `Couldn't load <code>out/viewer/manifest.json</code> (${err.message}).<br><br>` +
      `Export the model and serve this folder:<br><code>cd cad && .venv/bin/python sultana_body.py --serve</code><br>` +
      `then open <code>http://127.0.0.1:8765/viewer/</code>. Opening the HTML file directly won't work.`);
    return;
  }
  state.manifest = manifest;
  const loader = new STLLoader();
  await Promise.all(manifest.parts.map(async (info) => {
    const geo = await loader.loadAsync(DATA + info.file);
    const creased = toCreasedNormals(geo, THREE.MathUtils.degToRad(35));
    const hardware = info.group === "hardware";
    const material = new THREE.MeshStandardMaterial({
      color: info.colour,
      roughness: hardware ? 0.35 : 0.65,
      metalness: hardware ? 0.6 : 0.05,
      side: THREE.DoubleSide,
      clippingPlanes: clipping,
    });
    const mesh = new THREE.Mesh(creased, material);
    mesh.name = info.name;
    mesh.userData.name = info.name;
    scene.add(mesh);
    state.parts.set(info.name, { info, mesh, material, visibleByUser: true });
  }));

  addFloor();
  buildPanel();
  goTo(manifest.steps.length - 1, 1, false);
  frame(true);
}

function addFloor() {
  const box = new THREE.Box3();
  for (const p of state.parts.values()) box.expandByObject(p.mesh);
  const grid = new THREE.GridHelper(400, 40, 0x3a4049, 0x2b3037);
  grid.rotation.x = Math.PI / 2;  // GridHelper lies in XZ; we're Z-up
  grid.position.set(0, 40, box.min.z - 0.01);
  scene.add(grid);
}

// ---------------------------------------------------------------------------
// Step state

function ease(t) {
  return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
}

// Where each part is at the current step and progress: { off, rot, axis }, the
// offset from assembled and a turn (degrees) about an axis through the origin.
// Parts not placed yet are missing.
function poses() {
  const out = new Map();
  const steps = state.manifest.steps;
  for (let i = 0; i <= state.step && i < steps.length; i++) {
    const moves = steps[i].moves;
    const n = moves.length;
    moves.forEach((mv, j) => {
      let t;
      if (i < state.step) t = 1;
      else {
        const start = j / n;
        if (state.progress < start) return;
        t = ease(Math.min(1, (state.progress - start) * n));
      }
      const off = mv.from.map((f, k) => f + (mv.to[k] - f) * t);
      const rot = mv.axis ? mv.rot_from + (mv.rot_to - mv.rot_from) * t : 0;
      for (const name of mv.parts) out.set(name, { off, rot, axis: mv.axis || null });
    });
  }
  return out;
}

function movingNow() {
  const s = state.manifest.steps[state.step];
  return new Set(s ? s.moves.flatMap((m) => m.parts) : []);
}

function apply() {
  if (!state.manifest) return;
  const where = poses();
  const current = movingNow();
  const ex = state.manifest.explode;
  for (const [name, p] of state.parts) {
    const pose = where.get(name);
    const group = p.info.group;
    let visible = pose !== undefined && p.visibleByUser;
    if (group === "bought" && !state.showBought) visible = false;
    if (group === "hardware" && !state.showHardware) visible = false;
    if (state.isolated && name !== state.isolated) visible = false;
    p.mesh.visible = visible;
    if (pose !== undefined) {
      const { off } = pose;
      const e = ex[name] || [0, 0, 0];
      p.mesh.position.set(off[0] + e[0] * state.explode * 3, off[1] + e[1] * state.explode * 3, off[2] + e[2] * state.explode * 3);
      if (pose.axis) p.mesh.quaternion.setFromAxisAngle(new THREE.Vector3(...pose.axis).normalize(), THREE.MathUtils.degToRad(pose.rot));
      else p.mesh.quaternion.identity();
    }
    const ghost = state.ghost && SHELLS.has(name);
    p.material.transparent = ghost;
    p.material.opacity = ghost ? 0.18 : 1;
    p.material.depthWrite = !ghost;
    const highlight = name === state.selected ? 0.35 : (current.has(name) && state.step >= 0 && state.progress < 1 ? 0.18 : 0);
    p.material.emissive.set(p.info.colour);
    p.material.emissiveIntensity = highlight;
  }
  updatePanel();
}

function goTo(step, progress = 1, animate = true) {
  const n = state.manifest.steps.length;
  state.step = Math.max(-1, Math.min(n - 1, step));
  state.progress = animate ? 0 : progress;
  state.playing = animate && state.step >= 0;
  apply();
  if (state.autoframe) frame(false);
}

// ---------------------------------------------------------------------------
// Camera

let frameTween = null;

function frame(instant, viewDir = null) {
  const box = new THREE.Box3();
  const where = poses();
  const ex = state.manifest.explode;
  const focus = !instant && state.manifest.steps[state.step]?.focus === "step" ? movingNow() : null;
  for (const [name, p] of state.parts) {
    if (!p.mesh.visible || (focus && !focus.has(name))) continue;
    // frame the end of the current step, not the middle of the animation
    const off = where.get(name)?.off || [0, 0, 0];
    const step = state.manifest.steps[state.step];
    let end = off;
    if (step) for (const mv of step.moves) if (mv.parts.includes(name)) end = mv.to;
    const e = ex[name] || [0, 0, 0];
    const geoBox = p.mesh.geometry.boundingBox || (p.mesh.geometry.computeBoundingBox(), p.mesh.geometry.boundingBox);
    const b = geoBox.clone().translate(new THREE.Vector3(
      end[0] + e[0] * state.explode * 3, end[1] + e[1] * state.explode * 3, end[2] + e[2] * state.explode * 3));
    box.union(b);
  }
  if (box.isEmpty()) return;
  if (clipping.length) {
    // only frame what the section leaves
    const n = clipPlane.normal;
    const pos = -clipPlane.constant * (n.x + n.y + n.z);
    const axis = n.x ? "x" : n.y ? "y" : "z";
    if (n.x + n.y + n.z < 0) box.max[axis] = Math.min(box.max[axis], pos);
    else box.min[axis] = Math.max(box.min[axis], pos);
  }
  const centre = box.getCenter(new THREE.Vector3());
  const radius = box.getSize(new THREE.Vector3()).length() / 2;
  const dist = radius / Math.sin(THREE.MathUtils.degToRad(camera.fov / 2)) * 1.05;
  const hint = !instant && !viewDir && state.manifest.steps[state.step]?.view;
  const dir = viewDir ? viewDir.clone().normalize()
    : hint ? new THREE.Vector3(...hint).normalize()
    : camera.position.clone().sub(controls.target).normalize();
  const toPos = centre.clone().add(dir.multiplyScalar(Math.max(dist, 90)));
  if (instant) {
    controls.target.copy(centre);
    camera.position.copy(toPos);
    frameTween = null;
  } else {
    frameTween = { t: 0, fromT: controls.target.clone(), toT: centre, fromP: camera.position.clone(), toP: toPos };
  }
}

// ---------------------------------------------------------------------------
// Panel

function buildPanel() {
  const m = state.manifest;
  const s = m.report.summary;
  $("meta").textContent = `Body ${s.sphere_d} mm (${s.overall_mm.width} × ${s.overall_mm.depth} × ${s.overall_mm.height}) · exported ${m.generated}`;
  const chips = [];
  const chip = (label, ok) => chips.push(`<span class="chip ${ok ? "ok" : "bad"}">${label}</span>`);
  const r = m.report;
  if ("interference_mm3" in r) {
    chip(r.interference_mm3 === "none" ? "no overlaps" : `${r.interference_mm3.length} overlaps`, r.interference_mm3 === "none");
    chip(r.arm_travel === "clear" ? "arm travel clear" : "arm travel hits", r.arm_travel === "clear");
    chip(r.back_shell_slide_on === "clear" ? "back shell slides on" : "back shell blocked", r.back_shell_slide_on === "clear");
    if ("display_module_in" in r) {
      chip(r.display_module_in === "clear" ? "display goes in" : "display blocked", r.display_module_in === "clear");
    }
  } else {
    chips.push(`<span class="chip">checks skipped (--quick)</span>`);
  }
  const t = r.torque.both;
  chip(`torque margin ${t.margin_vs_datasheet_min}× min / ${t.margin_vs_datasheet_typ}× typ`, t.margin_vs_datasheet_min >= 1.2);
  $("checks").innerHTML = chips.join("");

  $("steps").innerHTML = m.steps.map((st, i) => `<li data-i="${i}">${st.title}</li>`).join("");
  $("steps").onclick = (e) => {
    const li = e.target.closest("li");
    if (li) goTo(Number(li.dataset.i), 1, true);
  };

  const groups = { printed: "Printed", bought: "Bought (placeholders)", hardware: "Screws, magnets, bearing" };
  $("parts").innerHTML = Object.entries(groups).map(([g, title]) => {
    const rows = m.parts.filter((p) => p.group === g).map((p) => `
      <div class="part" data-name="${p.name}">
        <input type="checkbox" checked data-name="${p.name}">
        <span class="swatch" style="background:${p.colour}"></span>
        <span class="name" title="${p.name}">${p.label}</span>
      </div>`).join("");
    return rows ? `<div class="group"><div>${title}</div>${rows}</div>` : "";
  }).join("");
  $("parts").onchange = (e) => {
    const p = state.parts.get(e.target.dataset.name);
    if (p) { p.visibleByUser = e.target.checked; apply(); }
  };
  $("parts").onclick = (e) => {
    const el = e.target.closest(".name");
    if (el) select(el.parentElement.dataset.name);
  };
  $("unplaced").textContent = m.unplaced.length ? `Not in any step: ${m.unplaced.join(", ")}` : "";
}

function updatePanel() {
  const m = state.manifest;
  document.querySelectorAll("#steps li").forEach((li) => {
    const i = Number(li.dataset.i);
    li.className = i === state.step ? "current" : i < state.step ? "done" : "";
  });
  const st = m.steps[state.step];
  $("steptext").innerHTML = st
    ? `<b>${state.step + 1}. ${st.title}</b>${st.text}`
    : `<b>Start</b>Nothing placed yet. Step through with ▶ or the arrow keys.`;
  const where = poses();
  document.querySelectorAll("#parts .part").forEach((row) => {
    const name = row.dataset.name;
    row.classList.toggle("selected", name === state.selected);
    row.classList.toggle("unseen", !where.has(name));
  });
  $("isolate").disabled = !state.selected;
  const sel = state.selected && state.parts.get(state.selected);
  const box = $("selection");
  if (sel) {
    const step = m.steps.findIndex((s) => s.moves.some((mv) => mv.parts.includes(sel.info.name)));
    box.style.display = "block";
    box.innerHTML = `<b>${sel.info.label}</b><br><span class="muted">${sel.info.name} · ${sel.info.group}` +
      (step >= 0 ? ` · goes in at step ${step + 1}` : "") + `</span>`;
  } else {
    box.style.display = "none";
  }
}

function select(name) {
  state.selected = state.selected === name ? null : name;
  apply();
}

function showError(html) {
  const el = $("error");
  el.innerHTML = html;
  el.style.display = "block";
  $("meta").textContent = "Not loaded";
}

// ---------------------------------------------------------------------------
// Controls

$("first").onclick = () => goTo(-1, 1, false);
$("prev").onclick = () => goTo(state.step - 1, 1, false);
$("next").onclick = () => goTo(state.step + 1, 0, true);
$("last").onclick = () => goTo(state.manifest.steps.length - 1, 1, false);
$("play").onclick = () => goTo(Math.max(state.step, 0), 0, true);
$("explode").oninput = (e) => { state.explode = Number(e.target.value); apply(); };
$("ghost").onchange = (e) => { state.ghost = e.target.checked; apply(); };
$("showBought").onchange = (e) => { state.showBought = e.target.checked; apply(); };
$("showHardware").onchange = (e) => { state.showHardware = e.target.checked; apply(); };
$("autoframe").onchange = (e) => { state.autoframe = e.target.checked; };
$("frame").onclick = () => frame(false);
$("isolate").onclick = () => { state.isolated = state.isolated ? null : state.selected; apply(); frame(false); };
$("showAll").onclick = () => {
  state.isolated = null;
  for (const p of state.parts.values()) p.visibleByUser = true;
  document.querySelectorAll("#parts input").forEach((i) => { i.checked = true; });
  apply();
};

function updateClip(turnCamera) {
  const axis = $("clipAxis").value;
  const pos = Number($("clipPos").value);
  $("clipPos").disabled = !axis;
  clipping.length = 0;
  if (axis) {
    // three.js drops fragments where n·p + c < 0; keep the side below `pos` unless flipped
    const n = new THREE.Vector3(axis === "x" ? 1 : 0, axis === "y" ? 1 : 0, axis === "z" ? 1 : 0);
    const s = $("clipFlip").checked ? 1 : -1;
    clipPlane.set(n.multiplyScalar(s), -s * pos);
    clipping.push(clipPlane);
  }
  for (const p of state.parts.values()) p.material.needsUpdate = true;
  if (turnCamera && axis) {
    // look at the cut face from the side that was taken away, a little from above
    const look = clipPlane.normal.clone().negate();
    look.add(axis === "z" ? new THREE.Vector3(0, -0.3, 0) : new THREE.Vector3(0, axis === "x" ? -0.25 : 0, 0.3));
    frame(false, look);
  }
}
$("clipAxis").onchange = () => updateClip(true);
$("clipPos").oninput = () => updateClip(false);
$("clipFlip").onchange = () => updateClip(true);

window.addEventListener("keydown", (e) => {
  if (!state.manifest || e.target.tagName === "INPUT" || e.target.tagName === "SELECT") return;
  if (e.key === "ArrowRight") goTo(state.step + 1, 0, true);
  else if (e.key === "ArrowLeft") goTo(state.step - 1, 1, false);
  else if (e.key === " ") { e.preventDefault(); goTo(Math.max(state.step, 0), 0, true); }
  else if (e.key === "Home") goTo(-1, 1, false);
  else if (e.key === "End") goTo(state.manifest.steps.length - 1, 1, false);
  else if (e.key === "Escape") select(state.selected);
});

// hover and click picking
const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
let downAt = null;

function pick(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.set(((event.clientX - rect.left) / rect.width) * 2 - 1, -((event.clientY - rect.top) / rect.height) * 2 + 1);
  raycaster.setFromCamera(pointer, camera);
  const meshes = [...state.parts.values()].map((p) => p.mesh).filter((m) => m.visible);
  const hits = raycaster.intersectObjects(meshes, false).filter((h) => {
    if (!clipping.length) return true;
    return clipPlane.distanceToPoint(h.point) >= 0;
  });
  // skip see-through shells so you can point at what's inside
  const hit = hits.find((h) => !(state.ghost && SHELLS.has(h.object.name)));
  return hit ? hit.object.name : null;
}

renderer.domElement.addEventListener("pointermove", (e) => {
  if (!state.manifest) return;
  const name = pick(e);
  const tip = $("tooltip");
  if (name) {
    const rect = stageEl.getBoundingClientRect();
    tip.style.display = "block";
    tip.style.left = `${e.clientX - rect.left + 14}px`;
    tip.style.top = `${e.clientY - rect.top + 10}px`;
    tip.textContent = state.parts.get(name).info.label;
  } else {
    tip.style.display = "none";
  }
});
renderer.domElement.addEventListener("pointerdown", (e) => { downAt = [e.clientX, e.clientY]; });
renderer.domElement.addEventListener("pointerup", (e) => {
  if (!downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 4) return;
  const name = pick(e);
  if (name) select(name);
  else if (state.selected) select(state.selected);
});

// ---------------------------------------------------------------------------
// Loop

function resize() {
  const w = stageEl.clientWidth;
  const h = stageEl.clientHeight;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
window.addEventListener("resize", resize);
resize();

const clock = new THREE.Clock();
renderer.setAnimationLoop(() => {
  const dt = clock.getDelta();
  if (state.playing && state.manifest) {
    const n = Math.max(1, state.manifest.steps[state.step].moves.length);
    state.progress = Math.min(1, state.progress + dt / (MOVE_SECONDS * n));
    if (state.progress >= 1) state.playing = false;
    apply();
  }
  if (frameTween) {
    frameTween.t = Math.min(1, frameTween.t + dt / 0.6);
    const k = ease(frameTween.t);
    controls.target.lerpVectors(frameTween.fromT, frameTween.toT, k);
    camera.position.lerpVectors(frameTween.fromP, frameTween.toP, k);
    if (frameTween.t >= 1) frameTween = null;
  }
  controls.update();
  renderer.render(scene, camera);
});

window.sultanaViewer = { state, camera, controls, clipPlane };  // for poking at from the console
load();
