export const AUX_LONG_PRESS_MS = 500;
export const AUX_DOUBLE_CLICK_MS = 350;

export function createAuxClickDetector({ onShort, onDouble, now, setTimer, clearTimer }) {
  let timer = null;
  let firstClickAt = 0;
  let firstLabel = "";

  function cancel() {
    if (timer !== null) {
      clearTimer(timer);
    }
    timer = null;
    firstClickAt = 0;
    firstLabel = "";
  }

  function click(label) {
    const clickedAt = now();
    if (timer !== null) {
      const elapsedMs = clickedAt - firstClickAt;
      const pendingLabel = firstLabel;
      cancel();
      if (elapsedMs <= AUX_DOUBLE_CLICK_MS) {
        onDouble(`${label} double`);
        return;
      }
      onShort(pendingLabel);
    }

    firstClickAt = clickedAt;
    firstLabel = label;
    timer = setTimer(() => {
      const pendingLabel = firstLabel;
      cancel();
      onShort(pendingLabel);
    }, AUX_DOUBLE_CLICK_MS + 1);
  }

  return { cancel, click };
}

export function createAuxPressDetector({ onShort, onLong, now, setTimer, clearTimer }) {
  let timer = null;
  let pressedAt = 0;
  let pressedLabel = "";
  let pressed = false;
  let longEmitted = false;

  function clearLongTimer() {
    if (timer !== null) {
      clearTimer(timer);
      timer = null;
    }
  }

  function emitLong(label) {
    if (!pressed || longEmitted) {
      return false;
    }
    clearLongTimer();
    longEmitted = true;
    onLong(`${label} long`);
    return true;
  }

  function press(label) {
    if (pressed) {
      return false;
    }
    pressed = true;
    longEmitted = false;
    pressedAt = now();
    pressedLabel = label;
    timer = setTimer(() => emitLong(pressedLabel), AUX_LONG_PRESS_MS);
    return true;
  }

  function release(label = pressedLabel) {
    if (!pressed) {
      return false;
    }
    if (!longEmitted && now() - pressedAt >= AUX_LONG_PRESS_MS) {
      emitLong(label);
    }
    clearLongTimer();
    const wasLong = longEmitted;
    pressed = false;
    longEmitted = false;
    pressedAt = 0;
    pressedLabel = "";
    if (!wasLong) {
      onShort(label);
    }
    return true;
  }

  function cancel() {
    clearLongTimer();
    pressed = false;
    longEmitted = false;
    pressedAt = 0;
    pressedLabel = "";
  }

  return { cancel, press, release };
}
