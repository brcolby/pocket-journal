import { handleTap, menuFor, meta, parentOf } from "./ui_model.js";

const canvas = document.querySelector("#display");
const ctx = canvas.getContext("2d", { alpha: false });
const stateName = document.querySelector("#stateName");
const history = document.querySelector("#history");
const resetButton = document.querySelector("#resetButton");
const backButton = document.querySelector("#backButton");

let state = "static";
const visited = ["static"];

function clear() {
  ctx.fillStyle = "#f8f7ef";
  ctx.fillRect(0, 0, 200, 200);
  ctx.fillStyle = "#111";
  ctx.strokeStyle = "#111";
  ctx.lineWidth = 1;
  ctx.font = "10px ui-monospace, SFMono-Regular, Menlo, monospace";
  ctx.textBaseline = "top";
}

function textCenter(text, y, size = 16) {
  ctx.font = `${size}px ui-monospace, SFMono-Regular, Menlo, monospace`;
  ctx.textAlign = "center";
  ctx.fillText(text, 100, y);
}

function textLeft(text, x, y, size = 16) {
  ctx.font = `${size}px ui-monospace, SFMono-Regular, Menlo, monospace`;
  ctx.textAlign = "left";
  ctx.fillText(text, x, y);
}

function header(title, showBack = true) {
  ctx.strokeRect(0.5, 0.5, 199, 27);
  textCenter(title, 7, 14);
  if (showBack) {
    textLeft("<", 7, 6, 16);
  }
}

function drawMenu(items) {
  const top = 36;
  const rowHeight = Math.floor((200 - top - 8) / items.length);
  items.forEach(([label], index) => {
    const y = top + index * rowHeight;
    ctx.strokeRect(12.5, y + 0.5, 176, rowHeight - 4);
    textLeft(label, 24, y + 10, 16);
  });
}

function drawState() {
  clear();
  if (state === "static") {
    ctx.strokeRect(0.5, 0.5, 199, 199);
    textCenter("POCKET", 58, 24);
    textCenter("JOURNAL", 88, 24);
    textCenter("TAP", 150, 16);
  } else if (state === "time_temp") {
    header("TIME/TEMP");
    textCenter("09:41", 58, 32);
    textCenter("72F", 112, 24);
    textCenter("TAP HOME", 162, 16);
  } else if (menuFor(state)) {
    header(meta[state].title);
    drawMenu(menuFor(state));
  } else {
    header(meta[state].title);
    textCenter(meta[state].title, 80, 18);
    textCenter("V1", 132, 16);
  }
  stateName.textContent = state;
  history.innerHTML = visited.slice(-20).map((item) => `<li>${item}</li>`).join("");
}

function setState(next) {
  if (next !== state) {
    state = next;
    visited.push(next);
    drawState();
  }
}

canvas.addEventListener("click", (event) => {
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor(((event.clientX - rect.left) / rect.width) * 200);
  const y = Math.floor(((event.clientY - rect.top) / rect.height) * 200);
  setState(handleTap(state, x, y));
});

resetButton.addEventListener("click", () => {
  state = "static";
  visited.splice(0, visited.length, "static");
  drawState();
});

backButton.addEventListener("click", () => {
  setState(parentOf(state));
});

drawState();

