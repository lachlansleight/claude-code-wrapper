/** Map valence / activation to blend canvas pixel coords (simulator_v3). */
export function vaToCanvas(v: number, a: number, w: number, h: number): [number, number] {
    const pad = 14;
    const x = pad + (v + 1) * 0.5 * (w - 2 * pad);
    const y = h - pad - a * (h - 2 * pad);
    return [x, y];
}

export function canvasClientToVa(
    clientX: number,
    clientY: number,
    rect: DOMRectReadOnly,
    canvasW: number,
    canvasH: number
): [number, number] {
    const cx = (clientX - rect.left) * (canvasW / rect.width);
    const cy = (clientY - rect.top) * (canvasH / rect.height);
    const pad = 14;
    const w = canvasW;
    const h = canvasH;
    const v = ((cx - pad) / (w - 2 * pad)) * 2 - 1;
    const a = 1 - (cy - pad) / (h - 2 * pad);
    return [Math.max(-1, Math.min(1, v)), Math.max(0, Math.min(1, a))];
}
