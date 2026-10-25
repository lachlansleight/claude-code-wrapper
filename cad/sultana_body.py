#!/usr/bin/env python3
"""Sultana's body: a parametric build123d model.

    .venv/bin/python sultana_body.py                      # build, check, export
    .venv/bin/python sultana_body.py --set sphere_d=70 face_tilt_deg=15
    .venv/bin/python sultana_body.py --render             # also write PNG previews
    .venv/bin/python sultana_body.py --min-sphere         # smallest sphere that fits everything
    .venv/bin/python sultana_body.py --serve              # export, then serve the assembly viewer
    .venv/bin/python sultana_body.py --list               # print every parameter

Every dimension lives in `Params`. Change a default there, or override it on
the command line with --set name=value (tuples as --set arm_travel_deg=-30,35).

Structure
---------
bezel          the front: face lip, glass seat, BOOT/RST pinholes, and the
               groove the display module twist-locks into.
carrier_front  (P1) screws to the board's brass standoffs. Its three legs carry
               the twist-lock lugs; it also carries the right gear's pin and three
               posts to P2.
carrier_back   (P2) holds the X27.168 in a sleeve (motor_strap closes it).
gear_left/right  involute gears, each with a stub that reaches out to just
               inside the shell. gear_left sits on the X27 shaft.
arm_left/right plug into the stubs from outside, through slots in the back shell.
back_shell     slides on from behind. The arms then lock it in place (the slots
               are closed), and two magnets at the bottom keep it snug.
               Carries the back port, the base LEDs and the speaker.

Frames
------
Body frame: the face looks toward -Y (build123d's "front" view), +X is the
viewer's right, +Z is up before tilting. The face plane is y = -face_y.
"Depth" means distance behind the face plane, so y = depth - face_y.
World frame: the body rotated about X so the face tilts up by face_tilt_deg.
The bottom flat, the LED ring and a bottom speaker are built in world coordinates.

Bought parts (display board, X27.168, port board, VID6606 board, LED ring,
speaker) are simple placeholders taken from their drawings, so the fit checks
mean something. They go into the STEP assembly but not the STLs.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import asdict, dataclass, fields, replace
from pathlib import Path

from build123d import (
    Align,
    Axis,
    Box,
    Circle,
    Compound,
    Cylinder,
    Plane,
    Polygon,
    Pos,
    Rot,
    Sphere,
    Vector,
    export_step,
    export_stl,
    extrude,
)
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps
from OCP.gp import gp_Ax1, gp_Dir, gp_Pnt

HERE = Path(__file__).resolve().parent


@dataclass
class Params:
    # --- Body -------------------------------------------------------------
    sphere_d: float = 68.0          # outer diameter of the body sphere
    wall: float = 1.8               # shell wall thickness
    face_tilt_deg: float = 16.0     # face tilted up from vertical (world)
    bottom_flat_d: float = 38.0     # flat the robot sits on (world-horizontal)
    base_offset_y: float = 2.0      # move the LED ring and bottom speaker back (world Y)
    bottom_wall: float = 1.6        # floor thickness (the LEDs sit in it)
    foot_h: float = 1.5             # three feet under the floor (sound + LED gap)
    back_flat_d: float = 26.0       # flat at the back that carries the USB-C port
    back_flat_elev_deg: float = 0.0 # tilt the back flat's normal up (body frame)

    # --- Face and display (Waveshare ESP32-S3-Touch-LCD-1.85) --------------
    glass_d: float = 55.0           # cover glass outline
    glass_clear: float = 0.25       # radial clearance around the glass
    face_lip: float = 1.2           # shell in front of the glass
    face_window_d: float = 48.6     # visible opening (active area is 45.68)
    face_rim_extra: float = 0.0     # widen the flat face ring beyond the minimum
    display_rot_deg: float = 180.0  # board rotation; 0 = USB-C at the bottom as in Waveshare's drawing
    lcd_stack: float = 3.5          # glass + LCD in front of the PCB
    lcd_d: float = 53.0             # LCD module outline behind the glass (assumed)
    pcb_t: float = 1.6
    pcb_d: float = 47.86
    standoff_len: float = 4.0       # brass M2 standoffs on the PCB back
    standoff_slot: float = 1.5      # carrier holes are slots; positions come off a drawing
    usb_plug_w: float = 12.5        # right-angle USB-C plug body (keep-out)
    usb_plug_out: float = 5.0       # plug body beyond the receptacle mouth (use a slim right-angle plug)
    usb_plug_t: float = 6.5
    button_pinholes: bool = True    # pinholes for BOOT and RST
    pinhole_d: float = 1.6

    # --- Split between bezel and back shell --------------------------------
    split_after_pcb: float = 3.0    # split plane this far behind the PCB's back face (room for the twist-lock)
    lip_h: float = 1.5              # bezel lip that locates the back shell
    lip_clear: float = 0.2
    assembly_clear: float = 0.6     # internals vs. the back shell as it slides on
    # Snap catches hold the back shell on: hooks on the bezel ride over blocks
    # inside the back shell and drop into pockets. Pull the shell straight back
    # firmly to open it.
    snap_angles: tuple = (90.0, 200.0, 340.0)  # around the seam, clear of the arm slots and twist-lock
    snap_w: float = 6.0             # hook width
    snap_t: float = 1.2             # hook thickness; thinner snaps easier and holds less
    snap_len: float = 8.0           # hook length behind the seam
    snap_engage: float = 0.9        # barb height; the hook flexes about this much going on
    snap_hold_deg: float = 50.0     # barb's holding face (90 = only opens by prying)
    snap_clear: float = 0.15
    magnets: bool = False           # two magnets at the bottom to pull the seam tight. Off: at this size
                                    # their pads reach into the glass's path (display_module_in check)
    magnet_d: float = 5.0
    magnet_h: float = 1.5
    magnet_angles: tuple = (250.0, 290.0)

    # --- Carrier plates ---------------------------------------------------
    p1_t: float = 2.0
    p2_t: float = 1.6
    gear_gap_front: float = 0.4     # P1 back face to gear front face
    gear_gap_back: float = 1.0      # gear back face to P2 (X27 shaft collar needs >= 0.8)
    post_d: float = 5.0             # P1-to-P2 posts (M2 screws through P2)
    posts: tuple = (0.0, 14.0, -20.0, -10.0, 20.0, -10.0)  # x,z pairs, relative to (0, pivot_z)
    # The display module (board + front carrier) twist-locks into the bezel: lugs on
    # three legs of the front carrier go in through slots in the bezel's back edge,
    # then turn along a groove whose ramp pulls the glass down onto the face lip.
    # (Screw bosses can't work here: the glass has to pass them on its way in.)
    lock_angles: tuple = (50.0, 170.0, 325.0)  # where the legs end up
    lock_twist_deg: float = 15.0    # turn to lock: clockwise, looking from behind
    lock_draw: float = 0.3          # how far the ramp pulls the module forward over the turn
    leg_w: float = 4.0
    leg_t: float = 1.2
    lug_t: float = 1.0              # lug thickness front to back
    lug_bearing: float = 0.7        # how far the lug overlaps the groove's back wall
    lock_clear: float = 0.2
    screw_pilot_d: float = 1.7      # M2 self-tapping into PLA
    screw_clear_d: float = 2.3
    screw_head: tuple = (3.8, 1.3)  # M2 pan head: diameter, height
    counterbore: tuple = (4.3, 1.4) # P1's screw heads sit below its back face, clear of the gears

    # --- Gears ----------------------------------------------------------------
    gear_module: float = 1.0
    gear_teeth: int = 18
    gear_t: float = 4.0
    gear_backlash: float = 0.15
    pivot_z: float = 4.0            # pivot height in the face plane (body frame)
    x27_bore_d: float = 0.95        # gear on the 1 mm X27 shaft (print undersize, ream to fit)
    pin_d: float = 3.0              # without a bearing, the other gear runs on a printed pin from P1
    pin_clear: float = 0.3
    bearing: bool = True            # run gear_right on a 5x10x4 ball bearing (MR105ZZ) instead
    bearing_size: tuple = (5.0, 10.0, 4.0)  # bore, OD, width
    bearing_fit: float = 0.05       # gear bore = OD + this; tune so the bearing presses in
    bearing_pin_fit: float = -0.05  # pin = bore + this; a 5 mm steel dowel works too
    bearing_collar_d: float = 6.6   # pin shoulder that bears on the inner race only
    stub_w: float = 5.6             # stub from the gear out to the arm socket
    socket_w: float = 3.0           # arm tongue socket (in the stub end)
    socket_t: float = 2.6
    socket_len: float = 6.0
    tongue_fit: float = 0.15        # tongue is this much smaller than the socket
    cw_r: float = 0.0               # counterweight lobe radius from the pivot (0 = none)
    cw_d: float = 7.0               # lobe diameter
    cw_insert_g: float = 0.0        # extra mass glued into the lobe

    # --- Arms (separate parts) ------------------------------------------------
    arm_rest_deg: float = 25.0      # arm angle above horizontal at offset 0
    arm_travel_deg: tuple = (-30.0, 35.0)  # offsets the slots and checks cover
    arm_len: float = 21.0           # from the stub end to the tip
    arm_root_w: float = 5.0         # width where it leaves the stub (seen from the front)
    arm_tip_w: float = 2.6          # width at the tip
    arm_t: float = 3.2              # depth front-to-back
    arm_curve_deg: float = 0.0      # bend the arm upward along its length (0 = straight)
    slot_clear: float = 0.6

    # --- X27.168 (Juken drawing X27.168.02) ---------------------------------
    motor_dir_deg: float = 180.0    # direction from shaft to housing centre (0 = +X)
    x27_body_d: float = 30.4
    x27_body_h: float = 9.6
    x27_shaft_offset: float = 5.05
    x27_shaft_len: float = 8.9      # above the housing
    x27_collar_h: float = 2.2       # the 2 mm collar at the shaft root
    x27_rear_h: float = 4.4         # pins and pegs behind the housing
    x27_peg_d: float = 1.85
    sleeve_wall: float = 1.2
    sleeve_h: float = 5.0

    # --- Back port ----------------------------------------------------------
    port: str = "pd"                # "pd": Adafruit HUSB238 switchable (ADA5991); "fpc": USB-C extension socket
    pd_size: tuple = (20.3, 29.1, 6.0)   # w, l, t without the terminal block
    fpc_size: tuple = (12.5, 12.0, 6.5)  # panel end of a 90-degree FPC USB-C extension
    port_offset_x: float = 4.5      # receptacle position on the back flat
    port_offset_z: float = 0.0
    port_roll_deg: float = 90.0     # 0 = board lies flat; 90 = stands on edge
    usbc_cut: tuple = (9.4, 3.6)
    plug_recess: tuple = (13.0, 7.5)  # outside pocket for the cable's overmould

    # --- VID6606 on an SOP-28 adapter -------------------------------------------
    drv_size: tuple = (18.0, 20.5, 5.0)  # x, z, thickness
    drv_pos: tuple = (19.5, -1.0)   # (x, z) on the back of P2
    drv_standoff: float = 1.6       # stands off P2 to clear the post screw heads (foam tape)

    # --- Base LEDs: 8 x WS2812B ring --------------------------------------------
    led_ring_od: float = 32.0
    led_ring_id: float = 18.0
    led_ring_t: float = 3.2
    led_count: int = 8
    led_window: float = 5.4
    led_ring_rot_deg: float = 22.5

    # --- Speaker ------------------------------------------------------------------
    speaker_mode: str = "bottom"    # "bottom" (fires down through the LED ring), "back", "none"
    speaker_size: tuple = (28.0, 28.0, 4.5)  # box w, d, h; set speaker_round to use w as a diameter.
                                             # Default: Adafruit 1890 (28 x 4.5 mm, 8 ohm, 0.25 W rated)
    speaker_round: bool = True
    speaker_rot_deg: float = 45.0
    speaker_lift: float = 0.5       # foam tape between the LED ring and the speaker
    speaker_back_pos: tuple = (0.0, 0.0)  # (x, z) on the back flat when speaker_mode = "back"
    grille_hole_d: float = 1.6
    grille_pitch: float = 3.0

    # --- Material and motion (torque check) ---------------------------------------
    pla_density: float = 1.24       # g/cm^3; thin arms print nearly solid
    tau_dyn_min: float = 1.0        # X27 dynamic torque at 200 deg/s and 5 V, mNm (datasheet min)
    tau_dyn_typ: float = 1.45
    tau_hold: float = 3.5           # holding torque, powered (min)
    accel_limit_dps2: float = 4000.0
    speed_peak_dps: float = 385.0   # fastest expression today: 49 deg there-and-back in 400 ms
    mesh_eff: float = 0.9


# ----------------------------------------------------------------------------
# Geometry helpers
# ----------------------------------------------------------------------------

BIG = 400.0


def slab(face2d, y0: float, y1: float):
    """Extrude an XZ sketch (u = x, v = z) so it fills y0..y1."""
    return extrude(Plane.XZ.offset(-y1) * face2d, amount=y1 - y0)


def y_box(y0: float, y1: float, size: float = BIG):
    return Pos(0, (y0 + y1) / 2, 0) * Box(size, y1 - y0, size)


def z_box(z0: float, z1: float, size: float = BIG):
    return Pos(0, 0, (z0 + z1) / 2) * Box(size, size, z1 - z0)


def y_cyl(x: float, z: float, y0: float, y1: float, d: float):
    return Pos(x, (y0 + y1) / 2, z) * Rot(X=90) * Cylinder(d / 2, y1 - y0)


def cyl_along(p0, direction, length: float, d: float):
    pl = Plane(origin=Vector(*p0), z_dir=Vector(*direction))
    return pl * Cylinder(d / 2, length, align=(Align.CENTER, Align.CENTER, Align.MIN))


def circle_at(x: float, z: float, d: float):
    return Pos(x, z) * Circle(d / 2)


def rect_at(x, z, w, h, deg=0.0):
    return Pos(x, z) * Rot(0, 0, deg) * Polygon((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2), align=None)


def rot2(x: float, z: float, deg: float):
    a = math.radians(deg)
    return (x * math.cos(a) - z * math.sin(a), x * math.sin(a) + z * math.cos(a))


def hull2(points):
    """Convex hull (monotone chain) of 2D points, CCW."""
    pts = sorted(set((round(x, 4), round(y, 4)) for x, y in points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def circle_pts(x, z, r, n=48):
    return [(x + r * math.cos(2 * math.pi * i / n), z + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def sector(cx, cz, r_in, r_out, a0, a1, n=72):
    """Annular sector about (cx, cz), angles in degrees, a0 < a1."""
    outer = [(cx + r_out * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
              cz + r_out * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    inner = [(cx + r_in * math.cos(math.radians(a1 - (a1 - a0) * i / n)),
              cz + r_in * math.sin(math.radians(a1 - (a1 - a0) * i / n))) for i in range(n + 1)]
    return Polygon(*(outer + inner), align=None)


def involute_gear(module, teeth, backlash, pa_deg=20.0, steps=10):
    """2D involute spur gear centred on the origin, a tooth on +X."""
    pa = math.radians(pa_deg)
    rp = module * teeth / 2
    rb = rp * math.cos(pa)
    ra = rp + module
    rf = rp - 1.25 * module
    inv = lambda a: math.tan(a) - a  # noqa: E731
    half = math.pi / (2 * teeth) - backlash / (2 * rp)

    def theta(r):
        return half + inv(pa) - inv(math.acos(min(1.0, rb / r)))

    r0 = max(rb, rf)
    radii = [r0 + (ra - r0) * i / steps for i in range(steps + 1)]
    pts = []
    for k in range(teeth):
        c = 2 * math.pi * k / teeth
        if rf < r0:
            pts.append((rf * math.cos(c - theta(r0)), rf * math.sin(c - theta(r0))))
        for r in radii:
            pts.append((r * math.cos(c - theta(r)), r * math.sin(c - theta(r))))
        for r in reversed(radii):
            pts.append((r * math.cos(c + theta(r)), r * math.sin(c + theta(r))))
        if rf < r0:
            pts.append((rf * math.cos(c + theta(r0)), rf * math.sin(c + theta(r0))))
    return Polygon(*pts, align=None)


def arm_centreline(px, pz, angle_deg, r0, length, curve_deg, n=12):
    """Points along the arm from radius r0 (from the pivot) to the tip; curve bends it upward."""
    pts = []
    a = math.radians(angle_deg)
    x, z = px + r0 * math.cos(a), pz + r0 * math.sin(a)
    pts.append((x, z, a))
    sign = 1.0 if math.cos(a) >= 0 else -1.0  # bend toward +Z on both sides
    for i in range(1, n + 1):
        h = a + sign * math.radians(curve_deg) * (i / n)
        step = length / n
        x += step * math.cos(h)
        z += step * math.sin(h)
        pts.append((x, z, h))
    return pts


def arm_outline_pts(px, pz, angle_deg, r0, length, w0, w1, curve_deg, grow=0.0):
    cl = arm_centreline(px, pz, angle_deg, r0, length, curve_deg)
    n = len(cl) - 1
    left, right = [], []
    for i, (x, z, h) in enumerate(cl):
        w = (w0 + (w1 - w0) * i / n) / 2 + grow
        nx, nz = -math.sin(h), math.cos(h)
        left.append((x + nx * w, z + nz * w))
        right.append((x - nx * w, z - nz * w))
    return left, right, cl[-1]


def arm_outline(px, pz, angle_deg, r0, length, w0, w1, curve_deg, grow=0.0):
    left, right, (tx, tz, _) = arm_outline_pts(px, pz, angle_deg, r0, length, w0, w1, curve_deg, grow)
    poly = Polygon(*(right + list(reversed(left))), align=None)
    return poly + circle_at(tx, tz, w1 + 2 * grow)


def vol(s) -> float:
    return sum(x.volume for x in s.solids()) if s is not None else 0.0


def overlap(a, b) -> float:
    try:
        return vol(a & b)
    except Exception:
        return float("nan")


# ----------------------------------------------------------------------------
# The model
# ----------------------------------------------------------------------------


class Sultana:
    def __init__(self, p: Params):
        self.p = p
        self.R = p.sphere_d / 2
        self.Rin = self.R - p.wall
        self.notes: list[str] = []
        self._derive()

    def _derive(self):
        p = self.p
        self.glass_r = p.glass_d / 2 + p.glass_clear
        need_r = self.glass_r + p.wall + p.face_rim_extra
        if need_r >= self.R:
            raise SystemExit(f"sphere_d {p.sphere_d} is too small for a {p.glass_d} mm glass")
        self.face_y = p.face_lip + math.sqrt(self.R**2 - need_r**2)
        self.face_r = math.sqrt(self.R**2 - self.face_y**2)

        self.d_glass = p.face_lip
        self.d_pcb_front = self.d_glass + p.lcd_stack
        self.d_pcb_back = self.d_pcb_front + p.pcb_t
        self.d_split = self.d_pcb_back + p.split_after_pcb
        self.d_p1 = self.d_pcb_back + p.standoff_len
        self.d_gear = self.d_p1 + p.p1_t + p.gear_gap_front
        self.d_p2 = self.d_gear + p.gear_t + p.gear_gap_back
        self.d_motor = self.d_p2 + p.p2_t
        self.d_motor_back = self.d_motor + p.x27_body_h

        collar_end = self.d_motor - p.x27_collar_h
        self.shaft_tip_depth = self.d_motor - p.x27_shaft_len
        gear_back = self.d_gear + p.gear_t
        if collar_end < gear_back + 0.2:
            self.notes.append(f"X27 collar reaches the gear: raise gear_gap_back by {gear_back + 0.2 - collar_end:.2f}")
        self.shaft_engage = gear_back - max(self.shaft_tip_depth, self.d_gear)
        if self.shaft_engage < 3.0:
            self.notes.append(f"X27 shaft engages its gear by only {self.shaft_engage:.1f} mm")

        self.pitch_r = p.gear_module * p.gear_teeth / 2
        self.tip_r = self.pitch_r + p.gear_module
        self.pivots = {"L": (-self.pitch_r, p.pivot_z), "R": (self.pitch_r, p.pivot_z)}

        # The back shell slides on from behind, so everything behind the lip has to
        # pass through its narrowest point.
        self.r_pass = self.r_inner_at(self.d_split + p.lip_h) - p.assembly_clear
        self.stub_len = self._stub_len()

        e = math.radians(p.back_flat_elev_deg)
        self.back_n = Vector(0, math.cos(e), math.sin(e))
        self.back_dist = math.sqrt(self.R**2 - (p.back_flat_d / 2) ** 2)
        self.floor_z = -math.sqrt(self.R**2 - (p.bottom_flat_d / 2) ** 2)

    def y(self, depth: float) -> float:
        return depth - self.face_y

    def r_inner_at(self, depth: float) -> float:
        yy = self.y(depth)
        return math.sqrt(max(self.Rin**2 - yy**2, 0.0))

    def to_world(self, shape):
        return Rot(X=-self.p.face_tilt_deg) * shape

    def rest_angle(self, side):
        return self.p.arm_rest_deg if side == "R" else 180 - self.p.arm_rest_deg

    def travel_angles(self, side):
        lo, hi = self.p.arm_travel_deg
        base = self.rest_angle(side)
        return (base + lo, base + hi) if side == "R" else (base - hi, base - lo)

    def _stub_len(self):
        """Longest stub (from the pivot) whose end stays inside r_pass over the whole travel."""
        p = self.p
        px, pz = self.pivots["R"]
        a0, a1 = self.travel_angles("R")
        best = 1e9
        half = p.stub_w / 2
        for i in range(41):
            a = math.radians(a0 + (a1 - a0) * i / 40)
            for s in (-half, half):
                # point at distance t along the stub, offset s sideways: |c + t u| = r_pass
                cx = px - s * math.sin(a)
                cz = pz + s * math.cos(a)
                ux, uz = math.cos(a), math.sin(a)
                b = cx * ux + cz * uz
                c = cx * cx + cz * cz - self.r_pass**2
                t = -b + math.sqrt(max(b * b - c, 0.0))
                best = min(best, t)
        return best

    # -- shells ----------------------------------------------------------------
    def _outer_and_hollow(self):
        p = self.p
        R, Rin = self.R, self.Rin
        y_face = -self.face_y
        n = self.back_n
        cut_back = Plane(origin=n * self.back_dist, z_dir=n) * Box(BIG, BIG, BIG, align=(Align.CENTER, Align.CENTER, Align.MIN))
        outer = (Sphere(R) & y_box(y_face, R + 1)) - cut_back
        hollow = Sphere(Rin) & y_box(y_face + p.face_lip, R)
        hollow = hollow - (Plane(origin=n * (self.back_dist - p.wall), z_dir=n)
                           * Box(BIG, BIG, BIG, align=(Align.CENTER, Align.CENTER, Align.MIN)))
        seat = y_cyl(0, 0, y_face + p.face_lip, y_face + p.face_lip + p.lcd_stack + 0.5, 2 * self.glass_r)
        window = y_cyl(0, 0, y_face - 1, y_face + p.face_lip + 0.1, p.face_window_d)
        outer_w = self.to_world(outer) - z_box(-BIG, self.floor_z)
        hollow_w = self.to_world(hollow + seat) - z_box(-BIG, self.floor_z + p.bottom_wall)
        return outer_w, hollow_w, self.to_world(window)

    def shells(self):
        p = self.p
        outer_w, hollow_w, window_w = self._outer_and_hollow()
        self.outer_w = outer_w
        shell = outer_w - hollow_w - window_w
        ys = self.y(self.d_split)
        bezel = shell & self.to_world(y_box(-BIG, ys))
        back = shell & self.to_world(y_box(ys, BIG))
        # lip: the inner half of the wall continues past the split on the bezel
        r_mid = self.Rin + p.wall / 2
        lip = (Sphere(r_mid) - Sphere(self.Rin)) & y_box(ys, ys + p.lip_h)
        for ang in self.magnet_list():  # the magnets meet face to face, so no lip there
            lip = lip - self._wall_pad(ang, 0, ys - 1, ys + p.lip_h + 1, p.magnet_d + 3.0)
        lip_cut = (Sphere(r_mid + p.lip_clear) - Sphere(self.Rin - 2)) & y_box(ys - 0.01, ys + p.lip_h + p.lip_clear)
        bezel = bezel + (self.to_world(lip) - z_box(-BIG, self.floor_z + p.bottom_wall))
        back = back - self.to_world(lip_cut)
        bezel = self._bezel_features(bezel)
        back = self._back_features(back)
        back = back - self.to_world(self.arm_slots())
        return bezel, back

    def _wall_pad(self, angle_deg, r_in, y0, y1, width):
        a = math.radians(angle_deg)
        r_mid = (r_in + self.R + 2) / 2
        block = (Pos(r_mid * math.cos(a), (y0 + y1) / 2, r_mid * math.sin(a))
                 * Rot(Y=-angle_deg) * Box(self.R + 2 - r_in, y1 - y0, width))
        return block & Sphere(self.R - 0.01)

    # -- twist-lock between the display module and the bezel ---------------------------
    def lock_geom(self):
        """Lug and leg depths and radii. The sphere flares outward going back, so the
        lug's outer face is set from the wall at its back face (where it bears) and the
        leg from the wall at its front (the narrowest point it has to pass)."""
        p = self.p
        d_lug1 = self.d_split - 1.1          # lug back face, locked
        d_lug0 = d_lug1 - p.lug_t
        r_leg_out = self.r_inner_at(d_lug0) - p.lock_clear
        r_lug_out = self.r_inner_at(d_lug1) + p.lug_bearing
        return {"d_lug0": d_lug0, "d_lug1": d_lug1, "r_leg_in": r_leg_out - p.leg_t, "r_leg_out": r_leg_out,
                "r_lug_out": r_lug_out, "r_groove": r_lug_out + p.lock_clear}

    def lock_half_angle(self):
        g = self.lock_geom()
        return math.degrees(math.asin((self.p.leg_w / 2 + self.p.lock_clear) / g["r_leg_in"]))

    def lock_legs(self):
        """P1's legs and lugs (body frame, locked position)."""
        p = self.p
        g = self.lock_geom()
        out = None
        for ang in p.lock_angles:
            pl = self._snap_plane(ang)
            leg = Polygon((g["r_leg_in"], self.y(g["d_lug0"])), (g["r_leg_out"], self.y(g["d_lug0"])),
                          (g["r_leg_out"], self.y(self.d_p1 + 0.5)), (g["r_leg_in"], self.y(self.d_p1 + 0.5)), align=None)
            lug = Polygon((g["r_leg_out"] - 0.3, self.y(g["d_lug0"])), (g["r_lug_out"], self.y(g["d_lug0"])),
                          (g["r_lug_out"], self.y(g["d_lug1"])), (g["r_leg_out"] - 0.3, self.y(g["d_lug1"])), align=None)
            s = extrude(pl * (leg + lug), amount=p.leg_w / 2, both=True)
            out = s if out is None else out + s
        return out

    def lock_cuts(self):
        """The bezel's side: an entry slot at each leg's starting angle, and a groove the
        lug turns along whose back wall ramps forward by lock_draw."""
        p = self.p
        g = self.lock_geom()
        half = self.lock_half_angle()
        r_in = g["r_leg_out"] - 0.5
        y_front = self.y(g["d_lug0"] - p.lock_clear)
        out = None
        for ang in p.lock_angles:
            start = ang - p.lock_twist_deg
            slot = slab(sector(0, 0, r_in, g["r_groove"], start - half, start + half),
                        y_front, self.y(self.d_split + p.lip_h + 0.2))
            out = slot if out is None else out + slot
            n = 10
            a0, a1 = start - half, ang + half
            for k in range(n):
                s0 = a0 + (a1 - a0) * k / n
                s1 = a0 + (a1 - a0) * (k + 1) / n
                # back wall depth: lug_t behind the front at the locked end, + draw at the start
                frac = max(0.0, min(1.0, (ang - half - s1) / max(p.lock_twist_deg, 1e-6)))
                y_back = self.y(g["d_lug1"] + p.lock_draw * frac)
                out = out + slab(sector(0, 0, r_in, g["r_groove"], s0, s1 + 0.01), y_front, y_back)
        return out

    def magnet_r(self):
        # as far out as the wall allows, leaving 0.8 mm of skin outside the pocket
        y = self.y(self.d_split)
        r_outer = math.sqrt(self.R**2 - y**2)
        return r_outer - 0.8 - self.p.magnet_d / 2

    def magnet_pad_r_in(self):
        return self.magnet_r() - self.p.magnet_d / 2 - 1.0

    def magnet_list(self):
        return self.p.magnet_angles if self.p.magnets else ()

    # -- snap catches ------------------------------------------------------------------
    def snap_radii(self):
        """(block face, hook outside, hook inside) radii. The block face is the back
        shell's narrowest inner radius along the hook, so the block is thinnest at the
        seam and thickens toward the back as the sphere flares out."""
        p = self.p
        r_block = self.r_inner_at(self.d_split + p.lip_h + p.lip_clear)
        r_out = r_block - p.snap_clear
        return r_block, r_out, r_out - p.snap_t

    def _snap_plane(self, angle_deg):
        """Local x = radial, local y = body +Y (depth), local z = around the seam."""
        a = math.radians(angle_deg)
        return Plane(origin=(0, 0, 0), x_dir=(math.cos(a), 0, math.sin(a)), z_dir=(-math.sin(a), 0, math.cos(a)))

    def _barb_profile(self, grow=0.0):
        """Barb outline in the snap plane (radial, y), its tip at the hook's end."""
        p = self.p
        _, r_out, _ = self.snap_radii()
        e = p.snap_engage
        tip = self.y(self.d_split + p.snap_len)
        ramp = e / math.tan(math.radians(30))           # lead-in the shell's edge rides up
        hold = e / math.tan(math.radians(p.snap_hold_deg))
        y_top_back = tip - 0.3 - ramp
        y_top_front = y_top_back - 0.6
        pts = [(r_out - 0.01, y_top_front - hold - grow), (r_out + e + grow, y_top_front - grow),
               (r_out + e + grow, y_top_back + grow), (r_out - 0.01, tip - 0.3 + grow)]
        return pts

    def snap_hook(self, angle_deg):
        p = self.p
        _, r_out, r_in = self.snap_radii()
        y0 = self.y(self.d_split - 0.8)  # rooted into the bezel wall and lip
        y1 = self.y(self.d_split + p.snap_len)
        beam = Polygon((r_in, y0), (r_out, y0), (r_out, y1), (r_in, y1), align=None)
        barb = Polygon(*self._barb_profile(), align=None)
        return extrude(self._snap_plane(angle_deg) * (beam + barb), amount=p.snap_w / 2, both=True)

    def snap_block(self, angle_deg):
        """The back shell's side: a block the hook rides over, and the pocket for the
        barb (cut from the whole shell, since the barb can reach past the block into
        the wall)."""
        p = self.p
        r_block, _, _ = self.snap_radii()
        y0 = self.y(self.d_split + p.lip_h + p.lip_clear)
        y1 = self.y(self.d_split + p.snap_len + 1.2)
        block = extrude(self._snap_plane(angle_deg) * Polygon(
            (r_block, y0), (self.R + 1, y0), (self.R + 1, y1), (r_block, y1), align=None), amount=p.snap_w / 2 + 1.0, both=True)
        pocket_pts = self._barb_profile(grow=0.25)
        pocket_pts = [(r_block - 0.5, pocket_pts[0][1]), *pocket_pts, (r_block - 0.5, pocket_pts[-1][1])]
        pocket = extrude(self._snap_plane(angle_deg) * Polygon(*pocket_pts, align=None),
                         amount=p.snap_w / 2 + 0.2, both=True)
        return block & Sphere(self.R - 0.01), pocket

    def _bezel_features(self, bezel):
        p = self.p
        y_seat_end = self.y(self.d_glass + p.lcd_stack + 0.5)
        bezel = bezel - self.to_world(self.lock_cuts())
        g = self.lock_geom()
        skin = math.sqrt(self.R**2 - self.y(g["d_lug0"] - p.lock_clear) ** 2) - g["r_groove"]
        if skin < 0.5:
            self.notes.append(f"twist-lock groove leaves {skin:.2f} mm of wall outside it")
        # magnets: pockets face back, in pads between the glass seat and the split
        ys = self.y(self.d_split)
        rm = self.magnet_r()
        for ang in self.magnet_list():
            pad = self._wall_pad(ang, self.magnet_pad_r_in(), y_seat_end, ys, p.magnet_d + 2.4)
            x, z = rm * math.cos(math.radians(ang)), rm * math.sin(math.radians(ang))
            bezel = bezel + self.to_world(pad)
            bezel = bezel - self.to_world(y_cyl(x, z, ys - p.magnet_h - 0.1, ys + 0.01, p.magnet_d + 0.2))
        if self.magnet_list() and ys - p.magnet_h - 0.1 - y_seat_end < 0.4:
            self.notes.append("magnet pockets leave under 0.4 mm behind them; use thinner magnets")
        for ang in p.snap_angles:
            bezel = bezel + self.to_world(self.snap_hook(ang))
        strain = 1.5 * (p.snap_engage - p.snap_clear) * p.snap_t / p.snap_len**2
        if strain > 0.025:
            self.notes.append(f"snap hooks bend to {strain:.1%} strain going on; PLA wants under ~2.5% "
                              "(longer or thinner hooks, or less engagement)")
        if p.button_pinholes:
            for name, (bx, bz) in self.board_buttons().items():
                if name == "PWR":
                    continue
                a = math.atan2(bz, bx)
                yb = self.y(self.d_pcb_front + p.pcb_t / 2)
                bezel = bezel - self.to_world(cyl_along((bx, yb, bz), (math.cos(a), 0, math.sin(a)), self.R, p.pinhole_d))
        return bezel & self.outer_w

    def port_frame(self):
        p = self.p
        n = self.back_n
        up = Vector(0, -n.Z, n.Y)
        right = Vector(1, 0, 0)
        o = n * self.back_dist + right * p.port_offset_x + up * p.port_offset_z
        a = math.radians(p.port_roll_deg)
        ax_u = right * math.cos(a) + up * math.sin(a)
        return o, ax_u, n

    def port_size(self):
        return self.p.pd_size if self.p.port == "pd" else self.p.fpc_size

    def _back_features(self, back):
        p = self.p
        ys = self.y(self.d_split)
        # magnets: pads just behind the split, pockets face forward
        rm = self.magnet_r()
        for ang in self.magnet_list():
            y1 = ys + p.magnet_h + 1.2
            pad = self._wall_pad(ang, self.magnet_pad_r_in(), ys, y1, p.magnet_d + 2.4)
            x, z = rm * math.cos(math.radians(ang)), rm * math.sin(math.radians(ang))
            back = back + self.to_world(pad)
            back = back - self.to_world(y_cyl(x, z, ys - 1, ys + p.magnet_h + 0.1, p.magnet_d + 0.2))
        for ang in p.snap_angles:
            block, pocket = self.snap_block(ang)
            back = back + self.to_world(block) - self.to_world(pocket)
        # back port: opening, outside recess for the plug overmould, inside sleeve
        o, ax_u, n = self.port_frame()
        w, l, t = self.port_size()
        cut = Plane(origin=o - n * 3, x_dir=ax_u, z_dir=n) * Box(*p.usbc_cut, 10, align=(Align.CENTER, Align.CENTER, Align.MIN))
        recess = Plane(origin=o - n * (p.wall - 0.8), x_dir=ax_u, z_dir=n) * Box(
            *p.plug_recess, 10, align=(Align.CENTER, Align.CENTER, Align.MIN))
        sl_o = Plane(origin=o - n * (p.wall + 6), x_dir=ax_u, z_dir=n) * Box(
            w + 3.2, t + 3.2, 6.5, align=(Align.CENTER, Align.CENTER, Align.MIN))
        sl_i = Plane(origin=o - n * (p.wall + 7), x_dir=ax_u, z_dir=n) * Box(
            w + 0.4, t + 0.4, 7.0, align=(Align.CENTER, Align.CENTER, Align.MIN))
        back = back + (self.to_world(sl_o - sl_i) & self.outer_w)
        back = back - self.to_world(cut) - self.to_world(recess)

        # floor: LED windows, feet, ring locator
        fz = self.floor_z
        top = fz + p.bottom_wall
        oy = p.base_offset_y
        r_led = (p.led_ring_od + p.led_ring_id) / 4
        for i in range(p.led_count):
            a = math.radians(p.led_ring_rot_deg + 360 * i / p.led_count)
            back = back - (Pos(r_led * math.cos(a), oy + r_led * math.sin(a), fz + p.bottom_wall / 2)
                           * Rot(Z=math.degrees(a)) * Box(p.led_window, p.led_window, p.bottom_wall + 2))
        for i in range(3):
            a = math.radians(90 + 120 * i)
            rf = p.bottom_flat_d / 2 - 2.5
            back = back + Pos(rf * math.cos(a), rf * math.sin(a), fz - p.foot_h / 2) * Cylinder(2.0, p.foot_h)
        # the speaker sits on the ring (foam tape between); one wall locates both
        behind_split = self.to_world(y_box(ys + p.lip_h + p.lip_clear, BIG))
        r_wall = p.led_ring_od / 2 + 0.3
        h_wall = p.led_ring_t + min(1.0, p.speaker_lift - 0.1)
        sx, sy, sh = p.speaker_size
        s_bot = top + p.led_ring_t + p.speaker_lift
        # a speaker that fits inside the wall is located by it; a bigger one just sits on the ring
        reach = sx / 2 if p.speaker_round else math.hypot(sx, sy) / 2
        contained = p.speaker_mode == "bottom" and reach + 0.3 < r_wall
        if contained:
            h_wall = p.led_ring_t + p.speaker_lift + sh / 2
        ring_wall = Pos(0, oy, top + h_wall / 2) * (Cylinder(r_wall + 1.1, h_wall) - Cylinder(r_wall, h_wall + 0.1))
        if contained:
            half = sx / 2 if p.speaker_round else max(sx, sy) / 2
            if half + 0.3 < r_wall:  # ribs from the wall in to the speaker's edge
                for i in range(3):
                    a = 90 + 120 * i
                    rib_l = r_wall - (half + 0.3) + 0.5
                    rc = r_wall - rib_l / 2 + 0.25
                    ring_wall = ring_wall + (Pos(rc * math.cos(math.radians(a)), oy + rc * math.sin(math.radians(a)),
                                                 s_bot + sh / 4) * Rot(Z=a) * Box(rib_l, 1.6, sh / 2))
        back = back + (ring_wall & self.outer_w & behind_split)

        if p.speaker_mode == "bottom":
            g_r = p.led_ring_id / 2 - 1.2
            back = self._grille(back, lambda gx, gy: Pos(gx, oy + gy, fz + p.bottom_wall / 2) * Cylinder(
                p.grille_hole_d / 2, p.bottom_wall + 2), g_r)
        elif p.speaker_mode == "back":
            n = self.back_n
            up = Vector(0, -n.Z, n.Y)
            sx, sy, _ = p.speaker_size
            c = n * self.back_dist + Vector(1, 0, 0) * p.speaker_back_pos[0] + up * p.speaker_back_pos[1]
            g_r = min(sx, sy) / 2 - 2
            pl = Plane(origin=c - n * (p.wall + 1), x_dir=(1, 0, 0), z_dir=n)
            back = self._grille(back, lambda gx, gy: self.to_world(pl * Pos(gx, gy, 0) * Cylinder(
                p.grille_hole_d / 2, p.wall + 4, align=(Align.CENTER, Align.CENTER, Align.MIN))), g_r)
        return back

    def _grille(self, back, hole, g_r):
        p = self.p
        k = int(g_r // p.grille_pitch) + 1
        for i in range(-k, k + 1):
            for j in range(-k, k + 1):
                gx = i * p.grille_pitch
                gy = j * p.grille_pitch + (p.grille_pitch / 2 if i % 2 else 0)
                if math.hypot(gx, gy) <= g_r:
                    back = back - hole(gx, gy)
        return back

    # -- arm slots (back shell only) ------------------------------------------------
    def arm_slots(self):
        p = self.p
        ys = self.y(self.d_gear + p.gear_t / 2)
        t = p.arm_t + 2 * p.slot_clear
        sk = None
        for side, (px, pz) in self.pivots.items():
            a0, a1 = self.travel_angles(side)
            r_in = self.stub_len - 0.5
            s = sector(px, pz, r_in, self.stub_len + p.arm_len + 2, a0, a1)
            for a in (a0, a1):
                s = s + arm_outline(px, pz, a, self.stub_len, p.arm_len, p.arm_root_w, p.arm_tip_w,
                                    p.arm_curve_deg if side == "R" else p.arm_curve_deg, grow=p.slot_clear)
            # arms that curve sweep a wider band; cover it by sampling
            if p.arm_curve_deg:
                for i in range(1, 8):
                    a = a0 + (a1 - a0) * i / 8
                    s = s + arm_outline(px, pz, a, self.stub_len, p.arm_len, p.arm_root_w, p.arm_tip_w,
                                        p.arm_curve_deg, grow=p.slot_clear)
            s = s - circle_at(px, pz, 2 * r_in)
            sk = s if sk is None else sk + s
        return slab(sk, ys - t / 2, ys + t / 2)

    # -- display board placeholder ----------------------------------------------------
    def board_xz(self, x, z):
        """Waveshare back-view coordinates -> body (front view), then the board rotation."""
        return rot2(-x, z, self.p.display_rot_deg)

    def board_buttons(self):
        raw = {"PWR": (-23.6, 27.5 - 12.46), "BOOT": (-23.6, 27.5 - 22.93), "RST": (-23.6, 27.5 - 33.60)}
        return {k: self.board_xz(*v) for k, v in raw.items()}

    def board_standoffs(self):
        raw = [(-15.9, 15.8), (21.9, 0.9), (-15.9, -14.2)]  # measured off the drawing, +-1.5 mm
        return [self.board_xz(*v) for v in raw]

    def usb_mouth(self):
        mx, mz = self.board_xz(0, 27.5 - 49.69)
        r = math.hypot(mx, mz)
        return (mx, mz), (mx / r, mz / r)

    def display_board(self):
        p = self.p
        y = self.y
        glass = y_cyl(0, 0, y(self.d_glass), y(self.d_glass + 0.7), p.glass_d)
        lcd = y_cyl(0, 0, y(self.d_glass + 0.7), y(self.d_pcb_front), p.lcd_d)
        pcb = y_cyl(0, 0, y(self.d_pcb_front), y(self.d_pcb_back), p.pcb_d)
        comps = y_cyl(0, 0, y(self.d_pcb_back), y(self.d_p1 - 0.05), p.pcb_d - 4)
        (mx, mz), (ux, uz) = self.usb_mouth()
        yc = y(self.d_pcb_front + p.pcb_t / 2)
        rec = Plane(origin=(mx - ux * 3.7, yc, mz - uz * 3.7), x_dir=(uz, 0, -ux), z_dir=(ux, 0, uz)) * Box(9.0, 3.3, 7.4)
        return glass + lcd + pcb + comps + rec

    def usb_plug(self):
        p = self.p
        (mx, mz), (ux, uz) = self.usb_mouth()
        yc = self.y(self.d_pcb_front + p.pcb_t / 2)
        pl = Plane(origin=(mx, yc, mz), x_dir=(uz, 0, -ux), z_dir=(ux, 0, uz))
        body = pl * Box(p.usb_plug_w, p.usb_plug_t, p.usb_plug_out, align=(Align.CENTER, Align.CENTER, Align.MIN))
        r_c = math.hypot(mx, mz) + p.usb_plug_out / 2
        r_c = min(r_c, self.r_pass - 2.2)
        cable = y_cyl(ux * r_c, uz * r_c, yc + p.usb_plug_t / 2 - 0.5, self.y(self.d_p2 + p.p2_t + 2), 4.0)
        return body + cable

    # -- carriers ------------------------------------------------------------------
    def post_xz(self):
        q = self.p.posts
        return [(q[i], self.p.pivot_z + q[i + 1]) for i in range(0, len(q), 2)]

    def carrier_front(self):
        p = self.p
        y0, y1 = self.y(self.d_p1), self.y(self.d_p1 + p.p1_t)
        r_out = self.r_pass
        # outline: hull of what P1 has to reach (standoffs, twist-lock legs, pin, posts),
        # so the bottom of the body stays free for the base LEDs and speaker
        g = self.lock_geom()
        r_leg = (g["r_leg_in"] + g["r_leg_out"]) / 2
        pts = []
        for sx, sz in self.board_standoffs():
            pts += circle_pts(sx, sz, p.standoff_slot + 3.0, 24)
        for a in p.lock_angles:
            pts += circle_pts(r_leg * math.cos(math.radians(a)), r_leg * math.sin(math.radians(a)), p.leg_w / 2 + 1.0, 24)
        for px, pz in [*self.post_xz(), *self.pivots.values()]:
            pts += circle_pts(px, pz, p.post_d / 2 + 1.5, 24)
        sk = Polygon(*hull2(pts), align=None) & Circle(r_out)
        (mx, mz), (ux, uz) = self.usb_mouth()
        ang = math.degrees(math.atan2(uz, ux))
        sk = sk - rect_at(ux * r_out, uz * r_out, 2 * (r_out - math.hypot(mx, mz) + 3), 34, ang)
        spk = self.board_xz(21.0, -5.0)
        sa = math.degrees(math.atan2(spk[1], spk[0]))
        sk = sk - rect_at(math.cos(math.radians(sa)) * r_out, math.sin(math.radians(sa)) * r_out, 10, 12, sa)
        for ang_m in self.magnet_list():  # room for the back shell's magnet pads to slide past
            a = math.radians(ang_m)
            sk = sk - rect_at(math.cos(a) * r_out, math.sin(a) * r_out, 2 * (r_out - self.magnet_pad_r_in() + 0.6),
                              p.magnet_d + 3.6, ang_m)
        r_hook_in = self.snap_radii()[2]
        for ang_s in p.snap_angles:  # room for the snap hooks and their flex, all through the twist
            for k in range(5):
                a_deg = ang_s + p.lock_twist_deg * k / 4
                a = math.radians(a_deg)
                sk = sk - rect_at(math.cos(a) * r_out, math.sin(a) * r_out, 2 * (r_out - r_hook_in + 1.2), p.snap_w + 2.0, a_deg)
        def standoff_slots(d):
            out = None
            for sx, sz in self.board_standoffs():
                r = math.hypot(sx, sz)
                s_ = rect_at(sx, sz, 2 * p.standoff_slot, d, math.degrees(math.atan2(sz, sx)))
                for s in (-1, 1):
                    s_ = s_ + circle_at(sx + s * p.standoff_slot * sx / r, sz + s * p.standoff_slot * sz / r, d)
                out = s_ if out is None else out + s_
            return out

        sk = sk - standoff_slots(p.screw_clear_d)
        lx, lz = self.pivots["L"]
        sk = sk - circle_at(lx, lz, 2.4)
        plate = slab(sk, y0, y1) + self.lock_legs()
        cb_d, cb_h = p.counterbore
        plate = plate - slab(standoff_slots(cb_d), y1 - cb_h, y1 + 0.1)
        rx, rz = self.pivots["R"]
        y_p2 = self.y(self.d_p2)
        y_gear0, y_gear1 = self.y(self.d_gear), self.y(self.d_gear + p.gear_t)
        if p.bearing:
            bore, _, _ = p.bearing_size
            plate = plate + y_cyl(rx, rz, y1 - 0.1, y_gear0, p.bearing_collar_d)
            plate = plate + y_cyl(rx, rz, y1 - 0.1, y_gear1 + 0.3, bore + p.bearing_pin_fit)
        else:
            plate = plate + y_cyl(rx, rz, y1 - 0.1, y_gear1 + 0.5, p.pin_d)
        for px, pz in self.post_xz():
            plate = plate + (y_cyl(px, pz, y1 - 0.1, y_p2, p.post_d) - y_cyl(px, pz, y1 + 0.5, y_p2 + 0.1, p.screw_pilot_d))
        return plate

    def motor_centre(self):
        lx, lz = self.pivots["L"]
        a = math.radians(self.p.motor_dir_deg)
        return lx + self.p.x27_shaft_offset * math.cos(a), lz + self.p.x27_shaft_offset * math.sin(a)

    def carrier_back(self):
        p = self.p
        y0, y1 = self.y(self.d_p2), self.y(self.d_p2 + p.p2_t)
        mcx, mcz = self.motor_centre()
        ms = p.x27_body_d / 2 + 0.15 + p.sleeve_wall
        pts = circle_pts(mcx, mcz, ms)
        for px, pz in self.post_xz():
            pts += circle_pts(px, pz, p.post_d / 2 + 1.5, 24)
        dx, dz = p.drv_pos
        w, h, _ = p.drv_size
        pts += [(dx - w / 2, dz - h / 2), (dx + w / 2, dz - h / 2), (dx + w / 2, dz + h / 2), (dx - w / 2, dz + h / 2)]
        sk = Polygon(*hull2(pts), align=None)
        lx, lz = self.pivots["L"]
        sk = sk - circle_at(lx, lz, 4.3)
        for px, pz in self.post_xz():
            sk = sk - circle_at(px, pz, p.screw_clear_d)
        # let the port board through
        if p.port:
            o, ax_u, n = self.port_frame()
            w2, l2, t2 = self.port_size()
            port = Plane(origin=o - n * (p.wall + 0.3), x_dir=ax_u, z_dir=-n) * Box(
                w2 + 1.2, t2 + 1.2, l2 + 5, align=(Align.CENTER, Align.CENTER, Align.MIN))
        plate = slab(sk, y0, y1)
        r_in = p.x27_body_d / 2 + 0.15
        plate = plate + (y_cyl(mcx, mcz, y1 - 0.1, y1 + p.sleeve_h, 2 * (r_in + p.sleeve_wall))
                         - y_cyl(mcx, mcz, y1 - 0.2, y1 + p.sleeve_h + 0.1, 2 * r_in))
        plate = plate - port
        return plate & Sphere(self.r_pass)

    def motor_strap(self):
        p = self.p
        a = math.radians(p.motor_dir_deg)
        yb = self.y(self.d_motor_back)
        lx, lz = self.pivots["L"]
        ax, az = math.cos(a), math.sin(a)
        nx, nz = -az, ax
        cx, cz = lx + 6.0 * ax, lz + 6.0 * az
        L = 2 * (p.x27_body_d / 2 + 0.15 + p.sleeve_wall)
        bar = Pos(cx, yb + 0.6, cz) * Rot(Y=-math.degrees(math.atan2(nz, nx))) * Box(L, 1.2, 5)
        for s in (-1, 1):
            bar = bar - y_cyl(cx + s * 13.5 * nx, cz + s * 13.5 * nz, yb - 1, yb + 2, p.x27_peg_d + 0.25)
        return bar

    # -- gears and arms ---------------------------------------------------------------
    def gear(self, side):
        p = self.p
        px, pz = self.pivots[side]
        y0 = self.y(self.d_gear)
        g = involute_gear(p.gear_module, p.gear_teeth, p.gear_backlash)
        if side == "R" and p.gear_teeth % 2 == 0:
            g = Rot(0, 0, 180 / p.gear_teeth) * g
        g = Pos(px, pz) * g
        rest = self.rest_angle(side)
        a = math.radians(rest)
        L = self.stub_len
        stub = rect_at(px + math.cos(a) * L / 2, pz + math.sin(a) * L / 2, L, p.stub_w, rest)
        stub = stub + circle_at(px + math.cos(a) * L, pz + math.sin(a) * L, p.stub_w) - rect_at(
            px + math.cos(a) * (L + p.stub_w / 2), pz + math.sin(a) * (L + p.stub_w / 2), p.stub_w, p.stub_w + 2, rest)
        sk = g + stub
        if p.cw_r > 0:
            ca = a + math.pi
            sk = sk + circle_at(px + p.cw_r * math.cos(ca), pz + p.cw_r * math.sin(ca), p.cw_d)
        if side == "L":
            bore = p.x27_bore_d
        elif p.bearing:
            bore = p.bearing_size[1] + p.bearing_fit
        else:
            bore = p.pin_d + p.pin_clear
        sk = sk - circle_at(px, pz, bore)
        part = slab(sk, y0, y0 + p.gear_t)
        # arm socket in the stub end (flush with the stub's back face so the arm prints flat)
        sx, sz = px + math.cos(a) * (L - p.socket_len / 2), pz + math.sin(a) * (L - p.socket_len / 2)
        sock = slab(rect_at(sx, sz, p.socket_len + 0.02, p.socket_w, rest), y0 + p.gear_t - p.socket_t, y0 + p.gear_t + 0.1)
        return part - sock

    def arm(self, side):
        p = self.p
        px, pz = self.pivots[side]
        rest = self.rest_angle(side)
        a = math.radians(rest)
        y_back = self.y(self.d_gear + p.gear_t)
        y_front = y_back - p.arm_t
        curve = p.arm_curve_deg
        body = slab(arm_outline(px, pz, rest, self.stub_len, p.arm_len, p.arm_root_w, p.arm_tip_w, curve), y_front, y_back)
        tl = p.socket_len - 0.3
        tx, tz = px + math.cos(a) * (self.stub_len - tl / 2), pz + math.sin(a) * (self.stub_len - tl / 2)
        tongue = slab(rect_at(tx, tz, tl + 0.02, p.socket_w - 2 * p.tongue_fit, rest),
                      y_back - p.socket_t + p.tongue_fit, y_back)
        return body + tongue

    # -- other bought parts -----------------------------------------------------------
    def x27(self):
        p = self.p
        mcx, mcz = self.motor_centre()
        lx, lz = self.pivots["L"]
        yf, yb = self.y(self.d_motor), self.y(self.d_motor_back)
        body = y_cyl(mcx, mcz, yf, yb, p.x27_body_d)
        rear = y_cyl(mcx, mcz, yb, yb + p.x27_rear_h, 16.0)
        collar = y_cyl(lx, lz, yf - p.x27_collar_h, yf, 2.0)
        shaft = y_cyl(lx, lz, self.y(self.shaft_tip_depth), yf - p.x27_collar_h + 0.01, 1.0)
        return body + rear + collar + shaft

    def port_board(self):
        p = self.p
        o, ax_u, n = self.port_frame()
        w, l, t = self.port_size()
        return Plane(origin=o - n * (p.wall + 0.3), x_dir=ax_u, z_dir=-n) * Box(w, t, l, align=(Align.CENTER, Align.CENTER, Align.MIN))

    def driver_board(self):
        p = self.p
        w, h, t = p.drv_size
        dx, dz = p.drv_pos
        y1 = self.y(self.d_p2 + p.p2_t)
        return Pos(dx, y1 + p.drv_standoff + t / 2, dz) * Box(w, t, h)

    # -- fasteners, magnets, bearing (named so the assembly lists every part) --------
    def _screw(self, x, z, y_head_back, length):
        """M2 pan-head screw pointing toward the face, head's back face at y_head_back."""
        hd, hh = self.p.screw_head
        head = y_cyl(x, z, y_head_back - hh, y_head_back, hd)
        shank = y_cyl(x, z, y_head_back - hh - length, y_head_back - hh + 0.01, 1.9)
        return head + shank

    def fasteners(self):
        """{name: (shape in body frame, parts it is meant to overlap)}"""
        p = self.p
        out = {}
        cb_d, cb_h = p.counterbore
        y_cb_floor = self.y(self.d_p1 + p.p1_t - cb_h)
        for i, (sx, sz) in enumerate(self.board_standoffs(), 1):
            out[f"screw_m2x4_standoff_{i}"] = (self._screw(sx, sz, y_cb_floor + p.screw_head[1], 4.0),
                                              {"carrier_front", "display_board"})
        y_p2_back = self.y(self.d_p2 + p.p2_t)
        for i, (x, z) in enumerate(self.post_xz(), 1):
            out[f"screw_m2x5_post_{i}"] = (self._screw(x, z, y_p2_back + p.screw_head[1], 5.0),
                                          {"carrier_back", "carrier_front"})
        ys = self.y(self.d_split)
        rm = self.magnet_r()
        for i, ang in enumerate(self.magnet_list(), 1):
            x, z = rm * math.cos(math.radians(ang)), rm * math.sin(math.radians(ang))
            out[f"magnet_bezel_{i}"] = (y_cyl(x, z, ys - p.magnet_h, ys, p.magnet_d), {"bezel"})
            out[f"magnet_back_{i}"] = (y_cyl(x, z, ys, ys + p.magnet_h, p.magnet_d), {"back_shell"})
        if p.bearing:
            bore, od, w = p.bearing_size
            rx, rz = self.pivots["R"]
            y0 = self.y(self.d_gear)
            ring = y_cyl(rx, rz, y0, y0 + w, od) - y_cyl(rx, rz, y0 - 1, y0 + w + 1, bore)
            out["bearing_mr105zz"] = (ring, {"gear_right", "carrier_front"})
        return out

    def led_ring(self):
        p = self.p
        top = self.floor_z + p.bottom_wall
        return Pos(0, p.base_offset_y, top + p.led_ring_t / 2) * (
            Cylinder(p.led_ring_od / 2, p.led_ring_t) - Cylinder(p.led_ring_id / 2, p.led_ring_t + 1))

    def speaker(self):
        p = self.p
        sx, sy, sh = p.speaker_size
        shape = Cylinder(sx / 2, sh) if p.speaker_round else Box(sx, sy, sh)
        if p.speaker_mode == "bottom":
            z0 = self.floor_z + p.bottom_wall + p.led_ring_t + p.speaker_lift
            return Pos(0, p.base_offset_y, z0 + sh / 2) * Rot(Z=p.speaker_rot_deg) * shape
        if p.speaker_mode == "back":
            n = self.back_n
            up = Vector(0, -n.Z, n.Y)
            c = n * (self.back_dist - p.wall - 0.3 - sh / 2) + Vector(1, 0, 0) * p.speaker_back_pos[0] + up * p.speaker_back_pos[1]
            return self.to_world(Plane(origin=c, x_dir=(1, 0, 0), z_dir=n) * shape)
        return None

    # -- build ------------------------------------------------------------------------
    def build(self):
        tw = self.to_world
        bezel, back = self.shells()
        parts = {
            "bezel": bezel,
            "back_shell": back,
            "carrier_front": tw(self.carrier_front()),
            "carrier_back": tw(self.carrier_back()),
            "motor_strap": tw(self.motor_strap()),
            "gear_left": tw(self.gear("L")),
            "gear_right": tw(self.gear("R")),
            "arm_left": tw(self.arm("L")),
            "arm_right": tw(self.arm("R")),
        }
        bought = {
            "display_board": tw(self.display_board()),
            "usb_plug_keepout": tw(self.usb_plug()),
            "x27": tw(self.x27()),
            "port_board": tw(self.port_board()),
            "vid6606_board": tw(self.driver_board()),
            "led_ring": self.led_ring(),
        }
        spk = self.speaker()
        if spk is not None:
            bought["speaker"] = spk
        self.hosts = {}
        for name, (shape, hosts) in self.fasteners().items():
            bought[name] = tw(shape)
            self.hosts[name] = hosts
        return parts, bought


# ----------------------------------------------------------------------------
# Checks
# ----------------------------------------------------------------------------


def mass_props(shape, density, axis_p, axis_d):
    props = GProp_GProps()
    for s in shape.solids():
        BRepGProp.VolumeProperties_s(s.wrapped, props)
    com = props.CentreOfMass()
    I = props.MomentOfInertia(gp_Ax1(gp_Pnt(*axis_p), gp_Dir(*axis_d)))
    rho = density / 1000.0
    return props.Mass() * rho, (com.X(), com.Y(), com.Z()), I * rho  # g, mm, g*mm^2


def torque_report(m: Sultana):
    p = m.p
    tilt = math.radians(p.face_tilt_deg)
    out = {}
    for side in ("L", "R"):
        px, pz = m.pivots[side]
        part = m.gear(side) + m.arm(side)
        yc = m.y(m.d_gear + p.gear_t / 2)
        mass, com, I = mass_props(part, p.pla_density, (px, yc, pz), (0, 1, 0))
        mx, mz = com[0] - px, com[2] - pz
        if p.cw_r > 0 and p.cw_insert_g > 0:
            ca = math.radians(m.rest_angle(side) + 180)
            mx = (mass * mx + p.cw_insert_g * p.cw_r * math.cos(ca)) / (mass + p.cw_insert_g)
            mz = (mass * mz + p.cw_insert_g * p.cw_r * math.sin(ca)) / (mass + p.cw_insert_g)
            I += p.cw_insert_g * p.cw_r**2
            mass += p.cw_insert_g
        rc = math.hypot(mx, mz)
        grav = mass * 9.81 * math.cos(tilt) * rc / 1000.0  # g * m/s^2 * mm -> mNm
        out[side] = {"mass_g": round(mass, 2), "com_from_pivot_mm": round(rc, 1),
                     "inertia_g_mm2": round(I), "gravity_worst_mNm": round(grav, 3)}
    alpha = math.radians(p.accel_limit_dps2)
    I_tot = (out["L"]["inertia_g_mm2"] + out["R"]["inertia_g_mm2"]) * 1e-9  # kg m^2
    inertial = I_tot * alpha * 1000  # mNm
    gl, gr = out["L"]["gravity_worst_mNm"], out["R"]["gravity_worst_mNm"]
    need = gl + (gr + inertial) / p.mesh_eff
    out["both"] = {
        "gravity_worst_mNm": round(gl + gr, 3),
        "inertia_at_accel_limit_mNm": round(inertial, 3),
        "needed_mNm": round(need, 3),
        "margin_vs_datasheet_min": round(p.tau_dyn_min / need, 2),
        "margin_vs_datasheet_typ": round(p.tau_dyn_typ / need, 2),
        "hold_margin": round(p.tau_hold / (gl + gr), 1),
        "note": f"X27 torque is specified at 200 deg/s; the fastest expression peaks at {p.speed_peak_dps:.0f} deg/s",
    }
    return out


def rotate_about(shape, m: Sultana, side, deg):
    px, pz = m.pivots[side]
    return shape.rotate(Axis((px, 0, pz), (0, -1, 0)), deg)


def fit_report(m: Sultana, parts, bought):
    """Interference between parts, over the arm travel, and while the back shell slides on."""
    hits = []
    allp = {**parts, **bought}
    pairs_ok = {
        frozenset(x) for x in [
            ("carrier_front", "gear_right"), ("carrier_front", "carrier_back"), ("x27", "gear_left"),
            ("x27", "carrier_back"), ("carrier_back", "motor_strap"), ("x27", "motor_strap"),
            ("display_board", "usb_plug_keepout"), ("led_ring", "speaker"), ("gear_left", "gear_right"),
            ("gear_left", "arm_left"), ("gear_right", "arm_right"), ("bezel", "display_board"),
            ("bezel", "carrier_front"), ("carrier_front", "display_board"),
        ]
    }
    hosts = getattr(m, "hosts", {})
    names = list(allp)
    bbs = {k: v.bounding_box() for k, v in allp.items()}
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            if frozenset((a, b)) in pairs_ok or b in hosts.get(a, ()) or a in hosts.get(b, ()):
                continue
            if not bbs[a].overlaps(bbs[b]):
                continue
            v = overlap(allp[a], allp[b])
            if v > 0.3:
                hits.append(f"{a} x {b}: {v:.1f}")
    # bezel x display / carrier are touching by design; check they don't overlap much
    for a, b in [("bezel", "display_board"), ("bezel", "carrier_front")]:
        v = overlap(allp[a], allp[b])
        if v > 2.0:
            hits.append(f"{a} x {b}: {v:.1f}")

    # gears + arms over their travel (body frame, then to world)
    p = m.p
    sweep = []
    fixed = {k: v for k, v in allp.items() if not k.startswith(("gear_", "arm_"))}
    lo, hi = p.arm_travel_deg
    for side in ("L", "R"):
        moving = m.gear(side) + m.arm(side)
        for off in (lo, hi):
            deg = off if side == "R" else -off
            moved = m.to_world(rotate_about(moving, m, side, deg))
            for b, sb in fixed.items():
                if side == "L" and b == "x27":
                    continue  # the shaft is a press fit in the bore
                v = overlap(moved, sb)
                if v > 0.3:
                    sweep.append(f"{side} at {off:+.0f} deg x {b}: {v:.1f}")

    # assembly: the back shell slides on along body -Y, starting behind. Only its
    # material inside the pass radius can meet the internals on the way in.
    tilt = m.p.face_tilt_deg
    inner_band = (Rot(X=tilt) * parts["back_shell"]) & (Rot(X=90) * Cylinder(m.r_pass + m.p.assembly_clear, BIG))
    rides_with_back = ("back_shell", "arm_left", "arm_right", "led_ring", "speaker", "port_board")
    internals = [k for k in allp if k != "bezel" and k not in rides_with_back and not k.startswith("magnet_back")]
    inside = Compound(children=[s for k in internals for s in (Rot(X=tilt) * allp[k]).solids()])
    assembly = set()
    ib = inside.bounding_box()
    t = 1.0
    while t < 50:
        moved = Pos(0, t, 0) * inner_band
        if moved.bounding_box().overlaps(ib) and overlap(moved, inside) > 0.3:
            for k in internals:
                if overlap(moved, Rot(X=tilt) * allp[k]) > 0.3:
                    assembly.add(k)
        t += 2.0
    return hits or "none", sweep or "clear", sorted(assembly) or "clear", display_path(m, allp)


def display_path(m: Sultana, allp):
    """The display module (board, plug, front carrier and its screws) goes into the
    bezel turned to the twist-lock's start angle, glass first from behind, then turns
    home. Check both against the bezel and its magnets."""
    p = m.p
    tilt = p.face_tilt_deg
    mod_names = [k for k in allp if k in ("display_board", "usb_plug_keepout", "carrier_front")
                 or k.startswith("screw_m2x4_standoff")]
    module = Compound(children=[s for k in mod_names for s in (Rot(X=tilt) * allp[k]).solids()])
    fixed = Compound(children=[s for k in allp if k == "bezel" or k.startswith("magnet_bezel")
                               for s in (Rot(X=tilt) * allp[k]).solids()])
    axis = Axis((0, 0, 0), (0, -1, 0))
    hits = []
    start = module.rotate(axis, -p.lock_twist_deg)
    fb = fixed.bounding_box()
    t = 0.5
    while t < 30:
        moved = Pos(0, t, 0) * start
        if moved.bounding_box().overlaps(fb):
            v = overlap(moved, fixed)
            if v > 0.5:
                hits.append(f"pushing in, {t:.1f} mm out: {v:.1f}")
        t += 1.0
    for k in range(6):
        ang = -p.lock_twist_deg * (1 - k / 5)
        v = overlap(module.rotate(axis, ang), fixed)
        if v > 0.5:
            hits.append(f"turning, {ang:+.0f} deg: {v:.1f}")
    return hits or "clear"


def summary(m: Sultana, parts):
    p = m.p
    bb = parts["bezel"].bounding_box().add(parts["back_shell"].bounding_box())
    return {
        "sphere_d": p.sphere_d,
        "overall_mm": {"width": round(bb.size.X, 1), "depth": round(bb.size.Y, 1), "height": round(bb.size.Z, 1)},
        "flat_face_d": round(2 * m.face_r, 1),
        "window_d": p.face_window_d,
        "window_vs_body": round(p.face_window_d / p.sphere_d, 2),
        "depths_behind_face": {
            "pcb_front": round(m.d_pcb_front, 2), "split": round(m.d_split, 2), "carrier_front": round(m.d_p1, 2),
            "gears": round(m.d_gear, 2), "motor_plate": round(m.d_p2, 2), "motor_back": round(m.d_motor_back, 2),
            "back_flat": round(m.face_y + m.back_dist, 2),
        },
        "stub_len_from_pivot": round(m.stub_len, 1),
        "arm_reach_from_pivot": round(m.stub_len + p.arm_len, 1),
        "x27_shaft_in_gear_mm": round(m.shaft_engage, 2),
        "pivot_spacing": round(2 * m.pitch_r, 2),
    }


def check(p: Params, quick=False):
    m = Sultana(p)
    parts, bought = m.build()
    rep = {"summary": summary(m, parts), "torque": torque_report(m), "notes": m.notes}
    if not quick:
        hits, sweep, asm, disp = fit_report(m, parts, bought)
        rep["interference_mm3"] = hits
        rep["arm_travel"] = sweep
        rep["back_shell_slide_on"] = asm
        rep["display_module_in"] = disp
    return m, parts, bought, rep


def is_clear(rep):
    return all(rep.get(k) in ("none", "clear")
               for k in ("interference_mm3", "arm_travel", "back_shell_slide_on", "display_module_in"))


# ----------------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------------


def parse_overrides(p: Params, items):
    names = {f.name for f in fields(Params)}
    for item in items or []:
        k, _, v = item.partition("=")
        if k not in names:
            raise SystemExit(f"unknown parameter {k!r} (use --list)")
        cur = getattr(p, k)
        if isinstance(cur, bool):
            val = v.lower() in ("1", "true", "yes", "on")
        elif isinstance(cur, int):
            val = int(v)
        elif isinstance(cur, float):
            val = float(v)
        elif isinstance(cur, tuple):
            val = tuple(float(x) for x in v.split(","))
        else:
            val = v
        setattr(p, k, val)
    return p


def export_all(parts, bought, report, p, out: Path):
    out.mkdir(parents=True, exist_ok=True)
    for name, shape in parts.items():
        export_stl(shape, str(out / f"{name}.stl"), tolerance=0.02, angular_tolerance=0.1)
    children = []
    for name, shape in {**parts, **bought}.items():
        solids = list(shape.solids())
        for i, s in enumerate(solids, 1):
            s.label = name if len(solids) == 1 else f"{name}_{i}"
        c = Compound(children=solids)
        c.label = name
        children.append(c)
    asm = Compound(children=children)
    asm.label = "sultana"
    export_step(asm, str(out / "sultana_assembly.step"))
    (out / "params.json").write_text(json.dumps(asdict(p), indent=2))
    (out / "report.json").write_text(json.dumps(report, indent=2))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--set", nargs="*", metavar="NAME=VALUE", help="override parameters")
    ap.add_argument("--out", default=str(HERE / "out"), help="export directory")
    ap.add_argument("--no-export", action="store_true")
    ap.add_argument("--quick", action="store_true", help="skip the (slow) interference checks")
    ap.add_argument("--render", action="store_true", help="write PNG previews to renders/")
    ap.add_argument("--min-sphere", action="store_true", help="find the smallest sphere_d with no collisions")
    ap.add_argument("--list", action="store_true", help="print all parameters and exit")
    ap.add_argument("--serve", nargs="?", const=8765, type=int, metavar="PORT",
                    help="after exporting, serve the assembly viewer (default port 8765)")
    args = ap.parse_args(argv)

    p = parse_overrides(Params(), args.set)
    if args.list:
        print(json.dumps(asdict(p), indent=2))
        return
    if args.min_sphere:
        lo, hi = 62.0, 90.0
        while hi - lo > 0.5:
            mid = round((lo + hi) / 2, 2)
            try:
                _, _, _, rep = check(replace(p, sphere_d=mid))
                ok = is_clear(rep)
            except SystemExit:
                ok = False
            print(f"sphere_d {mid}: {'fits' if ok else 'collides'}", flush=True)
            lo, hi = (lo, mid) if ok else (mid, hi)
        print(f"smallest sphere_d that fits: {hi}")
        p = replace(p, sphere_d=hi)

    m, parts, bought, report = check(p, quick=args.quick)
    print(json.dumps(report, indent=2))
    if not args.no_export:
        from viewer_export import export_viewer

        export_all(parts, bought, report, p, Path(args.out))
        export_viewer(m, parts, bought, report, Path(args.out))
        print(f"exported to {args.out}")
    if args.render:
        from render import render_all

        render_all(parts, bought, HERE / "renders")
    if args.serve:
        serve(args.serve)


def serve(port: int):
    """Serve cad/ so viewer/index.html can load out/viewer/."""
    import functools
    import http.server

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(HERE))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as httpd:
        print(f"assembly viewer: http://127.0.0.1:{port}/viewer/  (Ctrl-C to stop)", flush=True)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main(sys.argv[1:])
