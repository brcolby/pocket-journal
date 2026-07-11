export const AUX_LONG_PRESS_MS = 1000;
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
