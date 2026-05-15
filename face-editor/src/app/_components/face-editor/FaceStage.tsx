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
        <div className="flex flex-col items-center gap-2 pt-[16px]">
            <canvas
                ref={canvasRef}
                className="aspect-square w-full max-w-[360px] rounded-full border-4 border-face-border bg-face-hole [image-rendering:pixelated]"
                width={360}
                height={360}
            />
            <div className="font-mono text-sm text-face-muted">
                <span className="text-face-good">{fps}</span>
            </div>
        </div>
    );
}
