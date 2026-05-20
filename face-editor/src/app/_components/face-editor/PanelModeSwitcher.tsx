import Panel from "./atoms/Panel";

export type SimulatorPanelMode = "blend" | "verbTimeline" | "logReplay";

const PanelModeSwitcher = ({
    mode,
    setMode,
}: {
    mode: SimulatorPanelMode;
    setMode: (mode: SimulatorPanelMode) => void;
}): JSX.Element => {
    return (
        <Panel className="flex flex-col gap-2">
            <button
                type="button"
                className={
                    mode === "blend"
                        ? "rounded border border-face-accent bg-face-panel-2 px-3 py-1.5 text-sm font-inherit text-face-accent"
                        : "rounded border border-face-border bg-face-panel px-3 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2"
                }
                onClick={() => setMode("blend")}
            >
                Emotions
            </button>
            <button
                type="button"
                className={
                    mode === "verbTimeline"
                        ? "rounded border border-face-accent bg-face-panel-2 px-3 py-1.5 text-sm font-inherit text-face-accent"
                        : "rounded border border-face-border bg-face-panel px-3 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2"
                }
                onClick={() => setMode("verbTimeline")}
            >
                Verbs
            </button>
            <button
                type="button"
                className={
                    mode === "logReplay"
                        ? "rounded border border-face-accent bg-face-panel-2 px-3 py-1.5 text-sm font-inherit text-face-accent"
                        : "rounded border border-face-border bg-face-panel px-3 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2"
                }
                onClick={() => setMode("logReplay")}
            >
                Logs
            </button>
        </Panel>
    );
};

export default PanelModeSwitcher;
