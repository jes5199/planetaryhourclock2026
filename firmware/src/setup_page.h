// The setup page served from the clock's hotspot. Plain HTML/JS, no external resources
// (the phone has no internet while it is joined to the clock).
#pragma once

static const char kSetupPage[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Planetary hour clock</title>
<style>
  :root { color-scheme: dark; --fg:#eee; --dim:#999; --bg:#0a0a0f; --card:#16161f; --accent:#f0b84b; }
  * { box-sizing: border-box; }
  body { margin:0; padding:16px; background:var(--bg); color:var(--fg);
         font:16px/1.4 -apple-system, system-ui, sans-serif; }
  main { max-width:480px; margin:0 auto; }
  h1 { font-size:22px; margin:4px 0 16px; }
  section { background:var(--card); border-radius:12px; padding:14px 16px; margin-bottom:14px; }
  h2 { font-size:15px; color:var(--dim); font-weight:600; margin:0 0 8px; text-transform:uppercase; letter-spacing:.04em; }
  .row { display:flex; justify-content:space-between; gap:12px; margin:4px 0; }
  .row span:first-child { color:var(--dim); }
  .row span:last-child { text-align:right; }
  button { width:100%; padding:12px; margin-top:10px; border:0; border-radius:10px;
           background:var(--accent); color:#111; font-size:17px; font-weight:600; }
  button.secondary { background:#2a2a36; color:var(--fg); }
  button.city { background:#20202b; color:var(--fg); text-align:left; font-weight:400; margin-top:6px; }
  button.city small { color:var(--dim); display:block; font-size:14px; }
  input { width:100%; padding:12px; margin-top:6px; border-radius:8px; border:1px solid #333;
          background:#0f0f16; color:var(--fg); font-size:17px; }
  .two { display:flex; gap:8px; }
  .msg { color:var(--accent); min-height:1.4em; margin-top:8px; }
  .num { font-variant-numeric: tabular-nums; }
  footer { color:#666; font-size:13px; margin:18px 0 8px; text-align:center; }
  footer a { color:#888; }
</style></head>
<body><main>
<h1>Planetary hour clock</h1>

<section>
  <h2>Clock</h2>
  <div class="row"><span>Time</span><span id="clockTime" class="num">…</span></div>
  <div class="row"><span>Time zone</span><span id="tzName">…</span></div>
  <div class="row"><span>Place</span><span id="place">…</span></div>
  <div class="row"><span>Lat, lon</span><span id="latlon" class="num">…</span></div>
  <button id="setTime">Set time from this phone</button>
  <div class="msg" id="timeMsg"></div>
</section>

<section>
  <h2>Location</h2>
  <div>Choose the nearest city. Sunrise and sunset are worked out from it.</div>
  <input id="q" type="search" autocomplete="off" autocapitalize="off" placeholder="Search for a city">
  <div id="results"></div>
  <div class="msg" id="locMsg"></div>
  <h2 style="margin-top:16px">Or coordinates</h2>
  <div class="two">
      <input id="lat" inputmode="decimal" placeholder="Latitude (N +)">
      <input id="lon" inputmode="decimal" placeholder="Longitude (E +)">
    </div>
    <input id="placeName" placeholder="Place name (optional)">
    <button id="setLoc" class="secondary">Save coordinates</button>
</section>

<button id="done" class="secondary">Done</button>
<footer>City data © <a href="https://www.geonames.org">GeoNames</a>, CC BY 4.0</footer>

<script>
const $ = id => document.getElementById(id);
let clockOffsetMs = 0;  // clock time minus phone time
let state = null;

function phoneZone() { return Intl.DateTimeFormat().resolvedOptions().timeZone || ''; }
function phoneOffsetMin() { return -new Date().getTimezoneOffset(); }
function fold(s) { return s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase().trim(); }
function placeOf(c) { return c.r ? c.n + ', ' + c.r : c.n + ', ' + c.c; }

async function refresh() {
  const t0 = Date.now();
  state = await (await fetch('/api/state')).json();
  clockOffsetMs = state.time * 1000 - (t0 + Date.now()) / 2;
  $('tzName').textContent = state.tzName;
  $('place').textContent = state.place || '—';
  $('latlon').textContent = state.lat.toFixed(4) + ', ' + state.lon.toFixed(4);
  // Keep the coordinate boxes showing the clock's location, unless someone is typing in them.
  const editing = ['lat', 'lon', 'placeName'].includes(document.activeElement && document.activeElement.id);
  if (!editing) {
    $('lat').value = state.lat.toFixed(4);
    $('lon').value = state.lon.toFixed(4);
    $('placeName').value = state.place || '';
  }
}
function tick() {
  if (!state) return;
  try {
    $('clockTime').textContent = new Date(Date.now() + clockOffsetMs)
      .toLocaleTimeString([], {timeZone: state.tzName, hour12: false});
  } catch (e) {
    $('clockTime').textContent = new Date(Date.now() + clockOffsetMs).toLocaleTimeString();
  }
}

async function send(params) {
  const r = await fetch('/set?' + new URLSearchParams(params));
  if (!r.ok) throw new Error(await r.text());
  await refresh();
}

$('setTime').onclick = async () => {
  $('timeMsg').textContent = 'Setting…';
  try {
    await send({ time: Date.now(), tz: phoneZone(), off: phoneOffsetMin() });
    $('timeMsg').textContent = 'Set to this phone’s time (' + phoneZone() + ').';
  } catch (e) { $('timeMsg').textContent = 'Failed: ' + e.message; }
};

function showCities(list, note) {
  const box = $('results');
  box.innerHTML = '';
  if (note) { const d = document.createElement('div'); d.className = 'msg'; d.textContent = note; box.append(d); }
  for (const c of list) {
    const b = document.createElement('button');
    b.className = 'city';
    b.textContent = c.n;
    const s = document.createElement('small');
    s.textContent = [c.r, c.c].filter(Boolean).join(', ');
    b.append(s);
    b.onclick = () => chooseCity(c);
    box.append(b);
  }
}

async function chooseCity(c) {
  $('locMsg').textContent = 'Saving…';
  const params = { lat: c.lat, lon: c.lon, place: placeOf(c) };
  if (c.tz) params.tz = c.tz;
  try {
    await send(params);
    $('locMsg').textContent = 'Location set to ' + placeOf(c) + '.';
    $('q').value = '';
    showCities([]);
  } catch (e) { $('locMsg').textContent = 'Failed: ' + e.message; }
}

let searchTimer = 0, searchSeq = 0;
$('q').oninput = () => {
  clearTimeout(searchTimer);
  searchTimer = setTimeout(async () => {
    const q = fold($('q').value);
    if (!q) return suggest();
    const seq = ++searchSeq;
    const list = await (await fetch('/api/cities?' + new URLSearchParams({ q, tz: phoneZone() }))).json();
    if (seq === searchSeq) showCities(list, list.length ? '' : 'No city found. Try a nearby larger one.');
  }, 200);
};

async function suggest() {
  const list = await (await fetch('/api/cities?' + new URLSearchParams({ tz: phoneZone() }))).json();
  showCities(list, list.length ? 'Suggested from your time zone:' : '');
}

$('setLoc').onclick = async () => {
  const lat = parseFloat($('lat').value), lon = parseFloat($('lon').value);
  if (!(Math.abs(lat) <= 90) || !(Math.abs(lon) <= 180)) {
    $('locMsg').textContent = 'Latitude must be -90..90 and longitude -180..180.';
    return;
  }
  try {
    await send({ lat, lon, place: $('placeName').value.trim() });
    $('locMsg').textContent = 'Saved.';
  } catch (e) { $('locMsg').textContent = 'Failed: ' + e.message; }
};

$('done').onclick = async () => {
  await fetch('/done');
  document.body.innerHTML = '<main><h1>All set</h1><p>The clock is back to showing the time. ' +
    'You can leave this Wi-Fi network.</p></main>';
};

if (new URLSearchParams(location.search).has('saved')) $('locMsg').textContent = 'Saved.';
refresh().then(suggest).catch(e => { $('tzName').textContent = 'cannot reach clock'; });
setInterval(tick, 250);
</script>
</main></body></html>
)HTML";
