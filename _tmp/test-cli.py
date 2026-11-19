#!/usr/bin/env python3
"""Exercise the CLI labeler, EDT, separation, and path optimization commands."""

import json
import math
import os
from pathlib import Path
import struct
import subprocess


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
TRACKS_PDF = SCRIPT_DIR / "two-sources-B_Cu.pdf"
CUTTING_PDF = SCRIPT_DIR / "two-sources-Edge_Cuts.pdf"
TRACKS_RASTER = SCRIPT_DIR / "cu-source.raster"
CUTTING_RASTER = SCRIPT_DIR / "cut-source.raster"
HEADER_SIZE = 64


def run(*command: str) -> None:
    subprocess.run(command, check=True, env=ENVIRONMENT)


def run_tsx(script: str, *arguments: Path | str) -> None:
    run("npx", "tsx", str(ROOT_DIR / script), *(str(argument) for argument in arguments))


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def file_reference(path: Path) -> str:
    return f"#FILE#{path.resolve()}"


def raster_header(data: bytes) -> tuple[int, int, int, int]:
    assert len(data) >= HEADER_SIZE, "Raster is missing its header"
    return struct.unpack_from("<iiii", data)


def pixel_offset(x: int, y: int, stride: int, pixel_bytes: int) -> int:
    return HEADER_SIZE + y * stride + x * pixel_bytes


def component_holes(info: list[dict]) -> list[tuple[int, int, dict]]:
    return [
        (hole["pixelCount"], component["index"], hole)
        for component in info
        for hole in component["holes"]
    ]


def validate_labeler(response_path: Path, raster_path: Path) -> None:
    response = read_json(response_path)
    info = response.get("info")
    assert isinstance(info, list), response
    assert response.get("labeled", "").endswith(f"{response_path.stem}-1.raw"), response

    raster = raster_path.read_bytes()
    width, height, stride, pixel_bytes = raster_header(raster)
    assert width > 0 and height > 0 and pixel_bytes == 2 and stride >= width * pixel_bytes
    assert len(raster) == HEADER_SIZE + stride * height, "Output raster size does not match its header"

    counts = [0] * len(info)
    minimums = [[width, height] for _ in info]
    maximums = [[-1, -1] for _ in info]
    top_rows = [height] * len(info)
    top_lefts = [width] * len(info)
    hole_info = {}
    for item in info:
        holes = item.get("holes")
        assert isinstance(holes, list), item
        for hole in holes:
            hole_index = hole["index"]
            assert hole_index <= -2 and hole_index not in hole_info, hole
            hole_info[hole_index] = {
                "info": hole,
                "count": 0,
                "minimum": [width, height],
                "maximum": [-1, -1],
            }

    for y in range(height):
        for x in range(width):
            label = struct.unpack_from(
                "<h", raster, pixel_offset(x, y, stride, pixel_bytes)
            )[0]
            if label == -1:
                continue
            if label >= 0:
                assert label < len(info), f"Unexpected label {label} at ({x}, {y})"
                counts[label] += 1
                minimums[label][0] = min(minimums[label][0], x)
                minimums[label][1] = min(minimums[label][1], y)
                maximums[label][0] = max(maximums[label][0], x + 1)
                maximums[label][1] = max(maximums[label][1], y + 1)
                if y < top_rows[label] or (y == top_rows[label] and x < top_lefts[label]):
                    top_rows[label], top_lefts[label] = y, x
            else:
                assert label in hole_info, f"Unreported hole label {label} at ({x}, {y})"
                hole = hole_info[label]
                hole["count"] += 1
                hole["minimum"][0] = min(hole["minimum"][0], x)
                hole["minimum"][1] = min(hole["minimum"][1], y)
                hole["maximum"][0] = max(hole["maximum"][0], x + 1)
                hole["maximum"][1] = max(hole["maximum"][1], y + 1)

    for index, item in enumerate(info):
        assert item["index"] == index
        assert item["pixelCount"] == counts[index] and counts[index] > 0, item
        assert item["topLeft"] == {"x": top_lefts[index], "y": top_rows[index]}, item
        assert item["bbox"] == {
            "begin": {"x": minimums[index][0], "y": minimums[index][1]},
            "end": {"x": maximums[index][0], "y": maximums[index][1]},
        }, item

    for index, hole in hole_info.items():
        item = hole["info"]
        assert item["pixelCount"] == hole["count"] and hole["count"] > 0, item
        assert item["bbox"] == {
            "begin": {"x": hole["minimum"][0], "y": hole["minimum"][1]},
            "end": {"x": hole["maximum"][0], "y": hole["maximum"][1]},
        }, item
        location = item["location"]
        assert struct.unpack_from(
            "<h", raster, pixel_offset(location["x"], location["y"], stride, pixel_bytes)
        )[0] == index

    assert info, "Expected the sample PCB raster to contain labeled components"
    print(f"Validated labeler response: {width}x{height}, {len(info)} labels, {len(hole_info)} holes")


def make_collapse_request(input_path: Path, raster_path: Path, labeler_response_path: Path) -> None:
    write_json(
        input_path,
        {
            "command": "collapseObjects",
            "labeled": file_reference(raster_path),
            "info": read_json(labeler_response_path)["info"],
        },
    )


def validate_collapse(response_path: Path, raster_path: Path) -> None:
    response = read_json(response_path)
    assert response.get("collapsed", "").endswith(f"{response_path.stem}-1.raw"), response
    raster = raster_path.read_bytes()
    width, height, stride, pixel_bytes = raster_header(raster)
    assert width > 0 and height > 0 and pixel_bytes == 2 and stride >= width * pixel_bytes
    assert len(raster) == HEADER_SIZE + stride * height, "Collapsed raster size does not match its header"
    labels = [
        struct.unpack_from("<h", raster, pixel_offset(x, y, stride, pixel_bytes))[0]
        for y in range(height)
        for x in range(width)
    ]
    assert all(label < 0 for label in labels), "Collapsed raster still contains object labels"
    print(f"Validated collapsed edge raster: {width}x{height}")


def make_labeler_request(input_path: Path, raster_path: Path) -> None:
    write_json(
        input_path,
        {
            "command": "labeler",
            "bitmap": file_reference(raster_path),
        },
    )


def make_edt_request(input_path: Path, raster_path: Path, track_index: int) -> None:
    write_json(
        input_path,
        {
            "command": "edt",
            "bitmap": file_reference(raster_path),
            "objectCondition": "==",
            "objectValue": track_index,
        },
    )


def make_track_edt_request(input_path: Path, raster_path: Path, response_path: Path) -> None:
    response = read_json(response_path)
    info = response.get("info")
    assert isinstance(info, list) and info, response
    make_edt_request(input_path, raster_path, len(info) // 2)


def make_hole_edt_request(input_path: Path, raster_path: Path, response_path: Path) -> bool:
    holes = component_holes(read_json(response_path)["info"])
    if not holes:
        print("Warning: The labeler output contains no holes; skipping hole-related processing.")
        return False
    _, track_index, _ = max(holes, key=lambda item: item[0])
    make_edt_request(input_path, raster_path, track_index)
    return True


def validate_edt(response_path: Path, raster_path: Path) -> None:
    response = read_json(response_path)
    assert response.get("distance", "").endswith(f"{response_path.stem}-1.raw"), response
    raster = raster_path.read_bytes()
    width, height, stride, pixel_bytes = raster_header(raster)
    assert width > 0 and height > 0 and pixel_bytes == 4 and stride >= width * pixel_bytes
    assert len(raster) == HEADER_SIZE + stride * height, "EDT raster size does not match its header"
    distances = [
        struct.unpack_from("<f", raster, pixel_offset(x, y, stride, pixel_bytes))[0]
        for y in range(height)
        for x in range(width)
    ]
    assert any(distance == 0 for distance in distances), "Expected object pixels at distance zero"
    assert all(math.isfinite(distance) and distance >= 0 for distance in distances)
    print(f"Validated EDT response: {width}x{height}")


def make_separation_request(
    input_path: Path, distance_path: Path, response_path: Path
) -> None:
    info = read_json(response_path)["info"]
    track = info[len(info) // 2]
    write_json(
        input_path,
        {
            "command": "separation",
            "distance": file_reference(distance_path),
            "offset": {"x": 0, "y": 0},
            "radius": 20,
            "direction": "ccw",
            "edge": "outer",
            "startingPoint": track["topLeft"],
        },
    )


def make_optimize_path_request(input_path: Path, path_path: Path, max_error: float) -> None:
    write_json(
        input_path,
        {
            "command": "optimizePath",
            "path": file_reference(path_path),
            "maxError": max_error,
        },
    )


def make_composite_path_request(
    input_path: Path,
    original_path: Path,
    indices_path: Path,
    max_error: float,
    min_radius: float,
    max_radius: float,
) -> None:
    write_json(
        input_path,
        {
            "command": "compositePath",
            "path": file_reference(original_path),
            "indices": file_reference(indices_path),
            "maxError": max_error,
            "minRadius": min_radius,
            "maxRadius": max_radius,
        },
    )


def validate_composite_path(response_path: Path, path_path: Path) -> None:
    response = read_json(response_path)
    output_stem = response_path.stem
    assert response.get("path", "").endswith(f"{output_stem}-1.raw"), response
    data = path_path.read_bytes()
    assert len(data) >= 14 * 4 and len(data) % 4 == 0
    values = struct.unpack(f"<{len(data) // 4}f", data)
    assert (len(values) - 2) % 4 == 0
    segment_count = (len(values) - 2) // 4
    assert segment_count >= 3
    for segment in range(segment_count):
        offset = segment * 4
        x0, y0, center_x, center_y, x1, y1 = values[offset : offset + 6]
        assert math.isfinite(x0) and math.isfinite(y0) and math.isfinite(x1) and math.isfinite(y1)
        if math.isnan(center_x) and math.isnan(center_y):
            continue
        assert math.isfinite(center_x) and math.isfinite(center_y)
        radius0 = math.hypot(x0 - center_x, y0 - center_y)
        radius1 = math.hypot(x1 - center_x, y1 - center_y)
        assert math.isclose(radius0, radius1, rel_tol=1e-5, abs_tol=1e-4)
    assert values[-2:] == values[:2], "Composite path does not close"
    print(f"Validated composite path: {segment_count} segments")


def validate_optimized_path(
    response_path: Path,
    path_path: Path,
    indices_path: Path,
    original_path: Path,
) -> None:
    response = read_json(response_path)
    output_stem = response_path.stem
    assert response.get("path", "").endswith(f"{output_stem}-2.raw"), response
    assert response.get("indices", "").endswith(f"{output_stem}-1.raw"), response
    original_data = original_path.read_bytes()
    optimized_data = path_path.read_bytes()
    index_data = indices_path.read_bytes()
    assert len(original_data) > 0 and len(original_data) % 8 == 0
    assert len(optimized_data) > 0 and len(optimized_data) % 8 == 0
    assert len(index_data) == len(optimized_data) // 2, "Index count does not match optimized points"
    original_count = len(original_data) // 8
    optimized_count = len(optimized_data) // 8
    coordinates = struct.unpack(f"<{len(optimized_data) // 4}f", optimized_data)
    indices = struct.unpack(f"<{len(index_data) // 4}i", index_data)
    assert all(math.isfinite(value) for value in coordinates)
    assert all(0 <= index < original_count for index in indices)
    assert tuple(sorted(set(indices))) == indices, "Mapped indices must be unique and ordered"
    assert optimized_count < original_count, "Expected redundant path points to be removed"
    print(f"Validated optimized path: {optimized_count} of {original_count} points")


def validate_separation(response_path: Path, path_path: Path, *, inner: bool = False) -> None:
    response = read_json(response_path)
    assert response.get("path", "").endswith(f"{response_path.stem}-1.raw"), response
    path = path_path.read_bytes()
    message = "Inner separation path" if inner else "Separation path"
    assert len(path) >= 2 * 2 * 4 and len(path) % (2 * 4) == 0, f"{message} is missing or malformed"
    points = struct.unpack(f"<{len(path) // 4}f", path)
    assert all(math.isfinite(value) for value in points), f"{message} contains invalid coordinates"
    assert len(points) // 2 > 8, f"{message} is unexpectedly short"
    description = "inner hole separation path" if inner else "separation path"
    print(f"Validated {description}: {len(points) // 2} points")


def make_hole_separation_request(
    input_path: Path,
    distance_path: Path,
    label_path: Path,
    response_path: Path,
) -> None:
    holes = component_holes(read_json(response_path)["info"])
    assert holes, "Expected the labeler output to contain a hole"
    _, _, largest_hole = max(holes, key=lambda item: item[0])

    labels = label_path.read_bytes()
    distances = distance_path.read_bytes()
    width, height, label_stride, label_bytes = raster_header(labels)
    distance_width, distance_height, distance_stride, distance_bytes = raster_header(distances)
    assert (distance_width, distance_height) == (width, height), "Hole EDT dimensions do not match label raster"
    assert label_bytes == 2 and distance_bytes == 4
    assert len(labels) == HEADER_SIZE + label_stride * height
    assert len(distances) == HEADER_SIZE + distance_stride * height

    hole_index = largest_hole["index"]
    starting_point = None
    smallest_positive_distance = float("inf")
    for y in range(height):
        for x in range(width):
            label = struct.unpack_from(
                "<h", labels, pixel_offset(x, y, label_stride, label_bytes)
            )[0]
            if label != hole_index:
                continue
            distance = struct.unpack_from(
                "<f", distances, pixel_offset(x, y, distance_stride, distance_bytes)
            )[0]
            if 0 < distance < 20 and distance < smallest_positive_distance:
                starting_point = {"x": x, "y": y}
                smallest_positive_distance = distance

    assert starting_point is not None, "Largest hole has no pixel within the requested inner radius"
    write_json(
        input_path,
        {
            "command": "separation",
            "distance": file_reference(distance_path),
            "offset": {"x": 0, "y": 0},
            "radius": 20,
            "direction": "ccw",
            "edge": "inner",
            "startingPoint": starting_point,
        },
    )
    print(
        f"Selected largest hole {hole_index} with "
        f"{largest_hole['pixelCount']} pixels at {starting_point}"
    )


def main() -> None:
    for pdf_path in (TRACKS_PDF, CUTTING_PDF):
        if not pdf_path.is_file():
            raise SystemExit(f"Input PDF not found: {pdf_path}")

    run("make", "-f", str(ROOT_DIR / "core/Makefile.cli.mk"), "-C", str(ROOT_DIR))
    run_tsx("core/script/pdf2raster.ts", "50", TRACKS_PDF, CUTTING_PDF)
    TRACKS_PDF.with_suffix(".raster").replace(TRACKS_RASTER)
    CUTTING_PDF.with_suffix(".raster").replace(CUTTING_RASTER)
    tracks_dimensions = raster_header(TRACKS_RASTER.read_bytes())[:2]
    cutting_dimensions = raster_header(CUTTING_RASTER.read_bytes())[:2]
    assert tracks_dimensions == cutting_dimensions, (
        f"Converted PDF dimensions differ: {tracks_dimensions} != {cutting_dimensions}"
    )

    tracks_image = SCRIPT_DIR / "cu-source.png"
    cutting_image = SCRIPT_DIR / "cut-source.png"
    run_tsx("core/script/raster2png.ts", "-o", tracks_image, TRACKS_RASTER)
    run_tsx("core/script/raster2png.ts", "-o", cutting_image, CUTTING_RASTER)

    tracks_labeler_input = SCRIPT_DIR / "cu-tracks-labeler-request.json"
    tracks_labeler_output = SCRIPT_DIR / "cu-tracks-labeler-response.json"
    tracks_labeled_raster = SCRIPT_DIR / "cu-tracks-labeler-response-1.raw"
    make_labeler_request(tracks_labeler_input, TRACKS_RASTER)
    run_tsx("src/cucutcli.ts", tracks_labeler_output, tracks_labeler_input)
    validate_labeler(tracks_labeler_output, tracks_labeled_raster)

    cutting_labeler_input = SCRIPT_DIR / "cut-edges-labeler-request.json"
    cutting_labeler_output = SCRIPT_DIR / "cut-edges-labeler-response.json"
    cutting_labeled_raster = SCRIPT_DIR / "cut-edges-labeler-response-1.raw"
    make_labeler_request(cutting_labeler_input, CUTTING_RASTER)
    run_tsx("src/cucutcli.ts", cutting_labeler_output, cutting_labeler_input)
    validate_labeler(cutting_labeler_output, cutting_labeled_raster)

    cutting_collapse_input = SCRIPT_DIR / "cut-edges-collapse-request.json"
    cutting_collapse_output = SCRIPT_DIR / "cut-edges-collapse-response.json"
    cutting_collapsed_raster = SCRIPT_DIR / "cut-edges-collapse-response-1.raw"
    make_collapse_request(cutting_collapse_input, cutting_labeled_raster, cutting_labeler_output)
    run_tsx("src/cucutcli.ts", cutting_collapse_output, cutting_collapse_input)
    validate_collapse(cutting_collapse_output, cutting_collapsed_raster)

    edt_input = SCRIPT_DIR / "cu-track-distance-request.json"
    edt_output = SCRIPT_DIR / "cu-track-distance-response.json"
    edt_raster = SCRIPT_DIR / "cu-track-distance-response-1.raw"
    make_track_edt_request(edt_input, tracks_labeled_raster, tracks_labeler_output)
    run_tsx("src/cucutcli.ts", edt_output, edt_input)
    validate_edt(edt_output, edt_raster)

    hole_edt_input = SCRIPT_DIR / "cu-hole-distance-request.json"
    hole_edt_output = SCRIPT_DIR / "cu-hole-distance-response.json"
    hole_edt_raster = SCRIPT_DIR / "cu-hole-distance-response-1.raw"
    has_holes = make_hole_edt_request(
        hole_edt_input, tracks_labeled_raster, tracks_labeler_output
    )
    if has_holes:
        run_tsx("src/cucutcli.ts", hole_edt_output, hole_edt_input)

    separation_input = SCRIPT_DIR / "cu-track-separation-request.json"
    separation_output = SCRIPT_DIR / "cu-track-separation-response.json"
    separation_path = SCRIPT_DIR / "cu-track-separation-response-1.raw"
    make_separation_request(separation_input, edt_raster, tracks_labeler_output)
    run_tsx("src/cucutcli.ts", separation_output, separation_input)
    validate_separation(separation_output, separation_path)

    optimize_path_input = SCRIPT_DIR / "cu-track-path-optimization-request.json"
    optimize_path_output = SCRIPT_DIR / "cu-track-path-optimization-response.json"
    optimized_path = SCRIPT_DIR / "cu-track-path-optimization-response-2.raw"
    optimized_indices = SCRIPT_DIR / "cu-track-path-optimization-response-1.raw"
    make_optimize_path_request(optimize_path_input, separation_path, 1.0)
    run_tsx("src/cucutcli.ts", optimize_path_output, optimize_path_input)
    validate_optimized_path(
        optimize_path_output, optimized_path, optimized_indices, separation_path
    )

    composite_path_input = SCRIPT_DIR / "cu-track-arc-composition-request.json"
    composite_path_output = SCRIPT_DIR / "cu-track-arc-composition-response.json"
    composite_path = SCRIPT_DIR / "cu-track-arc-composition-response-1.raw"
    make_composite_path_request(
        composite_path_input, separation_path, optimized_indices, 1.0, 3.0, 100.0
    )
    run_tsx("src/cucutcli.ts", composite_path_output, composite_path_input)
    validate_composite_path(composite_path_output, composite_path)

    if has_holes:
        hole_separation_input = SCRIPT_DIR / "cu-hole-separation-request.json"
        hole_separation_output = SCRIPT_DIR / "cu-hole-separation-response.json"
        hole_separation_path = SCRIPT_DIR / "cu-hole-separation-response-1.raw"
        make_hole_separation_request(
            hole_separation_input,
            hole_edt_raster,
            tracks_labeled_raster,
            tracks_labeler_output,
        )
        run_tsx("src/cucutcli.ts", hole_separation_output, hole_separation_input)
        validate_separation(hole_separation_output, hole_separation_path, inner=True)

        hole_optimize_path_input = SCRIPT_DIR / "cu-hole-path-optimization-request.json"
        hole_optimize_path_output = SCRIPT_DIR / "cu-hole-path-optimization-response.json"
        optimized_hole_path = SCRIPT_DIR / "cu-hole-path-optimization-response-2.raw"
        optimized_hole_indices = SCRIPT_DIR / "cu-hole-path-optimization-response-1.raw"
        make_optimize_path_request(hole_optimize_path_input, hole_separation_path, 1.0)
        run_tsx("src/cucutcli.ts", hole_optimize_path_output, hole_optimize_path_input)
        validate_optimized_path(
            hole_optimize_path_output,
            optimized_hole_path,
            optimized_hole_indices,
            hole_separation_path,
        )

        composite_hole_path_input = SCRIPT_DIR / "cu-hole-arc-composition-request.json"
        composite_hole_path_output = SCRIPT_DIR / "cu-hole-arc-composition-response.json"
        composite_hole_path = SCRIPT_DIR / "cu-hole-arc-composition-response-1.raw"
        make_composite_path_request(
            composite_hole_path_input,
            hole_separation_path,
            optimized_hole_indices,
            1.0,
            3.0,
            100.0,
        )
        run_tsx("src/cucutcli.ts", composite_hole_path_output, composite_hole_path_input)
        validate_composite_path(composite_hole_path_output, composite_hole_path)

    tracks_labeled_image = SCRIPT_DIR / "cu-labeled-tracks.png"
    cutting_labeled_image = SCRIPT_DIR / "cut-labeled-edges.png"
    run_tsx("core/script/raster2png.ts", "-o", tracks_labeled_image, tracks_labeled_raster)
    run_tsx("core/script/raster2png.ts", "-o", cutting_labeled_image, cutting_labeled_raster)
    collapsed_cutting_image = SCRIPT_DIR / "cut-collapsed-edges.png"
    run_tsx("core/script/raster2png.ts", "-o", collapsed_cutting_image, cutting_collapsed_raster)
    run_tsx(
        "core/script/raster2png.ts",
        "-f",
        "distance",
        "-o",
        SCRIPT_DIR / "cu-track-distance.png",
        edt_raster,
    )
    path_images = [
        ("cu-track-separation.png", "path", separation_path),
        ("cu-track-separation-optimized.png", "path", optimized_path),
        ("cu-track-separation-composite.png", "composite-path", composite_path),
    ]
    if has_holes:
        path_images.extend(
            [
                ("cu-hole-separation.png", "path", hole_separation_path),
                ("cu-hole-separation-optimized.png", "path", optimized_hole_path),
                ("cu-hole-separation-composite.png", "composite-path", composite_hole_path),
            ]
        )
    for output_name, filter_name, path in path_images:
        arguments: list[Path | str] = [
            "-o",
            SCRIPT_DIR / output_name,
            "-f",
            filter_name,
        ]
        if filter_name == "path":
            arguments.extend(["--filter-color", "ff0000"])
        arguments.extend(
            ["--filter-line", "ffff00", "--filter-background", tracks_image, path]
        )
        run_tsx("core/script/buffer2png.ts", *arguments)

        svg_output = SCRIPT_DIR / output_name.replace(".png", ".svg")
        svg_arguments: list[Path | str] = [
            "--res",
            "50",
            "-o",
            svg_output,
            "-f",
            "png",
            "--filter-opacity",
            "75",
            tracks_labeled_image,
        ]
        svg_arguments.extend(
            ["-f", filter_name, "--filter-color", "ff0000", "--filter-width", "1.5", path]
        )
        run_tsx("core/script/buffer2svg.ts", *svg_arguments)

    images = [
        tracks_image.name,
        cutting_image.name,
        tracks_labeled_image.name,
        cutting_labeled_image.name,
        collapsed_cutting_image.name,
        "cu-track-distance.png",
        "cu-track-separation.png",
        "cu-track-separation-optimized.png",
        "cu-track-separation-composite.png",
        "cu-track-separation.svg",
        "cu-track-separation-optimized.svg",
        "cu-track-separation-composite.svg",
    ]
    if has_holes:
        images.extend(
            [
                "cu-hole-separation.png",
                "cu-hole-separation-optimized.png",
                "cu-hole-separation-composite.png",
                "cu-hole-separation.svg",
                "cu-hole-separation-optimized.svg",
                "cu-hole-separation-composite.svg",
            ]
        )
    print("Test complete. Images and SVGs: " + ", ".join(str(SCRIPT_DIR / name) for name in images))


ENVIRONMENT = os.environ.copy()
ENVIRONMENT["PATH"] = (
    str(Path.home() / ".nvm/versions/node/v22.19.0/bin")
    + os.pathsep
    + ENVIRONMENT.get("PATH", "")
)


if __name__ == "__main__":
    main()
