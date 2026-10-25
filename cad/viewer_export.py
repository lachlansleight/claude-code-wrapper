"""Export the model for the assembly viewer (viewer/index.html).

Writes out/viewer/parts/<name>.stl for every part (printed, bought and
fasteners) and out/viewer/manifest.json with each part's label, colour and
group, the assembly steps, and the explode directions.

A step is a list of moves. Each move takes some parts from an offset `from`
to an offset `to` (world mm, relative to the assembled position), so a part
can be put together with others on the bench and then moved home as a group.
"""

from __future__ import annotations

import json
import math
import time
from dataclasses import asdict
from pathlib import Path

from build123d import export_stl

LABELS = {
    "bezel": "Bezel",
    "back_shell": "Back shell",
    "carrier_front": "Front carrier (P1)",
    "carrier_back": "Motor plate (P2)",
    "motor_strap": "Motor strap",
    "gear_left": "Left gear, on the X27 shaft",
    "gear_right": "Right gear, on the bearing",
    "arm_left": "Left arm",
    "arm_right": "Right arm",
    "display_board": "Waveshare ESP32-S3-Touch-LCD-1.85",
    "usb_plug_keepout": "Right-angle USB-C plug and cable",
    "x27": "X27.168 stepper",
    "port_board": "Adafruit HUSB238 PD board (ADA5991)",
    "vid6606_board": "VID6606 driver board",
    "led_ring": "WS2812B ring, 8 LEDs",
    "speaker": "Speaker (Adafruit 1890)",
    "bearing_mr105zz": "MR105ZZ bearing, 5×10×4",
}

COLOURS = {
    "bezel": "#ece8df",
    "back_shell": "#e2ddd2",
    "carrier_front": "#7fb3d5",
    "carrier_back": "#5d9cc9",
    "motor_strap": "#4a86b5",
    "gear_left": "#7ee0c3",
    "gear_right": "#6fd3b5",
    "arm_left": "#7ee0c3",
    "arm_right": "#7ee0c3",
    "display_board": "#1d3557",
    "usb_plug_keepout": "#e76f51",
    "x27": "#f4a261",
    "port_board": "#6a4c93",
    "vid6606_board": "#2a9d8f",
    "led_ring": "#e63946",
    "speaker": "#495057",
    "bearing_mr105zz": "#c0c6cc",
}


def label_for(name: str) -> str:
    if name in LABELS:
        return LABELS[name]
    if name.startswith("screw_m2x4_standoff"):
        return f"M2×4 machine screw into a board standoff ({name[-1]})"
    if name.startswith("screw_m2x5_post"):
        return f"M2×5 self-tapping screw into a post ({name[-1]})"
    if name.startswith("magnet_bezel"):
        return f"5×1.5 mm magnet in the bezel ({name[-1]})"
    if name.startswith("magnet_back"):
        return f"5×1.5 mm magnet in the back shell ({name[-1]})"
    return name


def colour_for(name: str) -> str:
    if name in COLOURS:
        return COLOURS[name]
    if name.startswith("screw_"):
        return "#9aa0a6"
    if name.startswith("magnet_"):
        return "#8d99ae"
    return "#cccccc"


def group_for(name: str, printed) -> str:
    if name in printed:
        return "printed"
    if name.startswith(("screw_", "magnet_", "bearing_")):
        return "hardware"
    return "bought"


def _add(*vs):
    return tuple(sum(c) for c in zip(*vs))


def _mul(v, k):
    return tuple(c * k for c in v)


def steps_for(m, names):
    """The assembly sequence, in world coordinates."""
    t = math.radians(-m.p.face_tilt_deg)

    def world(v):
        x, y, z = v
        return (x, y * math.cos(t) - z * math.sin(t), y * math.sin(t) + z * math.cos(t))

    back = world((0, 1, 0))       # body +Y: from behind
    front = _mul(back, -1)
    zero = (0.0, 0.0, 0.0)
    _, (ux, uz) = m.usb_mouth()
    usb_out = world((ux, 0, uz))

    def arm_dir(side):
        a = math.radians(m.rest_angle(side))
        return world((math.cos(a), 0, math.sin(a)))

    def pick(prefix):
        return [n for n in names if n.startswith(prefix)]

    twist_axis = list(world((0, -1, 0)))  # turning about it is counter-clockwise from the front
    twist = m.p.lock_twist_deg

    def mv(parts, frm, to=zero, rot=None):
        out = {"parts": [p for p in parts if p in names], "from": list(frm), "to": list(to)}
        if rot is not None:
            out.update({"rot_from": rot[0], "rot_to": rot[1], "axis": twist_axis})
        return out

    board_bench = _mul(back, 70)
    motor_bench = _mul(back, 75)
    shell_bench = _mul(back, 135)
    shell_parts = ["back_shell", *pick("magnet_back"), "port_board", "led_ring", "speaker"]
    motor_parts = ["carrier_back", "x27", "motor_strap", "vid6606_board", "gear_left"]
    module = ["display_board", "usb_plug_keepout", "carrier_front", *pick("screw_m2x4_standoff")]
    unlocked = (-twist, -twist)

    steps = [
        {
            "title": "Bezel and magnets" if pick("magnet_bezel") else "Bezel",
            "text": ("Press the two magnets into the pockets at the bottom of the bezel, flush with the seam. "
                     "Note their polarity: the back shell's pair has to attract them.") if pick("magnet_bezel") else
                    "Lay the bezel face down. The three slots in its back edge are where the display module's "
                    "legs go in.",
            "moves": [mv(["bezel"], zero), mv(pick("magnet_bezel"), _mul(back, 25))],
        },
        {
            "title": "USB-C plug into the board",
            "text": "Plug the right-angle USB-C plug into the display board first. Once the board is in the bezel "
                    "there's no room to get it in.",
            "moves": [mv(["display_board"], board_bench, board_bench, unlocked),
                      mv(["usb_plug_keepout"], _add(board_bench, _mul(usb_out, 25)), board_bench, unlocked)],
        },
        {
            "title": "Front carrier onto the board",
            "text": "Lay the front carrier on the board's three brass standoffs and fix it with M2×4 machine screws. "
                    "The holes are slots, to take up error in the standoff positions, and the heads sit in "
                    "counterbores below the gears. The board and carrier are now one module.",
            "moves": [mv(["carrier_front"], _add(board_bench, _mul(back, 35)), board_bench, unlocked),
                      mv(pick("screw_m2x4_standoff"), _add(board_bench, _mul(back, 25)), board_bench, unlocked)],
        },
        {
            "title": "Display module in, then turn",
            "text": f"Line the carrier's three legs up with the slots in the bezel's back edge and push the module "
                    f"in, glass first. Then turn it {twist:.0f}° clockwise, looking from behind, until it stops: the "
                    "lugs ride up a ramp in the groove and pull the glass down onto the face lip.",
            "moves": [mv(module, board_bench, zero, unlocked), mv(module, zero, zero, (-twist, 0.0))],
        },
        {
            "title": "Right gear on its bearing",
            "text": "Press the bearing into the right gear, then push the gear onto the front carrier's pin. "
                    "The pin's shoulder touches only the bearing's inner race."
                    if "bearing_mr105zz" in names else
                    "Push the right gear onto the front carrier's pin.",
            "moves": [mv(["gear_right"], _mul(back, 45), _mul(back, 45)),
                      mv(["bearing_mr105zz"], _mul(back, 65), _mul(back, 45)),
                      mv(["gear_right", "bearing_mr105zz"], _mul(back, 45))],
        },
        {
            "title": "Motor plate, on the bench",
            "text": "Push the X27 into the motor plate's sleeve and put the strap over its rear pegs. Wire it to "
                    "the VID6606 board and stick that on with foam tape. Power it, drive the X27 into its end "
                    "stop, then press the left gear onto the shaft with its stub pointing where home should be.",
            "moves": [mv(["carrier_back"], motor_bench, motor_bench),
                      mv(["x27"], _add(motor_bench, _mul(back, 30)), motor_bench),
                      mv(["motor_strap"], _add(motor_bench, _mul(back, 40)), motor_bench),
                      mv(["vid6606_board"], _add(motor_bench, _mul(back, 35)), motor_bench),
                      mv(["gear_left"], _add(motor_bench, _mul(front, 30)), motor_bench)],
        },
        {
            "title": "Motor plate onto the posts",
            "text": "Slide the plate onto the three posts so the left gear meshes with the right gear at the same "
                    "angle, and screw it down with three M2×5 self-tappers.",
            "moves": [mv(motor_parts, motor_bench), mv(pick("screw_m2x5_post"), _mul(back, 25))],
        },
        {
            "title": "Back shell, on the bench",
            "text": ("Magnets into the back shell's pads, the " if pick("magnet_back") else "The ") +
                    "PD board into the sleeve at the back, the LED ring "
                    "onto the floor with its LEDs down in the windows, and the speaker face down on the ring. "
                    "Then wire across: PD board 5 V and GND to the capacitor, the VID6606, the LED ring (through "
                    "the diode) and the USB-C plug; its D+ and D- to the plug; LED data to GPIO12; the speaker to J10.",
            "moves": [mv(["back_shell"], shell_bench, shell_bench),
                      mv(pick("magnet_back"), _add(shell_bench, _mul(front, 25)), shell_bench),
                      mv(["port_board"], _add(shell_bench, _mul(front, 45)), shell_bench),
                      mv(["led_ring", "speaker"], _add(shell_bench, _mul(front, 55)), shell_bench)],
        },
        {
            "title": "Slide the back shell on",
            "text": "Bring it straight on from behind until the three catches click; tuck the wires in as it goes. "
                    "The lip at the seam locates it. To open it, pull it "
                    "straight back firmly.",
            "moves": [mv(shell_parts, shell_bench)],
        },
        {
            "title": "Arms in",
            "text": "Push each arm through its slot into its gear stub. The catches hold the shell, so the arms "
                    "carry no load and swing clear of the slot edges.",
            "moves": [mv(["arm_left"], _mul(arm_dir("L"), 30)), mv(["arm_right"], _mul(arm_dir("R"), 30))],
        },
    ]
    # where the camera looks from (direction from the parts toward the camera)
    from_behind = list(world((0.65, 1.0, 0.75)))
    from_front = list(world((-0.7, -1.0, 0.45)))
    into_shell = list(world((1.0, -0.9, 0.8)))  # into the back shell's open side
    for i, s in enumerate(steps):
        s["moves"] = [mv_ for mv_ in s["moves"] if mv_["parts"]]
        s["view"] = from_front if i == len(steps) - 1 else from_behind
    shell_step = next(s for s in steps if s["title"] == "Back shell, on the bench")
    shell_step["view"] = into_shell
    shell_step["focus"] = "step"  # frame just this step's parts
    return steps


def explode_for(steps, names):
    """Each move a part makes contributes its direction, scaled by how late the step is."""
    out = {n: [0.0, 0.0, 0.0] for n in names}
    for i, s in enumerate(steps):
        for mv_ in s["moves"]:
            d = [f - t for f, t in zip(mv_["from"], mv_["to"])]
            length = math.sqrt(sum(c * c for c in d))
            if length < 1e-6:
                continue
            k = (10 + 3 * i) / length
            for n in mv_["parts"]:
                out[n] = [o + c * k for o, c in zip(out[n], d)]
    return out


def export_viewer(m, parts, bought, report, out: Path):
    vdir = out / "viewer"
    pdir = vdir / "parts"
    pdir.mkdir(parents=True, exist_ok=True)
    for old in pdir.glob("*.stl"):
        old.unlink()
    allp = {**parts, **bought}
    names = list(allp)
    for name, shape in allp.items():
        export_stl(shape, str(pdir / f"{name}.stl"), tolerance=0.04, angular_tolerance=0.2)
    steps = steps_for(m, names)
    placed = {n for s in steps for mv_ in s["moves"] for n in mv_["parts"]}
    manifest = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "units": "mm",
        "up": [0, 0, 1],
        "parts": [{"name": n, "label": label_for(n), "group": group_for(n, parts), "colour": colour_for(n),
                   "file": f"parts/{n}.stl"} for n in names],
        "unplaced": sorted(set(names) - placed),
        "steps": steps,
        "explode": explode_for(steps, names),
        "report": report,
        "params": asdict(m.p),
    }
    (vdir / "manifest.json").write_text(json.dumps(manifest, indent=1))
    return vdir
