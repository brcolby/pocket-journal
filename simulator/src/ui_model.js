export const states = {
  STATIC: "static",
  TIME_TEMP: "time_temp",
  HOME: "home",
  NOTES: "notes",
  RECORD: "record",
  LISTEN: "listen",
  READ: "read",
  TIME: "time",
  ALARM: "alarm",
  STOPWATCH: "stopwatch",
  TIMER: "timer",
  INTERVAL: "interval",
  SETTINGS: "settings",
  VOLUME: "volume",
  CALENDAR: "calendar",
  NOTE_DETAIL: "note_detail",
};

export const meta = {
  static: { title: "POCKET", parent: "static" },
  time_temp: { title: "TIME/TEMP", parent: "static" },
  home: { title: "HOME", parent: "time_temp" },
  notes: { title: "NOTES", parent: "home" },
  record: { title: "RECORD", parent: "notes" },
  listen: { title: "LISTEN", parent: "notes" },
  read: { title: "READ", parent: "notes" },
  time: { title: "TIME", parent: "home" },
  alarm: { title: "ALARM", parent: "time" },
  stopwatch: { title: "STOPWATCH", parent: "time" },
  timer: { title: "TIMER", parent: "time" },
  interval: { title: "INTERVAL", parent: "time" },
  settings: { title: "SETTINGS", parent: "home" },
  volume: { title: "VOLUME", parent: "settings" },
  calendar: { title: "CALENDAR", parent: "home" },
  note_detail: { title: "NOTE", parent: "read" },
};

export const defaultHomeDesign = {
  title: "Pocket Journal",
  slots: [
    { label: "", icon: "notebook", state: "notes" },
    { label: "", icon: "time", state: "time" },
    { label: "", icon: "settings", state: "settings" },
  ],
};

const menus = {
  home: [
    { label: "", icon: "notebook", state: "notes" },
    { label: "", icon: "time", state: "time" },
    { label: "", icon: "settings", state: "settings" },
  ],
  notes: [
    { label: "", icon: "microphone", state: "record" },
    { label: "", icon: "document_audio", state: "listen" },
    { label: "", icon: "read_me", state: "read" },
  ],
  time: [
    { label: "", icon: "alarm", state: "alarm" },
    { label: "", icon: "timer", state: "stopwatch" },
    { label: "", icon: "timer", state: "timer" },
    { label: "", icon: "repeat", state: "interval" },
  ],
  settings: [
    { label: "Volume", value: "10", action: "volume", state: "volume" },
    { label: "Light/Dark", value: "LIGHT", action: "theme", state: "settings" },
    { label: "12/24", value: "24H", action: "clock", state: "settings" },
  ],
};

export const dummyNotes = [
  {
    id: "20260606-0941",
    time: "Sat 06/06 09:41",
    duration: "00:38",
    transcript: "Remember to test the recording flow before the first hardware pass.",
  },
  {
    id: "20260605-1812",
    time: "Fri 06/05 18:12",
    duration: "01:14",
    transcript: "Daily calendar sync should happen before the device wakes into the day view.",
  },
  {
    id: "20260604-0730",
    time: "Thu 06/04 07:30",
    duration: "00:51",
    transcript: "Try rounded home buttons with simple symbols and no navigation copy.",
  },
];

export function loadHomeDesign() {
  try {
    const raw = localStorage.getItem("pocketJournalHomeDesign");
    if (!raw) {
      return defaultHomeDesign;
    }
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed.slots) || parsed.slots.length === 0) {
      return defaultHomeDesign;
    }
    return {
      title: parsed.title || defaultHomeDesign.title,
      slots: parsed.slots.slice(0, 3).map((slot, index) => ({
        label: slot.label || defaultHomeDesign.slots[index]?.label || "Slot",
        icon: slot.icon || defaultHomeDesign.slots[index]?.icon || "circle",
        state: slot.state || defaultHomeDesign.slots[index]?.state || "settings",
      })),
    };
  } catch {
    return defaultHomeDesign;
  }
}

export function parentOf(state) {
  return meta[state]?.parent ?? "static";
}

export function menuFor(state) {
  if (state === "home") {
    return loadHomeDesign().slots;
  }
  return menus[state] ?? null;
}

export function backHit(state, x, y) {
  void state;
  void x;
  void y;
  return false;
}

export function tilesFor(state) {
  const menu = menuFor(state);
  if (!menu) {
    return [];
  }
  if (["home", "settings"].includes(state) && menu.length >= 3) {
    return [
      { ...menu[0], x: 0, y: 0, width: 200, height: 66 },
      { ...menu[1], x: 0, y: 66, width: 200, height: 67 },
      { ...menu[2], x: 0, y: 133, width: 200, height: 67 },
    ];
  }
  if (state === "notes" && menu.length >= 3) {
    return [
      { ...menu[0], x: 0, y: 0, width: 200, height: 67 },
      { ...menu[1], x: 0, y: 67, width: 200, height: 66 },
      { ...menu[2], x: 0, y: 133, width: 200, height: 67 },
    ];
  }
  return menu.slice(0, 4).map((item, index) => ({
    ...item,
    x: index % 2 === 0 ? 0 : 100,
    y: index < 2 ? 0 : 100,
    width: 100,
    height: 100,
  }));
}

export function menuHit(state, x, y) {
  const tiles = tilesFor(state);
  if (!tiles.length) {
    return null;
  }
  for (const tile of tiles) {
    if (x >= tile.x && x < tile.x + tile.width && y >= tile.y && y < tile.y + tile.height) {
      return tile;
    }
  }
  return null;
}

export function handleAuxLong(state) {
  return state === "static" ? "static" : parentOf(state);
}

export function handleAuxShort(state) {
  if (state === "static") {
    return "time_temp";
  }
  if (state === "time_temp") {
    return "home";
  }
  if (state === "notes") {
    return "record";
  }
  if (state === "time") {
    return "stopwatch";
  }
  return state;
}

export function handleTap(state, x, y) {
  if (backHit(state, x, y)) {
    return parentOf(state);
  }

  if (state === "static") {
    return "time_temp";
  }
  if (state === "time_temp") {
    return "home";
  }

  const item = menuHit(state, x, y);
  return item?.state ?? state;
}
