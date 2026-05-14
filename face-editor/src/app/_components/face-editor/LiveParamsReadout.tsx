"use client";

export function LiveParamsReadout({ text }: { text: string }) {
  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
        Live params
      </h3>
      <pre className="max-h-[380px] overflow-y-auto whitespace-pre rounded border border-face-border bg-face-panel p-2 pl-2.5 font-mono text-xs text-face-muted">
        {text}
      </pre>
    </>
  );
}
