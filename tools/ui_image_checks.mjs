export function analyzeFrame(pixels, width, height) {
  if (!(pixels instanceof Uint8Array) || pixels.length !== width * height) {
    throw new Error(`expected ${width}x${height} pixels`);
  }

  let blackPixels = 0;
  let edgePixels = 0;
  const edges = { top: 0, right: 0, bottom: 0, left: 0 };
  const halves = { left: 0, right: 0, top: 0, bottom: 0 };
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      if (!pixels[y * width + x]) continue;
      blackPixels += 1;
      if (x === 0 || y === 0 || x === width - 1 || y === height - 1) edgePixels += 1;
      if (y === 0) edges.top += 1;
      if (x === width - 1) edges.right += 1;
      if (y === height - 1) edges.bottom += 1;
      if (x === 0) edges.left += 1;
      if (x < width / 2) halves.left += 1; else halves.right += 1;
      if (y < height / 2) halves.top += 1; else halves.bottom += 1;
    }
  }

  const visited = new Uint8Array(pixels.length);
  const components = [];
  for (let start = 0; start < pixels.length; start += 1) {
    if (!pixels[start] || visited[start]) continue;
    const queue = [start];
    visited[start] = 1;
    let head = 0;
    let count = 0;
    let minX = width;
    let minY = height;
    let maxX = -1;
    let maxY = -1;
    while (head < queue.length) {
      const index = queue[head++];
      const x = index % width;
      const y = Math.floor(index / width);
      count += 1;
      minX = Math.min(minX, x); maxX = Math.max(maxX, x);
      minY = Math.min(minY, y); maxY = Math.max(maxY, y);
      for (const [nx, ny] of [[x - 1, y], [x + 1, y], [x, y - 1], [x, y + 1]]) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
        const next = ny * width + nx;
        if (pixels[next] && !visited[next]) {
          visited[next] = 1;
          queue.push(next);
        }
      }
    }
    components.push({ count, x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1 });
  }
  components.sort((a, b) => b.count - a.count);

  const density = blackPixels / pixels.length;
  const horizontalImbalance = blackPixels ? Math.abs(halves.left - halves.right) / blackPixels : 1;
  const verticalImbalance = blackPixels ? Math.abs(halves.top - halves.bottom) / blackPixels : 1;
  const edgeSides = Object.values(edges).filter((count) => count > 0).length;
  return { width, height, blackPixels, density, edgePixels, edges, edgeSides, horizontalImbalance, verticalImbalance,
    componentCount: components.length, largestComponent: components[0] ?? null };
}

export function validateFrame(metrics, options = {}) {
  const limits = { minDensity: 0.002, maxDensity: 0.55, maxEdgePixels: 0, minEdgeSides: 0,
    maxImbalance: 0.96, maxComponentFraction: 0.96, ...options };
  const errors = [];
  if (metrics.blackPixels === 0) errors.push("frame is blank");
  if (metrics.density < limits.minDensity) errors.push(`density ${metrics.density.toFixed(4)} is too low`);
  if (metrics.density > limits.maxDensity) errors.push(`density ${metrics.density.toFixed(4)} is too high`);
  if (metrics.edgePixels > limits.maxEdgePixels) errors.push(`${metrics.edgePixels} black pixels touch the display edge`);
  if (metrics.edgeSides < limits.minEdgeSides) errors.push(`content reaches only ${metrics.edgeSides} display edges`);
  if (metrics.horizontalImbalance > limits.maxImbalance || metrics.verticalImbalance > limits.maxImbalance) {
    errors.push("ink distribution is extremely imbalanced");
  }
  const component = metrics.largestComponent;
  if (component && (component.width > metrics.width * limits.maxComponentFraction ||
                    component.height > metrics.height * limits.maxComponentFraction)) {
    errors.push(`connected component spans ${component.width}x${component.height}`);
  }
  return errors;
}
