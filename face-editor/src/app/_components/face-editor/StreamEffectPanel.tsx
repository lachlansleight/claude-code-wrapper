"use client";

import Panel from "./atoms/Panel";
import type { StreamEffectPreview } from "../../_lib/face-engine/frameController";

const OPTIONS: { id: StreamEffectPreview; label: string }[] = [
    { id: "none", label: "None" },
    { id: "reading", label: "Reading" },
    { id: "writing", label: "Writing" },
];

export function StreamEffectPanel({
    value,
    onChange,
}: {
    value: StreamEffectPreview;
    onChange: (mode: StreamEffectPreview) => void;
}): JSX.Element {
    return (
        <Panel className="flex flex-col gap-2">
            <div className="text-xs font-medium uppercase tracking-wide text-face-muted">
                Effect type
            </div>
            <div className="flex flex-col gap-1.5" role="radiogroup" aria-label="Stream effect type">
                {OPTIONS.map(opt => (
                    <label
                        key={opt.id}
                        className="flex cursor-pointer items-center gap-2 text-sm text-face-text"
                    >
                        <input
                            type="radio"
                            name="stream-effect"
                            className="accent-face-accent"
                            checked={value === opt.id}
                            onChange={() => onChange(opt.id)}
                        />
                        {opt.label}
                    </label>
                ))}
            </div>
            <p className="text-xs leading-snug text-face-muted">
                Simulates firmware read/write code streams behind the face (preview only).
            </p>
        </Panel>
    );
}
