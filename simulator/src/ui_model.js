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
  SYNC: "sync",
  VOLUME: "volume",
  CALENDAR: "calendar",
  TBD: "tbd",
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
  sync: { title: "SYNC", parent: "settings" },
  volume: { title: "VOLUME", parent: "settings" },
  calendar: { title: "CALENDAR", parent: "home" },
  tbd: { title: "TBD", parent: "home" },
  note_detail: { title: "NOTE", parent: "read" },
};

export const defaultHomeDesign = {
  title: "Pocket Journal",
  slots: [
    { label: "Notes", icon: "stylus_note", state: "notes" },
    { label: "Time", icon: "schedule", state: "time" },
    { label: "Calendar", icon: "calendar_month", state: "calendar" },
    { label: "TBD", icon: "star", state: "tbd" },
    { label: "Settings", icon: "settings", state: "settings" },
  ],
};

const menus = {
  home: [
    { label: "Notes", icon: "stylus_note", state: "notes" },
    { label: "Time", icon: "schedule", state: "time" },
    { label: "Calendar", icon: "calendar_month", state: "calendar" },
    { label: "TBD", icon: "star", state: "tbd" },
    { label: "Settings", icon: "settings", state: "settings" },
  ],
  notes: [
    { label: "Record", icon: "radio_button_checked", state: "record" },
    { label: "Listen", icon: "headphones", state: "listen" },
    { label: "Read", icon: "article", state: "read" },
  ],
  time: [
    { label: "Alarm", icon: "alarm", state: "alarm" },
    { label: "Stopwatch", icon: "timer", state: "stopwatch" },
    { label: "Timer", icon: "hourglass_top", state: "timer" },
    { label: "Interval", icon: "repeat", state: "interval" },
  ],
  settings: [
    { label: "Sync", icon: "sync", state: "sync" },
    { label: "Volume", icon: "volume_up", state: "settings_volume" },
    { label: "Dark", icon: "dark_mode", state: "toggle_theme" },
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
      slots: parsed.slots.slice(0, 5).map((slot, index) => ({
        label: slot.label || defaultHomeDesign.slots[index]?.label || "Slot",
        icon: slot.icon || defaultHomeDesign.slots[index]?.icon || "circle",
        state: slot.state || defaultHomeDesign.slots[index]?.state || "tbd",
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
  if (state !== "static" && x >= 0 && x < 42 && y >= 0 && y < 32) {
    return true;
  }
  return false;
}

export function menuHit(state, y) {
  const menu = menuFor(state);
  if (!menu) {
    return null;
  }
  const top = 42;
  const bottom = 192;
  if (y < top || y >= bottom) {
    return null;
  }
  const rowHeight = Math.floor((200 - top - 8) / menu.length);
  const index = Math.floor((y - top) / rowHeight);
  return menu[index] ?? null;
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

  const item = menuHit(state, y);
  if (item?.state === "toggle_theme") {
    return state;
  }
  return item?.state ?? state;
}
