"use client";

import { useCallback, useState } from "react";
import Panel from "./atoms/Panel";
import {
    bridgeBase,
    postDisplayMode,
    postMotorsDisabled,
    type BridgeDisplayMode,
} from "../../_lib/face-editor/bridge";

/** Turn a `postRaw` result into a one-line status string. */
async function describeResponse(res: Response | null, label: string, okText: string): Promise<string> {
    if (!res) return `Failed to reach bridge at ${bridgeBase()}`;
    if (!res.ok) {
        const text = await res.text().catch(() => "");
        return `${label} → HTTP ${res.status}${text ? `: ${text}` : ""}`;
    }
    return okText;
}

const BTN_BASE = "rounded border bg-face-panel px-3 py-1 text-[0.72em] font-inherit hover:bg-face-panel-2";
const BTN_ACCENT = `${BTN_BASE} border-face-accent text-face-accent`;
const BTN_AMBER = `${BTN_BASE} border-amber-500/70 text-amber-200`;
/** Appended when the button matches the last value sent from this page. */
const BTN_ACTIVE = " bg-face-panel-2 font-semibold";

function buttonClass(base: string, active: boolean): string {
    return active ? base + BTN_ACTIVE : base;
}

/**
 * Robot-side settings that are sent to the firmware through the bridge
 * (`POST /api/raw/config/*` → WS `config_change` → `BridgeControl`).
 *
 * The bridge has no readback of device state, so the highlighted button only
 * reflects the last value sent from this page, not what the robot currently has.
 */
export function SettingsPanel(): JSX.Element {
    const [displayMode, setDisplayMode] = useState<BridgeDisplayMode | null>(null);
    const [displayModeStatus, setDisplayModeStatus] = useState<string | null>(null);
    const [armsDisabled, setArmsDisabled] = useState<boolean | null>(null);
    const [armsStatus, setArmsStatus] = useState<string | null>(null);

    const sendDisplayMode = useCallback(async (mode: BridgeDisplayMode) => {
        setDisplayModeStatus(null);
        const res = await postDisplayMode(mode);
        const msg = await describeResponse(res, `display-mode ${mode}`, `Display mode → ${mode}`);
        if (res?.ok) setDisplayMode(mode);
        setDisplayModeStatus(msg);
    }, []);

    const sendArmsDisabled = useCallback(async (disabled: boolean) => {
        setArmsStatus(null);
        const res = await postMotorsDisabled(disabled);
        const msg = await describeResponse(
            res,
            `motors-disabled ${disabled}`,
            disabled ? "Arm movement → off (servo held centred)" : "Arm movement → on"
        );
        if (res?.ok) setArmsDisabled(disabled);
        setArmsStatus(msg);
    }, []);

    return (
        <div className="flex min-h-0 flex-col gap-2">
            <Panel className="flex flex-col gap-2">
                <h4 className="m-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                    Robot display mode
                </h4>
                <p className="m-0 text-[0.72em] leading-snug text-face-muted">
                    Switches firmware scene via{" "}
                    <span className="font-mono">POST /api/raw/config/display-mode</span> (
                    {bridgeBase()}).
                </p>
                <div className="flex flex-wrap gap-2">
                    <button
                        type="button"
                        className={buttonClass(BTN_ACCENT, displayMode === "face")}
                        onClick={() => void sendDisplayMode("face")}
                    >
                        Face mode
                    </button>
                    <button
                        type="button"
                        className={buttonClass(BTN_AMBER, displayMode === "text")}
                        onClick={() => void sendDisplayMode("text")}
                    >
                        Text mode
                    </button>
                    <button
                        type="button"
                        className={buttonClass(BTN_AMBER, displayMode === "debug")}
                        onClick={() => void sendDisplayMode("debug")}
                    >
                        Debug mode
                    </button>
                </div>
                {displayModeStatus ? (
                    <p className="m-0 text-[0.72em] leading-snug text-face-muted">{displayModeStatus}</p>
                ) : null}
            </Panel>

            <Panel className="flex flex-col gap-2">
                <h4 className="m-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                    Arm movement
                </h4>
                <p className="m-0 text-[0.72em] leading-snug text-face-muted">
                    Enables or disables the arm servo via{" "}
                    <span className="font-mono">POST /api/raw/config/motors-disabled</span> (
                    {bridgeBase()}). When off, the servo is held centred and all verb / emotion arm
                    motion is ignored. The setting is persisted on the robot and survives reboot, so
                    remember to turn it back on.
                </p>
                <div className="flex flex-wrap gap-2">
                    <button
                        type="button"
                        className={buttonClass(BTN_ACCENT, armsDisabled === false)}
                        onClick={() => void sendArmsDisabled(false)}
                    >
                        Arms on
                    </button>
                    <button
                        type="button"
                        className={buttonClass(BTN_AMBER, armsDisabled === true)}
                        onClick={() => void sendArmsDisabled(true)}
                    >
                        Arms off
                    </button>
                </div>
                {armsStatus ? (
                    <p className="m-0 text-[0.72em] leading-snug text-face-muted">{armsStatus}</p>
                ) : null}
            </Panel>

            <p className="m-0 px-1 text-[0.68em] leading-snug text-face-muted">
                The bridge does not report device state back, so highlights only show the last value
                sent from this page.
            </p>
        </div>
    );
}
