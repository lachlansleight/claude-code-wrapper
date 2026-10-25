"""PNG previews of the Sultana model (pyvista, off-screen)."""

from __future__ import annotations

import tempfile
from pathlib import Path

import pyvista as pv
from build123d import export_stl

COLOURS = {
    "bezel": "#e9e6df",
    "back_shell": "#dcd8cf",
    "carrier_front": "#7fb3d5",
    "carrier_back": "#5d9cc9",
    "motor_strap": "#5d9cc9",
    "gear_left": "#7ee0c3",
    "gear_right": "#7ee0c3",
    "arm_left": "#7ee0c3",
    "arm_right": "#7ee0c3",
    "display_board": "#1d3557",
    "usb_plug_keepout": "#e76f51",
    "x27": "#f4a261",
    "port_board": "#6a4c93",
    "vid6606_board": "#2a9d8f",
    "led_ring": "#e63946",
    "speaker": "#495057",
}
SHELLS = ("bezel", "back_shell")
OUTSIDE = ("bezel", "back_shell", "arm_left", "arm_right", "display_board")


def to_mesh(shape, tol=0.08):
    # build123d's tessellate() trips on some boolean faces; the STL writer meshes them fine
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "part.stl"
        export_stl(shape, str(path), tolerance=tol, angular_tolerance=0.2)
        mesh = pv.read(str(path)).clean()
        return mesh.compute_normals(split_vertices=True, feature_angle=35, auto_orient_normals=False)


def _plot(meshes, path, camera, clip=None, opacity=None, size=(1200, 900), parallel=False):
    pl = pv.Plotter(off_screen=True, window_size=size)
    pl.set_background("#2b2b2b")
    for name, mesh in meshes.items():
        m = mesh
        if clip is not None:
            m = m.clip(normal=clip[0], origin=clip[1], invert=True)
            if m.n_points == 0:
                continue
        pl.add_mesh(m, color=COLOURS.get(name, "#cccccc"), opacity=(opacity or {}).get(name, 1.0),
                    smooth_shading=True, specular=0.2)
    pl.enable_anti_aliasing("ssaa")
    pl.camera_position = camera
    if parallel:
        pl.enable_parallel_projection()
    pl.screenshot(str(path))
    pl.close()


def render_all(parts, bought, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    meshes = {k: to_mesh(v) for k, v in {**parts, **bought}.items()}
    outside = {k: meshes[k] for k in OUTSIDE if k in meshes}
    d = 190
    views = {
        "front": [(20, -d, 25), (0, 0, 0), (0, 0, 1)],
        "front_34": [(-0.7 * d, -0.75 * d, 0.45 * d), (0, 0, 0), (0, 0, 1)],
        "back_34": [(0.7 * d, 0.75 * d, 0.35 * d), (0, 0, 0), (0, 0, 1)],
        "below": [(0.4 * d, 0.3 * d, -0.9 * d), (0, 0, 0), (0, 0, 1)],
    }
    for name, cam in views.items():
        _plot(outside, out_dir / f"{name}.png", cam)
    ghost = {k: 0.15 for k in SHELLS}
    _plot(meshes, out_dir / "internals_34.png", views["back_34"], opacity=ghost)
    _plot(meshes, out_dir / "section_side.png", [(d, 0, 0), (0, 0, 0), (0, 0, 1)], clip=("x", (0, 0, 0)), parallel=True)
    _plot(meshes, out_dir / "section_top.png", [(0, 0, d), (0, 0, 0), (0, 1, 0)], clip=("z", (0, 0, 4)), parallel=True)
    _plot(meshes, out_dir / "section_back.png", [(0, d, 0), (0, 0, 0), (0, 0, 1)], clip=("y", (0, 4, 0)), parallel=True)
    print(f"renders in {out_dir}")
