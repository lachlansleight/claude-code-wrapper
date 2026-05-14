"use client";

import type { RefObject } from "react";

export function FaceStage({
  canvasRef,
  currentExpr,
  armDeg,
  fps,
}: {
  canvasRef: RefObject<HTMLCanvasElement>;
  currentExpr: string;
  armDeg: string;
  fps: string;
}) {
  return (
    <div className="flex flex-col items-center gap-2">
      <canvas
        ref={canvasRef}
        className="aspect-square w-full max-w-[480px] rounded-full border-4 border-face-border bg-face-hole [image-rendering:pixelated]"
        width={480}
        height={480}
      />
      <div className="font-mono text-sm text-face-muted">
        expression: <span className="text-face-accent">{currentExpr}</span>
        &nbsp;·&nbsp; arm <span className="text-face-text">{armDeg}</span>
        &nbsp;·&nbsp; <span className="text-face-good">{fps}</span>
      </div>
    </div>
  );
}
