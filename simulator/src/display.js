export const DISPLAY_WIDTH = 200;
export const DISPLAY_HEIGHT = 200;
export const PAPER = "#ffffff";
export const INK = "#000000";
export const ONE_BIT_THRESHOLD = 128;

export function quantizeRgbaBuffer(data, threshold = ONE_BIT_THRESHOLD) {
  for (let index = 0; index < data.length; index += 4) {
    const luminance = Math.round((data[index] * 299 + data[index + 1] * 587 + data[index + 2] * 114) / 1000);
    const value = luminance < threshold ? 0 : 255;
    data[index] = value;
    data[index + 1] = value;
    data[index + 2] = value;
    data[index + 3] = 255;
  }
  return data;
}

export function uniqueRgbValues(data) {
  const values = new Set();
  for (let index = 0; index < data.length; index += 4) {
    values.add(data[index]);
    values.add(data[index + 1]);
    values.add(data[index + 2]);
  }
  return [...values].sort((left, right) => left - right);
}
