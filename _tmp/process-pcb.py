#!/usr/bin/env python3
"""Generate per-track separation paths and a composite PCB preview."""

import json
import mmap
import os
from pathlib import Path
import shutil
import struct
import subprocess


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
DATA_DIR = SCRIPT_DIR / "data"
TRACKS_PDF = SCRIPT_DIR / "two-sources-B_Cu.pdf"
EDGE_PDF = SCRIPT_DIR / "two-sources-Edge_Cuts.pdf"
RESOLUTION = 70
RADII_MM = (0.4, 0.8)
COLORS = {0.4: "0077cc", 0.8: "e64a19"}
OPTIMIZE_MAX_ERROR = 1.0
COMPOSITE_MIN_RADIUS = 3.0
COMPOSITE_MAX_RADIUS = 100.0
HEADER_SIZE = 64


def run(*command: str | Path) -> None:
    subprocess.run([str(argument) for argument in command], check=True, env=ENVIRONMENT)


def run_tsx(script: str, *arguments: str | Path) -> None:
    run("npx", "tsx", ROOT_DIR / script, *arguments)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def file_reference(path: Path) -> str:
    return f"#FILE#{path.resolve()}"


def output_file(response_path: Path, key: str) -> Path:
    value = read_json(response_path).get(key)
    if not isinstance(value, str) or not value.startswith("#FILE#"):
        raise RuntimeError(f"{response_path}: response has no binary {key!r} output")
    return Path(value.removeprefix("#FILE#"))


def invoke_core(request_path: Path, response_path: Path) -> dict:
    run_tsx("src/cucutcli.ts", response_path, request_path)
    return read_json(response_path)


def raster_dimensions(path: Path) -> tuple[int, int, int, int]:
    with path.open("rb") as raster:
        header = raster.read(HEADER_SIZE)
        raster_size = path.stat().st_size
    if len(header) < HEADER_SIZE:
        raise RuntimeError(f"{path}: raster is missing its header")
    width, height, stride, pixel_bytes = struct.unpack_from("<iiii", header)
    if width <= 0 or height <= 0 or pixel_bytes <= 0 or stride < width * pixel_bytes:
        raise RuntimeError(f"{path}: invalid raster header")
    if raster_size != HEADER_SIZE + stride * height:
        raise RuntimeError(f"{path}: raster size does not match its header")
    return width, height, stride, pixel_bytes


def label_raster(bitmap_path: Path, stem: str) -> tuple[Path, Path, dict]:
    request_path = DATA_DIR / f"{stem}-request.json"
    response_path = DATA_DIR / f"{stem}-response.json"
    write_json(
        request_path,
        {"command": "labeler", "bitmap": file_reference(bitmap_path)},
    )
    response = invoke_core(request_path, response_path)
    labeled_path = DATA_DIR / f"{stem}.raster"
    shutil.copy2(output_file(response_path, "labeled"), labeled_path)
    return labeled_path, response_path, response


def run_edt(label_path: Path, object_index: int, stem: str) -> Path:
    request_path = DATA_DIR / f"{stem}-request.json"
    response_path = DATA_DIR / f"{stem}-response.json"
    write_json(
        request_path,
        {
            "command": "edt",
            "bitmap": file_reference(label_path),
            "objectCondition": "==",
            "objectValue": object_index,
        },
    )
    invoke_core(request_path, response_path)
    return output_file(response_path, "distance")


def find_hole_start(
    label_path: Path,
    distance_path: Path,
    hole: dict,
    radius: float,
) -> dict | None:
    width, height, label_stride, label_bytes = raster_dimensions(label_path)
    distance_width, distance_height, distance_stride, distance_bytes = raster_dimensions(distance_path)
    if (width, height) != (distance_width, distance_height):
        raise RuntimeError("EDT dimensions do not match the label raster")
    if label_bytes != 2 or distance_bytes != 4:
        raise RuntimeError("Expected 16-bit labels and 32-bit EDT values")

    bbox = hole["bbox"]
    x0 = max(0, bbox["begin"]["x"])
    y0 = max(0, bbox["begin"]["y"])
    x1 = min(width, bbox["end"]["x"])
    y1 = min(height, bbox["end"]["y"])
    hole_index = hole["index"]
    best_point = None
    best_distance = radius
    with label_path.open("rb") as label_file, distance_path.open("rb") as distance_file:
        with mmap.mmap(label_file.fileno(), 0, access=mmap.ACCESS_READ) as labels:
            with mmap.mmap(distance_file.fileno(), 0, access=mmap.ACCESS_READ) as distances:
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        label_offset = HEADER_SIZE + y * label_stride + x * label_bytes
                        if struct.unpack_from("<h", labels, label_offset)[0] != hole_index:
                            continue
                        distance_offset = HEADER_SIZE + y * distance_stride + x * distance_bytes
                        distance = struct.unpack_from("<f", distances, distance_offset)[0]
                        if 0 < distance < best_distance:
                            best_point = {"x": x, "y": y}
                            best_distance = distance
    return best_point


def create_separation(
    distance_path: Path,
    component: dict,
    stem: str,
    radius_mm: float,
    edge: str,
    direction: str,
    starting_point: dict,
) -> tuple[Path, Path]:
    radius_pixels = radius_mm * RESOLUTION
    request_path = DATA_DIR / f"{stem}-request.json"
    response_path = DATA_DIR / f"{stem}-response.json"
    write_json(
        request_path,
        {
            "command": "separation",
            "distance": file_reference(distance_path),
            "radius": radius_pixels,
            "direction": direction,
            "edge": edge,
            "startingPoint": starting_point,
        },
    )
    invoke_core(request_path, response_path)
    path = output_file(response_path, "path")

    optimize_request = DATA_DIR / f"{stem}-optimize-request.json"
    optimize_response = DATA_DIR / f"{stem}-optimize-response.json"
    write_json(
        optimize_request,
        {
            "command": "optimizePath",
            "path": file_reference(path),
            "maxError": OPTIMIZE_MAX_ERROR,
        },
    )
    invoke_core(optimize_request, optimize_response)
    indices_path = output_file(optimize_response, "indices")

    radius_label = f"{radius_mm:.1f}"
    diameter_label = f"{2 * radius_mm:.1f}"
    boundary_suffix = "" if edge == "outer" else f"-hole-{abs(component['index'])}"
    separation_buffer = DATA_DIR / f"cu-sep-{component['owner']}{boundary_suffix}-{diameter_label}.buffer"
    composite_buffer = DATA_DIR / f"cu-comp-sep-{component['owner']}{boundary_suffix}-{radius_label}.buffer"
    shutil.copy2(path, separation_buffer)

    composite_request = DATA_DIR / f"{stem}-composite-request.json"
    composite_response = DATA_DIR / f"{stem}-composite-response.json"
    write_json(
        composite_request,
        {
            "command": "compositePath",
            "path": file_reference(path),
            "indices": file_reference(indices_path),
            "maxError": OPTIMIZE_MAX_ERROR,
            "minRadius": COMPOSITE_MIN_RADIUS,
            "maxRadius": COMPOSITE_MAX_RADIUS,
        },
    )
    invoke_core(composite_request, composite_response)
    shutil.copy2(output_file(composite_response, "path"), composite_buffer)
    print(f"Created {separation_buffer.name} and {composite_buffer.name}")
    return separation_buffer, composite_buffer


def create_preview(source_png: Path, path: Path, output_png: Path, *, composite: bool, color: str) -> None:
    filter_name = "composite-path" if composite else "path"
    arguments: list[str | Path] = [
        "-o", output_png,
        "-f", filter_name,
        "--filter-background", source_png,
    ]
    if composite:
        arguments.extend(["--filter-arc", color, "--filter-line", color])
    else:
        arguments.extend(["--filter-color", color, "--filter-line", color])
    arguments.append(path)
    run_tsx("core/script/buffer2png.ts", *arguments)


def main() -> None:
    for pdf_path in (TRACKS_PDF, EDGE_PDF):
        if not pdf_path.is_file():
            raise SystemExit(f"Input PDF not found: {pdf_path}")
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    run("make", "-f", ROOT_DIR / "core/Makefile.cli.mk", "-C", ROOT_DIR)
    tracks_pdf = DATA_DIR / TRACKS_PDF.name
    edge_pdf = DATA_DIR / EDGE_PDF.name
    shutil.copy2(TRACKS_PDF, tracks_pdf)
    shutil.copy2(EDGE_PDF, edge_pdf)
    run_tsx("core/script/pdf2raster.ts", str(RESOLUTION), tracks_pdf, edge_pdf)
    tracks_raster = DATA_DIR / "cu.raster"
    edge_raster = DATA_DIR / "edge.raster"
    (DATA_DIR / f"{tracks_pdf.stem}.raster").replace(tracks_raster)
    (DATA_DIR / f"{edge_pdf.stem}.raster").replace(edge_raster)

    run_tsx("core/script/raster2png.ts", "-o", DATA_DIR / "cu.png", tracks_raster)
    run_tsx("core/script/raster2png.ts", "-o", DATA_DIR / "edge.png", edge_raster)

    cu_labeler, cu_labeler_response, cu_result = label_raster(tracks_raster, "cu-labeler")
    edge_labeler, edge_labeler_response, _ = label_raster(edge_raster, "edge-labeler")
    cu_labeler_png = DATA_DIR / "cu-labeler.png"
    edge_labeler_png = DATA_DIR / "edge-labeler.png"
    run_tsx("core/script/raster2png.ts", "-o", cu_labeler_png, cu_labeler)
    run_tsx("core/script/raster2png.ts", "-o", edge_labeler_png, edge_labeler)

    collapse_request = DATA_DIR / "edge-collapse-request.json"
    collapse_response = DATA_DIR / "edge-collapse-response.json"
    write_json(
        collapse_request,
        {
            "command": "collapseObjects",
            "labeled": file_reference(edge_labeler),
            "info": read_json(edge_labeler_response)["info"],
        },
    )
    invoke_core(collapse_request, collapse_response)
    edge_collapsed = DATA_DIR / "edge-callapsed.raster"
    shutil.copy2(output_file(collapse_response, "collapsed"), edge_collapsed)
    edge_collapsed_png = DATA_DIR / "edge-callapsed.png"
    run_tsx("core/script/raster2png.ts", "-o", edge_collapsed_png, edge_collapsed)

    track_infos = cu_result.get("info")
    if not isinstance(track_infos, list):
        raise RuntimeError("cu-labeler response does not contain an info array")
    all_composites: list[tuple[Path, float]] = []
    for track in track_infos:
        track_index = track["index"]
        edt_stem = f"cu-edt-{track_index}"
        distance_path = run_edt(cu_labeler, track_index, edt_stem)
        edt_preview = DATA_DIR / f"{edt_stem}.png"
        run_tsx("core/script/raster2png.ts", "-f", "distance", "-o", edt_preview, distance_path)

        owner = {**track, "owner": track_index}
        for radius_mm in RADII_MM:
            radius_pixels = radius_mm * RESOLUTION
            _, composite = create_separation(
                distance_path,
                owner,
                f"cu-sep-{track_index}-{2 * radius_mm:.1f}",
                radius_mm,
                "outer",
                "ccw",
                track["topLeft"],
            )
            separation = DATA_DIR / f"cu-sep-{track_index}-{2 * radius_mm:.1f}.buffer"
            color = COLORS[radius_mm]
            create_preview(
                cu_labeler_png,
                separation,
                DATA_DIR / f"{separation.stem}.png",
                composite=False,
                color=color,
            )
            create_preview(
                cu_labeler_png,
                composite,
                DATA_DIR / f"{composite.stem}.png",
                composite=True,
                color=color,
            )
            all_composites.append((composite, 2 * radius_pixels))

        for hole in track.get("holes", []):
            hole_component = {**hole, "owner": track_index}
            for radius_mm in RADII_MM:
                radius_pixels = radius_mm * RESOLUTION
                start = find_hole_start(cu_labeler, distance_path, hole, radius_pixels)
                if start is None:
                    print(
                        f"Warning: no starting pixel for hole {hole['index']} at "
                        f"R={radius_mm} mm; skipping this hole radius"
                    )
                    continue
                _, composite = create_separation(
                    distance_path,
                    hole_component,
                    f"cu-sep-{track_index}-hole-{abs(hole['index'])}-{2 * radius_mm:.1f}",
                    radius_mm,
                    "inner",
                    "cw",
                    start,
                )
                separation = DATA_DIR / (
                    f"cu-sep-{track_index}-hole-{abs(hole['index'])}-{2 * radius_mm:.1f}.buffer"
                )
                color = COLORS[radius_mm]
                create_preview(
                    cu_labeler_png,
                    separation,
                    DATA_DIR / f"{separation.stem}.png",
                    composite=False,
                    color=color,
                )
                create_preview(
                    cu_labeler_png,
                    composite,
                    DATA_DIR / f"{composite.stem}.png",
                    composite=True,
                    color=color,
                )
                all_composites.append((composite, 2 * radius_pixels))

    svg_arguments: list[str | Path] = [
        "--res", str(RESOLUTION),
        "-o", DATA_DIR / "cu-separations.svg",
        "-f", "png", "--filter-opacity", "100", cu_labeler_png,
        "-f", "png", "--filter-opacity", "40", edge_collapsed_png,
    ]
    for composite, diameter_pixels in all_composites:
        radius_mm = diameter_pixels / (2 * RESOLUTION)
        svg_arguments.extend(
            [
                "-f", "composite-path",
                "--filter-color", COLORS[radius_mm],
                "--filter-width", str(diameter_pixels),
                composite,
            ]
        )
    run_tsx("core/script/buffer2svg.ts", *svg_arguments)
    print(f"Finished. All outputs and PNG previews are in {DATA_DIR}")


ENVIRONMENT = os.environ.copy()
ENVIRONMENT["PATH"] = (
    str(Path.home() / ".nvm/versions/node/v22.19.0/bin")
    + os.pathsep
    + ENVIRONMENT.get("PATH", "")
)


if __name__ == "__main__":
    main()
