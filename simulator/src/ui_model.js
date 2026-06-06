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
};

export const meta = {
  static: { title: "POCKET", parent: "static" },
  time_temp: { title: "TIME/TEMP", parent: "static" },
  home: { title: "HOME", parent: "static" },
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
};

const menus = {
  home: [
    ["NOTES", "notes"],
    ["TIME", "time"],
    ["SETTINGS", "settings"],
    ["CALENDAR", "calendar"],
    ["TBD", "tbd"],
  ],
  notes: [
    ["RECORD", "record"],
    ["LISTEN", "listen"],
    ["READ", "read"],
  ],
  time: [
    ["ALARM", "alarm"],
    ["STOPWATCH", "stopwatch"],
    ["TIMER", "timer"],
    ["INTERVAL", "interval"],
  ],
  settings: [
    ["SYNC", "sync"],
    ["VOLUME", "volume"],
  ],
};

export function parentOf(state) {
  return meta[state]?.parent ?? "static";
}

export function menuFor(state) {
  return menus[state] ?? null;
}

export function handleTap(state, x, y) {
  if (state !== "static" && x >= 0 && x < 42 && y >= 0 && y < 32) {
    return parentOf(state);
  }
  if (state === "static") {
    return "time_temp";
  }
  if (state === "time_temp") {
    return y < 100 ? "static" : "home";
  }

  const menu = menuFor(state);
  if (!menu) {
    return state;
  }
  const top = 36;
  const bottom = 192;
  if (y < top || y >= bottom) {
    return state;
  }
  const rowHeight = Math.floor((200 - top - 8) / menu.length);
  const index = Math.floor((y - top) / rowHeight);
  return menu[index]?.[1] ?? state;
}

