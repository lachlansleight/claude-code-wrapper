"use client";

import {
    copyParamRecordToClipboard,
    faceParamsToParamRecord,
    readParamRecordFromClipboard,
    type ParamValueRecord,
} from "../../_lib/face-editor/inspectorParamClipboard";
import type { FaceParams } from "../../_lib/face-engine/faceParams";

const btnClass =
    "rounded border border-face-border bg-face-panel px-2.5 py-1 text-[0.78em] font-inherit text-face-text hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45";

export function InspectorParamActions({
    params,
    disabled = false,
    showDelete = false,
    onPaste,
    onDelete,
}: {
    params: FaceParams;
    disabled?: boolean;
    showDelete?: boolean;
    onPaste: (record: ParamValueRecord) => void;
    onDelete?: () => void;
}) {
    return (
        <div className="mb-2.5 flex flex-wrap items-center gap-2">
            <button
                type="button"
                className={btnClass}
                disabled={disabled}
                onClick={async () => {
                    try {
                        await copyParamRecordToClipboard(faceParamsToParamRecord(params));
                    } catch (err) {
                        console.warn("Clipboard write blocked", err);
                    }
                }}
            >
                Copy
            </button>
            <button
                type="button"
                className={btnClass}
                disabled={disabled}
                onClick={async () => {
                    const parsed = await readParamRecordFromClipboard();
                    if (!parsed.ok) {
                        alert(parsed.error);
                        return;
                    }
                    onPaste(parsed.record);
                }}
            >
                Paste
            </button>
            {showDelete && onDelete ? (
                <button
                    type="button"
                    className={`${btnClass} hover:border-red-500/60 hover:bg-red-950/40 hover:text-red-300`}
                    disabled={disabled}
                    onClick={onDelete}
                >
                    Delete
                </button>
            ) : null}
        </div>
    );
}
