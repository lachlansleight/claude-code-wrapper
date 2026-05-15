import Panel from "./atoms/Panel";

const PanelSwitcher = ({
  verbTimelineMode,
  setMode,
}: {
  verbTimelineMode: boolean;
  setMode: (mode: "blend" | "verbTimeline") => void;
}): JSX.Element => {
  return (
    <Panel className="flex flex-col gap-2">
      <button
        type="button"
        className={
          !verbTimelineMode
            ? "rounded border border-face-accent bg-face-panel-2 px-3 py-1.5 text-sm font-inherit text-face-accent"
            : "rounded border border-face-border bg-face-panel px-3 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2"
        }
        onClick={() => setMode("blend")}
      >
        Blend (V/A)
      </button>
      <button
        type="button"
        className={
          verbTimelineMode
            ? "rounded border border-face-accent bg-face-panel-2 px-3 py-1.5 text-sm font-inherit text-face-accent"
            : "rounded border border-face-border bg-face-panel px-3 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2"
        }
        onClick={() => setMode("verbTimeline")}
      >
        Verb timelines
      </button>
    </Panel>
  );
};

export default PanelSwitcher;
