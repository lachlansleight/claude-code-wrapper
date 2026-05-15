/** Mirrors firmware `Settings::NamedColor` defaults (see Settings.cpp). */
const DEFAULTS: Record<string, [number, number, number]> = {
    background: [0, 0, 0],
    foreground: [255, 255, 255],
    thinking: [36, 56, 120],
    reading: [78, 146, 210],
    writing: [104, 118, 228],
    executing: [156, 64, 216],
    executing_long: [210, 75, 220],
    blocked: [255, 48, 24],
    finished: [255, 228, 32],
    excited: [40, 255, 80],
    wants_at: [255, 200, 40],
};

export interface RobotSettings {
    rgb(name: string): [number, number, number];
    set(name: string, r: number, g: number, b: number): void;
    version(): number;
    keys(): string[];
}

export function createRobotSettings(): RobotSettings {
    const colors: Record<string, [number, number, number]> = JSON.parse(
        JSON.stringify(DEFAULTS)
    ) as Record<string, [number, number, number]>;
    let version = 1;
    return {
        rgb(name: string): [number, number, number] {
            return colors[name] ?? [0, 0, 0];
        },
        set(name: string, r: number, g: number, b: number): void {
            if (!colors[name]) return;
            colors[name] = [r & 0xff, g & 0xff, b & 0xff];
            version += 1;
        },
        version(): number {
            return version;
        },
        keys(): string[] {
            return Object.keys(DEFAULTS);
        },
    };
}
