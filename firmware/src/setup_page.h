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
  button { width:100%; padding:12px; margin-top:10px; border:0; border-radius:10px;
           background:var(--accent); color:#111; font-size:17px; font-weight:600; }
  button.secondary { background:#2a2a36; color:var(--fg); }
  input { width:100%; padding:10px; margin-top:6px; border-radius:8px; border:1px solid #333;
          background:#0f0f16; color:var(--fg); font-size:16px; }
  .two { display:flex; gap:8px; }
  .msg { color:var(--accent); min-height:1.4em; margin-top:8px; }
  .num { font-variant-numeric: tabular-nums; }
</style></head>
<body><main>
<h1>Planetary hour clock</h1>

<section>
  <h2>Clock now</h2>
  <div class="row"><span>Time</span><span id="clockTime" class="num">…</span></div>
  <div class="row"><span>Phone</span><span id="phoneTime" class="num">…</span></div>
  <div class="row"><span>Time zone</span><span id="tzName">…</span></div>
  <div class="row"><span>Place</span><span id="place">…</span></div>
  <div class="row"><span>Lat, lon</span><span id="latlon" class="num">…</span></div>
</section>

<section>
  <h2>Time</h2>
  <div>Sets the clock's time and time zone from this phone.</div>
  <button id="setTime">Set time from this phone</button>
  <div class="msg" id="timeMsg"></div>
</section>

<section>
  <h2>Location</h2>
  <div class="two">
    <input id="lat" inputmode="decimal" placeholder="Latitude (N +)">
    <input id="lon" inputmode="decimal" placeholder="Longitude (E +)">
  </div>
  <input id="placeName" placeholder="Place name (optional)">
  <button id="useGps" class="secondary">Use this phone's location</button>
  <button id="setLoc">Save location</button>
  <div class="msg" id="locMsg"></div>
</section>

<button id="done" class="secondary">Done</button>

<script>
const $ = id => document.getElementById(id);
let clockOffsetMs = 0;  // clock time minus phone time

function fmt(ms, tz) {
  try { return new Date(ms).toLocaleString([], {timeZone: tz || undefined, hour12:false}); }
  catch (e) { return new Date(ms).toLocaleString(); }
}
function phoneZone() { return Intl.DateTimeFormat().resolvedOptions().timeZone || ''; }
function phoneOffsetMin() { return -new Date().getTimezoneOffset(); }

async function refresh() {
  const t0 = Date.now();
  const s = await (await fetch('/api/state')).json();
  clockOffsetMs = s.time * 1000 - (t0 + Date.now()) / 2;
  window.state = s;
  $('tzName').textContent = s.tzName;
  $('place').textContent = s.place || '—';
  $('latlon').textContent = s.lat.toFixed(4) + ', ' + s.lon.toFixed(4);
  if (!$('lat').value) $('lat').value = s.lat;
  if (!$('lon').value) $('lon').value = s.lon;
  if (!$('placeName').value) $('placeName').value = s.place || '';
}
function tick() {
  const now = Date.now();
  if (window.state) $('clockTime').textContent = fmt(now + clockOffsetMs, window.state.tzName);
  $('phoneTime').textContent = fmt(now) + ' (' + phoneZone() + ')';
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
    $('timeMsg').textContent = 'Done: clock set to ' + phoneZone() + '.';
  } catch (e) { $('timeMsg').textContent = 'Failed: ' + e.message; }
};

$('useGps').onclick = () => {
  if (!navigator.geolocation || !window.isSecureContext) {
    $('locMsg').textContent = 'This page cannot read GPS (browsers only allow it on secure pages). ' +
      'Enter coordinates, or use the online setup page.';
    return;
  }
  $('locMsg').textContent = 'Getting location…';
  navigator.geolocation.getCurrentPosition(p => {
    $('lat').value = p.coords.latitude.toFixed(4);
    $('lon').value = p.coords.longitude.toFixed(4);
    $('locMsg').textContent = 'Got it. Tap Save location.';
  }, e => { $('locMsg').textContent = 'Location failed: ' + e.message; });
};

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
refresh().catch(e => { $('tzName').textContent = 'cannot reach clock'; });
setInterval(tick, 250);
</script>
</main></body></html>
)HTML";
