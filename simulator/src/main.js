import {
  backHit,
  dummyNotes,
  handleTap,
  menuFor,
  menuHit,
  meta,
  parentOf,
} from "./ui_model.js";

const canvas = document.querySelector("#display");
const ctx = canvas.getContext("2d", { alpha: false });
const stateName = document.querySelector("#stateName");
const history = document.querySelector("#history");
const resetButton = document.querySelector("#resetButton");
const backButton = document.querySelector("#backButton");

const FONT = "'Audiowide', ui-monospace, SFMono-Regular, Menlo, monospace";
const ICON_FONT = "'Material Symbols Rounded'";

let state = "static";
let darkMode = false;
let volume = 5;
let syncPending = 3;
let syncTransferred = 0;
let batteryPercent = 84;
let recording = "idle";
let selectedNote = dummyNotes[0];
let listening = false;
let listenProgress = 0;
let alarmOn = false;
let alarmMinutes = 450;
let stopwatchRunning = false;
let stopwatchSeconds = 0;
let timerRunning = false;
let timerSeconds = 300;
let intervalRunning = false;
let intervalSeconds = 1500;
let intervalRound = 1;
let dirtyRegion = "full refresh";
const visited = ["static"];

function colors() {
  return darkMode
    ? { paper: "#111111", ink: "#f8f7ef", muted: "#bfbfb8" }
    : { paper: "#f8f7ef", ink: "#111111", muted: "#5e5e58" };
}

function setInk() {
  const palette = colors();
  ctx.fillStyle = palette.ink;
  ctx.strokeStyle = palette.ink;
}

function clear() {
  const palette = colors();
  ctx.fillStyle = palette.paper;
  ctx.fillRect(0, 0, 200, 200);
  setInk();
  ctx.lineWidth = 2;
  ctx.textBaseline = "top";
}

function font(size) {
  ctx.font = `${size}px ${FONT}`;
}

function iconFont(size) {
  ctx.font = `${size}px ${ICON_FONT}`;
}

function textCenter(text, y, size = 16) {
  font(size);
  ctx.textBaseline = "top";
  ctx.textAlign = "center";
  ctx.fillText(text, 100, y);
}

function textCenterAt(text, x, y, size = 12) {
  font(size);
  ctx.textBaseline = "middle";
  ctx.textAlign = "center";
  ctx.fillText(text, x, y);
}

function textLeftCenter(text, x, y, size = 14) {
  font(size);
  ctx.textBaseline = "middle";
  ctx.textAlign = "left";
  ctx.fillText(text, x, y);
}

function icon(name, x, y, size = 22) {
  iconFont(size);
  ctx.textBaseline = "alphabetic";
  ctx.textAlign = "center";
  const metrics = ctx.measureText(name);
  const ascent = metrics.actualBoundingBoxAscent || size * 0.75;
  const descent = metrics.actualBoundingBoxDescent || size * 0.25;
  ctx.fillText(name, x, y + (ascent - descent) / 2);
}

function backTriangle() {
  ctx.beginPath();
  ctx.moveTo(8, 16);
  ctx.lineTo(27, 5);
  ctx.lineTo(27, 27);
  ctx.closePath();
  ctx.fill();
}

function roundedRect(x, y, w, h, r = 8, fill = false) {
  ctx.beginPath();
  ctx.roundRect(x, y, w, h, r);
  fill ? ctx.fill() : ctx.stroke();
}

function fillCircle(x, y, r) {
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fill();
}

function line(x1, y1, x2, y2) {
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();
}

function drawDefaultStaticArt() {
  const palette = colors();
  ctx.fillStyle = palette.paper;
  ctx.fillRect(0, 0, 200, 200);
  ctx.strokeStyle = palette.ink;
  ctx.fillStyle = palette.ink;
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.arc(100, 100, 68, 0, Math.PI * 2);
  ctx.stroke();
  fillCircle(74, 82, 8);
  fillCircle(126, 82, 8);
  ctx.beginPath();
  ctx.arc(100, 110, 35, 0.18 * Math.PI, 0.82 * Math.PI);
  ctx.stroke();
}

function drawStaticArtRows(rows) {
  const palette = colors();
  ctx.fillStyle = palette.paper;
  ctx.fillRect(0, 0, 200, 200);
  ctx.fillStyle = palette.ink;
  rows.forEach((row, y) => {
    for (let x = 0; x < row.length; x += 1) {
      if (row[x] === "1" || row[x] === "#") {
        ctx.fillRect(x, y, 1, 1);
      }
    }
  });
}

function loadStaticArtRows() {
  try {
    const raw = localStorage.getItem("pocketJournalStaticArt");
    if (!raw) {
      return null;
    }
    const parsed = JSON.parse(raw);
    if (parsed.width !== 200 || parsed.height !== 200 || parsed.encoding !== "rows") {
      return null;
    }
    if (!Array.isArray(parsed.rows) || parsed.rows.length !== 200) {
      return null;
    }
    if (!parsed.rows.every((row) => typeof row === "string" && row.length === 200)) {
      return null;
    }
    return parsed.rows;
  } catch {
    return null;
  }
}

function drawMenu(items) {
  const top = 42;
  const rowHeight = Math.floor((200 - top - 8) / items.length);
  items.forEach((item, index) => {
    const y = top + index * rowHeight;
    const height = rowHeight - 4;
    roundedRect(12, y, 176, height, 8);
    icon(item.icon, 38, y + height / 2, 22);
    textLeftCenter(item.label.toUpperCase(), 64, y + height / 2 + 1, 13);
  });
}

function drawSettings() {
  const rows = [
    { y: 42, h: 38, label: "SYNC", icon: "sync" },
    { y: 86, h: 48, label: "VOLUME", icon: "volume_up" },
    { y: 140, h: 38, label: "DARK", icon: "dark_mode" },
  ];
  rows.forEach((row) => roundedRect(12, row.y, 176, row.h, 8));
  icon(rows[0].icon, 38, 61, 22);
  textLeftCenter(rows[0].label, 64, 62, 13);

  icon("volume_down", 38, 110, 22);
  icon("volume_up", 162, 110, 22);
  roundedRect(62, 103, 76, 16, 6);
  const width = Math.floor((volume / 10) * 68);
  if (width > 0) {
    roundedRect(66, 107, width, 8, 4, true);
  }
  textCenterAt(String(volume), 100, 127, 9);

  icon(rows[2].icon, 38, 159, 22);
  textLeftCenter(rows[2].label, 64, 160, 13);
}

function drawTimeTemp() {
  backTriangle();
  textCenter("SAT 06/06", 46, 18);
  textCenter("09:41", 76, 18);
  textCenter("22C / 72F", 106, 18);
  textCenter(`${batteryPercent}% BAT`, 136, 18);
}

function drawSync() {
  backTriangle();
  icon("sync", 100, 55, 32);
  textCenter("SYNC", 75, 17);
  roundedRect(22, 105, 156, 22, 7);
  const total = Math.max(syncPending + syncTransferred, 1);
  const width = Math.floor((syncTransferred / total) * 148);
  if (width > 0) {
    roundedRect(26, 109, width, 14, 5, true);
  }
  textCenter(`${syncPending} PENDING`, 137, 13);
  textCenter(`${syncTransferred} TRANSFERRED`, 157, 13);
}

function drawRecording() {
  backTriangle();
  textCenter("SAT 06/06 09:41", 8, 10);
  icon(recording === "paused" ? "pause_circle" : "radio_button_checked", 100, 66, 42);
  textCenter(recording === "paused" ? "PAUSED" : "REC", 92, 18);
  [
    { label: "PAUSE", icon: "pause_circle", x: 14 },
    { label: "RESUME", icon: "play_circle", x: 74 },
    { label: "FINISH", icon: "check", x: 134 },
  ].forEach((control) => {
    roundedRect(control.x, 142, 52, 40, 8);
    icon(control.icon, control.x + 26, 162, 28);
  });
}

function drawNoteList(kind) {
  backTriangle();
  dummyNotes.forEach((note, index) => {
    const y = 44 + index * 48;
    roundedRect(12, y, 176, 38, 8);
    icon(kind === "listen" ? "headphones" : "article", 36, y + 19, 22);
    textLeftCenter(note.time.toUpperCase(), 62, y + 20, 10);
  });
}

function drawListenDetail() {
  backTriangle();
  textCenter(selectedNote.time.toUpperCase(), 34, 10);
  icon(listening ? "pause_circle" : "play_circle", 100, 76, 38);
  roundedRect(30, 114, 140, 18, 7);
  const width = Math.floor(listenProgress * 132);
  if (width > 0) {
    roundedRect(34, 118, width, 10, 4, true);
  }
  const total = durationToSeconds(selectedNote.duration);
  textCenterAt(`${formatDuration(Math.round(total * listenProgress))} / ${selectedNote.duration}`, 100, 146, 10);
  roundedRect(30, 160, 62, 28, 8);
  roundedRect(108, 160, 62, 28, 8);
  icon("pause_circle", 61, 174, 24);
  icon("play_circle", 139, 174, 24);
}

function drawReadDetail() {
  backTriangle();
  textCenter(selectedNote.time.toUpperCase(), 34, 10);
  textCenter("TRANSCRIPT", 62, 12);
  const words = selectedNote.transcript.toUpperCase().split(" ");
  const lines = [
    words.slice(0, 5).join(" "),
    words.slice(5, 10).join(" "),
    words.slice(10, 15).join(" "),
    words.slice(15, 20).join(" "),
  ].filter(Boolean);
  lines.forEach((lineText, index) => textCenter(lineText, 90 + index * 20, 8));
}

function drawCalendar() {
  backTriangle();
  textCenter("SAT 06/06", 34, 15);
  [
    ["09:00", "DESIGN"],
    ["11:30", "SYNC"],
    ["15:00", "WALK"],
  ].forEach((event, index) => {
    const y = 62 + index * 40;
    roundedRect(16, y, 168, 30, 8);
    textLeftCenter(event[0], 28, y + 15, 9);
    textLeftCenter(event[1], 86, y + 15, 11);
  });
}

function formatDuration(seconds) {
  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;
  return `${String(minutes).padStart(2, "0")}:${String(rest).padStart(2, "0")}`;
}

function durationToSeconds(duration) {
  const [minutes, seconds] = duration.split(":").map((part) => Number.parseInt(part, 10));
  return minutes * 60 + seconds;
}

function formatClockMinutes(minutes) {
  const hour = Math.floor(minutes / 60) % 24;
  const minute = minutes % 60;
  return `${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`;
}

function drawAlarm() {
  backTriangle();
  icon("alarm", 100, 54, 34);
  textCenter(formatClockMinutes(alarmMinutes), 84, 26);
  textCenter(alarmOn ? "ON" : "OFF", 128, 16);
  drawTripleTimeControls(alarmOn ? "OFF" : "ON", "-30", "+30");
}

function drawStopwatch() {
  backTriangle();
  icon("timer", 100, 50, 32);
  textCenter(formatDuration(stopwatchSeconds), 84, 26);
  roundedRect(22, 146, 70, 34, 8);
  roundedRect(108, 146, 70, 34, 8);
  textCenterAt(stopwatchRunning ? "PAUSE" : "START", 57, 164, 9);
  textCenterAt("RESET", 143, 164, 9);
}

function drawTimer() {
  backTriangle();
  icon("hourglass_top", 100, 48, 32);
  textCenter(formatDuration(timerSeconds), 82, 26);
  drawTripleTimeControls(timerRunning ? "PAUSE" : "GO", "-1", "+1");
}

function drawInterval() {
  backTriangle();
  icon("repeat", 100, 45, 32);
  textCenter(`ROUND ${intervalRound}`, 78, 14);
  textCenter(formatDuration(intervalSeconds), 104, 22);
  drawTripleTimeControls(intervalRunning ? "PAUSE" : "GO", "-1", "+1");
}

function drawTripleTimeControls(centerLabel, leftLabel, rightLabel) {
  roundedRect(18, 142, 48, 36, 8);
  roundedRect(76, 142, 48, 36, 8);
  roundedRect(134, 142, 48, 36, 8);
  textCenterAt(leftLabel, 42, 160, 12);
  textCenterAt(centerLabel, 100, 160, centerLabel.length > 4 ? 8 : 10);
  textCenterAt(rightLabel, 158, 160, 12);
}

function drawLeaf() {
  backTriangle();
  icon(meta[state]?.title === "TBD" ? "star" : "category", 100, 66, 34);
  textCenter(meta[state]?.title ?? "V1", 108, 16);
}

function drawState() {
  clear();
  if (state === "static") {
    const rows = loadStaticArtRows();
    rows ? drawStaticArtRows(rows) : drawDefaultStaticArt();
  } else if (state === "time_temp") {
    drawTimeTemp();
  } else if (state === "sync") {
    drawSync();
  } else if (state === "record") {
    drawRecording();
  } else if (state === "listen" || state === "read") {
    drawNoteList(state);
  } else if (state === "note_detail") {
    selectedNote.mode === "listen" ? drawListenDetail() : drawReadDetail();
  } else if (state === "calendar") {
    drawCalendar();
  } else if (state === "alarm") {
    drawAlarm();
  } else if (state === "stopwatch") {
    drawStopwatch();
  } else if (state === "timer") {
    drawTimer();
  } else if (state === "interval") {
    drawInterval();
  } else if (state === "settings") {
    backTriangle();
    drawSettings();
  } else if (menuFor(state)) {
    backTriangle();
    drawMenu(menuFor(state));
  } else {
    drawLeaf();
  }
  stateName.textContent = `${state}${darkMode ? " / dark" : " / light"}`;
  history.innerHTML = visited.slice(-20).map((item) => `<li>${item}</li>`).join("");
  history.dataset.dirtyRegion = dirtyRegion;
}

function setState(next, dirty = "full refresh") {
  dirtyRegion = dirty;
  if (next === "record" && state !== "record") {
    recording = "active";
  }
  if (next !== state) {
    state = next;
    visited.push(next);
  }
  drawState();
}

function handleSettingsClick(x, y) {
  if (y >= 42 && y < 80) {
    setState("sync");
    return true;
  }
  if (y >= 86 && y < 134) {
    volume += x < 100 ? -1 : 1;
    volume = Math.max(0, Math.min(10, volume));
    setState("settings", "partial: volume row");
    return true;
  }
  if (y >= 140 && y < 178) {
    darkMode = !darkMode;
    setState("settings", "partial: theme");
    return true;
  }
  return false;
}

function handleTimeToolClick(x, y) {
  if (state === "alarm" && y >= 134) {
    if (x < 72) {
      alarmMinutes = (alarmMinutes + 1410) % 1440;
    } else if (x < 130) {
      alarmOn = !alarmOn;
    } else {
      alarmMinutes = (alarmMinutes + 30) % 1440;
    }
    setState(state, "partial: alarm controls");
    return true;
  }
  if (state === "stopwatch" && y >= 138) {
    if (x < 100) {
      stopwatchRunning = !stopwatchRunning;
    } else {
      stopwatchRunning = false;
      stopwatchSeconds = 0;
    }
    setState(state, "partial: stopwatch controls");
    return true;
  }
  if (state === "timer" && y >= 134) {
    if (x < 72) {
      timerSeconds = Math.max(60, timerSeconds - 60);
    } else if (x < 130) {
      timerRunning = !timerRunning;
    } else {
      timerSeconds = Math.min(5940, timerSeconds + 60);
    }
    setState(state, "partial: timer controls");
    return true;
  }
  if (state === "interval" && y >= 140) {
    if (x < 72) {
      intervalSeconds = Math.max(60, intervalSeconds - 60);
    } else if (x < 130) {
      intervalRunning = !intervalRunning;
    } else {
      intervalSeconds = Math.min(5940, intervalSeconds + 60);
    }
    setState(state, "partial: interval controls");
    return true;
  }
  return false;
}

function handleCanvasClick(x, y) {
  if (backHit(state, x, y)) {
    setState(parentOf(state));
    return;
  }

  if (state === "settings" && handleSettingsClick(x, y)) {
    return;
  }

  if (["alarm", "stopwatch", "timer", "interval"].includes(state) && handleTimeToolClick(x, y)) {
    return;
  }

  if (state === "sync") {
    if (syncPending > 0) {
      syncPending -= 1;
      syncTransferred += 1;
    }
    setState(state, "partial: sync counters");
    return;
  }

  if (state === "record") {
    if (y >= 142 && y <= 190) {
      if (x < 70) {
        recording = "paused";
        setState(state, "partial: recording controls");
      } else if (x < 130) {
        recording = "active";
        setState(state, "partial: recording controls");
      } else {
        recording = "idle";
        setState("notes");
      }
    } else {
      recording = recording === "paused" ? "active" : "paused";
      setState(state, "partial: recording status");
    }
    return;
  }

  if (state === "listen" || state === "read") {
    const index = Math.floor((y - 44) / 48);
    if (index >= 0 && index < dummyNotes.length) {
      selectedNote = { ...dummyNotes[index], mode: state };
      setState("note_detail");
      return;
    }
  }

  if (state === "note_detail" && selectedNote.mode === "listen" && y >= 150) {
    listening = x >= 100;
    setState(state, "partial: listen controls");
    return;
  }

  const item = menuHit(state, y);
  if (state === "settings" && item?.state === "toggle_theme") {
    darkMode = !darkMode;
    setState(state, "partial: theme");
    return;
  }

  setState(handleTap(state, x, y));
}

canvas.addEventListener("click", (event) => {
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor(((event.clientX - rect.left) / rect.width) * 200);
  const y = Math.floor(((event.clientY - rect.top) / rect.height) * 200);
  handleCanvasClick(x, y);
});

resetButton.addEventListener("click", () => {
  state = "static";
  darkMode = false;
  volume = 5;
  syncPending = 3;
  syncTransferred = 0;
  batteryPercent = 84;
  recording = "idle";
  listening = false;
  listenProgress = 0;
  alarmOn = false;
  alarmMinutes = 450;
  stopwatchRunning = false;
  stopwatchSeconds = 0;
  timerRunning = false;
  timerSeconds = 300;
  intervalRunning = false;
  intervalSeconds = 1500;
  intervalRound = 1;
  visited.splice(0, visited.length, "static");
  setState("static");
});

backButton.addEventListener("click", () => {
  setState(parentOf(state));
});

setInterval(() => {
  let needsDraw = false;
  if (stopwatchRunning) {
    stopwatchSeconds += 1;
    needsDraw = state === "stopwatch";
  }
  if (timerRunning && timerSeconds > 0) {
    timerSeconds -= 1;
    needsDraw = state === "timer";
  }
  if (intervalRunning && intervalSeconds > 0) {
    intervalSeconds -= 1;
    if (intervalSeconds === 0) {
      intervalRound += 1;
      intervalSeconds = intervalRound % 2 === 0 ? 300 : 1500;
    }
    needsDraw = state === "interval";
  }
  if (listening) {
    listenProgress = Math.min(1, listenProgress + 0.02);
    if (listenProgress >= 1) {
      listening = false;
    }
    needsDraw = state === "note_detail" && selectedNote.mode === "listen";
  }
  if (needsDraw) {
    dirtyRegion = "partial: ticking state";
    drawState();
  }
}, 1000);

async function boot() {
  if (document.fonts) {
    await Promise.all([
      document.fonts.load(`18px ${FONT}`, "POCKET"),
      document.fonts.load(`24px ${ICON_FONT}`, "settings"),
      document.fonts.load(`24px ${ICON_FONT}`, "timer"),
      document.fonts.load(`24px ${ICON_FONT}`, "calendar_month"),
      document.fonts.ready,
    ]);
  }
  drawState();
}

boot();
