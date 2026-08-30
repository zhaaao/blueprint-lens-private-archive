"""Owner-selected responsive authoring packet for LC6 Four-Track."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import tempfile
from typing import Any, Mapping

from PIL import Image, ImageDraw

from blueprint_lens import lc6_graph_first as _base


class LC6SelectedResponsiveError(ValueError):
    """Raised when selected LC6 responsive evidence drifts from its contract."""


SELECTED_CONDITION_ID = "LC6_SPLIT_FRONTIER_ROUTES"
TARGET_WIDTHS = (430, 480, 700)
RESPONSIVE_MODES = {
    430: "single_column",
    480: "stacked_detail",
    700: "side_by_side",
}
DISCLOSURE_STATES = _base.DISCLOSURE_STATES
SELECTED_SCENARIO_BY_STATE = _base.SELECTED_SCENARIO_BY_STATE
SCENARIO_ORDER = _base.SCENARIO_ORDER
FONTS = dict(_base.FONTS)
TOKENS = _base.TOKENS
NARROW_CANVAS = (0, 1350)
CRITERION_OVERVIEW_LABELS = {
    "LC6_OPAQUE": "Set Opaque",
    "LC6_UNCERTAIN": "Set Uncertain",
    "LC6_UNSUPPORTED": "Set Unsupported",
    "LC6_TRUNCATED": "Set …06",
}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC6SelectedResponsiveError(message)


def load_lc6_visual_ledger(evidence_dir):
    """Load the frozen LC6 ledger used by both comparison and selected packets."""

    return _base.load_lc6_visual_ledger(evidence_dir)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _file_entries(directory: Path) -> list[dict[str, Any]]:
    entries = []
    for path in sorted(directory.iterdir(), key=lambda item: item.name):
        _require(path.is_file(), "prior LC6 packet contains a directory")
        payload = path.read_bytes()
        entries.append(
            {
                "path": path.name,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    return entries


def _aggregate_sha256(entries: list[dict[str, Any]]) -> str:
    return hashlib.sha256(
        json.dumps(
            entries,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def build_prior_packet_lock(prior_packet_dir: str | Path) -> dict[str, Any]:
    """Validate and byte-lock the frozen 19-file comparison packet."""

    directory = Path(prior_packet_dir).resolve()
    _require(directory.is_dir(), "prior LC6 packet is unavailable")
    entries = _file_entries(directory)
    _require(len(entries) == 19, "prior LC6 packet must contain 19 files")
    hash_path = directory / "hashes.json"
    try:
        hashes = json.loads(hash_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC6SelectedResponsiveError(
            "prior LC6 hash inventory cannot be read"
        ) from error
    _require(
        hashes.get("entry_count") == 18
        and hashes.get("excluded_path") == "hashes.json",
        "prior LC6 hash inventory contract differs",
    )
    by_name = {entry["path"]: entry for entry in entries}
    expected_names = set(by_name) - {"hashes.json"}
    hash_entries = hashes.get("entries")
    _require(
        isinstance(hash_entries, list)
        and {entry.get("path") for entry in hash_entries} == expected_names,
        "prior LC6 hash coverage differs",
    )
    for entry in hash_entries:
        actual = by_name[entry["path"]]
        _require(
            entry.get("bytes") == actual["bytes"]
            and entry.get("sha256") == actual["sha256"],
            f"prior LC6 packet differs: {entry['path']}",
        )
    return {
        "format": "blueprint-lens-lc6-selected-prior-packet-lock",
        "format_version": "1.0.0",
        "path": "artifacts/r1/lc6-graph-first-candidates",
        "entry_count": len(entries),
        "sha256": _aggregate_sha256(entries),
        "entries": entries,
    }


def _state_slug(state: str) -> str:
    return state.casefold().replace("_", "-")


def _expected_manifest(
    ledger: Mapping[str, Any], prior_packet_dir: str | Path
) -> dict[str, Any]:
    _base.validate_lc6_visual_ledger(ledger)
    lock = build_prior_packet_lock(prior_packet_dir)
    states = []
    for width in TARGET_WIDTHS:
        for disclosure_state in DISCLOSURE_STATES:
            selected = SELECTED_SCENARIO_BY_STATE[disclosure_state]
            states.append(
                {
                    "state_id": (
                        f"{SELECTED_CONDITION_ID}__{disclosure_state}"
                        f"__W{width}"
                    ),
                    "condition_id": SELECTED_CONDITION_ID,
                    "condition_label": "Four-Track Boundary Overview",
                    "width": width,
                    "responsive_mode": RESPONSIVE_MODES[width],
                    "disclosure_state": disclosure_state,
                    "selected_scenario_id": selected,
                    "recoverability": _base.recoverability_contract(
                        ledger, selected
                    ),
                    "ledger_semantic_sha256": ledger["semantic_sha256"],
                    "evidence_state": "authoring_design_target",
                    "effect_paths": {
                        "svg": (
                            f"lc6-four-track-{_state_slug(disclosure_state)}"
                            f"-{width}.svg"
                        ),
                        "png": (
                            f"lc6-four-track-{_state_slug(disclosure_state)}"
                            f"-{width}.png"
                        ),
                    },
                }
            )
    return {
        "format": "blueprint-lens-lc6-selected-responsive-manifest",
        "format_version": "1.0.0",
        "status": "FOUR_TRACK__THREE_WIDTH_AUTHORING_TARGETS",
        "selection_status": "OWNER_SELECTED__IMPLEMENTATION_CONDITION",
        "selected_condition_id": SELECTED_CONDITION_ID,
        "default_condition_id": None,
        "complete_text_fallback_condition_id": "LC6_COMPLETE_TEXT",
        "target_widths_logical_px": list(TARGET_WIDTHS),
        "profile_binding": deepcopy(dict(ledger["profile_binding"])),
        "prior_packet": {
            "path": lock["path"],
            "entry_count": lock["entry_count"],
            "sha256": lock["sha256"],
        },
        "states": states,
        "non_claims": [
            "owner selection is not a product default",
            "responsive effects are not Slate or UE-visible evidence",
            "no human comprehension, preference or scalability result exists",
        ],
    }


def build_selected_manifest(
    ledger: Mapping[str, Any], prior_packet_dir: str | Path
) -> dict[str, Any]:
    """Build the exact owner-selected responsive manifest."""

    manifest = _expected_manifest(ledger, prior_packet_dir)
    validate_selected_manifest(manifest, ledger, prior_packet_dir)
    return manifest


def validate_selected_manifest(
    manifest: Mapping[str, Any],
    ledger: Mapping[str, Any],
    prior_packet_dir: str | Path,
) -> None:
    """Reject selection, evidence, responsive, or truth-binding drift."""

    _require(
        manifest == _expected_manifest(ledger, prior_packet_dir),
        "LC6 selected responsive manifest differs from frozen contract",
    )


def _scale_x(value: float, old_x: float, new_x: float, scale: float) -> float:
    return new_x + (value - old_x) * scale


def _transform_command(
    command: Mapping[str, Any],
    *,
    old_x: float,
    new_x: float,
    scale: float,
    delta_y: float,
) -> dict[str, Any]:
    item = deepcopy(dict(command))
    kind = item["kind"]
    if kind == "rect":
        item["x"] = round(_scale_x(item["x"], old_x, new_x, scale))
        item["y"] = round(item["y"] + delta_y)
        item["width"] = round(item["width"] * scale)
    elif kind == "text":
        item["x"] = round(_scale_x(item["x"], old_x, new_x, scale))
        item["y"] = round(item["y"] + delta_y)
        item["bounds"] = _base._text_bounds(
            item["x"],
            item["y"],
            item["text"],
            item["font_size"],
            item["weight"],
        )
    elif kind == "circle":
        item["cx"] = round(
            _scale_x(item["cx"], old_x, new_x, scale)
        )
        item["cy"] = round(item["cy"] + delta_y)
    elif kind in {"polyline", "polygon"}:
        item["points"] = [
            [
                round(_scale_x(point[0], old_x, new_x, scale)),
                round(point[1] + delta_y),
            ]
            for point in item["points"]
        ]
    return item


def _narrow_scene(
    scene: Mapping[str, Any], width: int
) -> dict[str, Any]:
    result = deepcopy(dict(scene))
    inner_width = width - 48
    result["width"] = width
    result["height"] = NARROW_CANVAS[1]
    result["responsive_mode"] = RESPONSIVE_MODES[width]
    commands = []
    region_specs = {
        "header": (24.0, 24.0, inner_width / 652.0, 0.0),
        "overview": (24.0, 24.0, inner_width / 404.0, 0.0),
        "detail": (442.0, 24.0, inner_width / 234.0, 588.0),
        "actions": (24.0, 24.0, inner_width / 652.0, 588.0),
    }
    region_rects = {
        "region.header": (24, 24, inner_width, 84),
        "region.overview": (24, 126, inner_width, 570),
        "region.detail": (24, 714, inner_width, 570),
        "region.actions": (24, 1302, inner_width, 30),
    }
    for command in scene["commands"]:
        command_id = command["id"]
        if command_id == "canvas.background":
            item = deepcopy(dict(command))
            item["width"] = width
            item["height"] = NARROW_CANVAS[1]
            commands.append(item)
            continue
        if command_id in region_rects:
            item = deepcopy(dict(command))
            x, y, rect_width, height = region_rects[command_id]
            item.update(x=x, y=y, width=rect_width, height=height)
            commands.append(item)
            continue
        region = command.get("region")
        if region in region_specs:
            old_x, new_x, scale, delta_y = region_specs[region]
            commands.append(
                _transform_command(
                    command,
                    old_x=old_x,
                    new_x=new_x,
                    scale=scale,
                    delta_y=delta_y,
                )
            )
        else:
            commands.append(deepcopy(dict(command)))
    result["commands"] = commands
    return result


def build_selected_scene(
    ledger: Mapping[str, Any],
    width: int,
    disclosure_state: str,
) -> dict[str, Any]:
    """Build one selected Four-Track responsive authoring state."""

    _require(width in TARGET_WIDTHS, "unsupported LC6 selected width")
    _require(
        disclosure_state in DISCLOSURE_STATES,
        "unsupported LC6 disclosure state",
    )
    scene = _base.build_graph_first_scene(
        ledger, SELECTED_CONDITION_ID, disclosure_state
    )
    for command in scene["commands"]:
        scenario_id = command.get("scenario_id")
        if command.get("id") == (
            f"overview.{scenario_id}.criterion.label"
        ):
            command["text"] = CRITERION_OVERVIEW_LABELS[scenario_id]
            command["bounds"] = _base._text_bounds(
                command["x"],
                command["y"],
                command["text"],
                command["font_size"],
                command["weight"],
            )
    scene["state_id"] = (
        f"{SELECTED_CONDITION_ID}__{disclosure_state}__W{width}"
    )
    scene["effect_paths"] = {
        "svg": f"lc6-four-track-{_state_slug(disclosure_state)}-{width}.svg",
        "png": f"lc6-four-track-{_state_slug(disclosure_state)}-{width}.png",
    }
    scene["responsive_mode"] = RESPONSIVE_MODES[width]
    if width != 700:
        scene = _narrow_scene(scene, width)
    checks = selected_scene_checks(scene)
    _require(checks["pass"], f"invalid LC6 selected scene: {checks}")
    return scene


def scene_signature(scene: Mapping[str, Any]) -> str:
    """Compare selected metadata independently from visual structure."""

    value = deepcopy(dict(scene))
    value.pop("responsive_mode", None)
    value.pop("state_id", None)
    value.pop("effect_paths", None)
    return hashlib.sha256(
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def overview_geometry_sha256(scene: Mapping[str, Any]) -> str:
    return _base.overview_geometry_sha256(scene)


def selected_scene_checks(scene: Mapping[str, Any]) -> dict[str, Any]:
    """Run the graph-first oracle against the responsive canvas."""

    checks = _base.graph_first_scene_checks(scene)
    expected_canvas = (
        [700, 760]
        if scene.get("width") == 700
        else [scene.get("width"), NARROW_CANVAS[1]]
    )
    checks["canvas"] = [scene.get("width"), scene.get("height")]
    checks["pass"] = (
        checks["canvas"] == expected_canvas
        and scene.get("responsive_mode")
        == RESPONSIVE_MODES.get(scene.get("width"))
        and not any(
            value
            for key, value in checks.items()
            if key
            not in {
                "canvas",
                "overview_geometry_sha256",
                "command_count",
                "pass",
            }
        )
    )
    return checks


def svg_for_selected_scene(scene: Mapping[str, Any]) -> str:
    """Render one validated selected scene as deterministic SVG."""

    checks = selected_scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid scene: {checks}")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            '<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{scene["width"]}" height="{scene["height"]}" '
            f'viewBox="0 0 {scene["width"]} {scene["height"]}">'
        ),
        (
            f'  <title>{html.escape(scene["condition_label"])} · '
            f'{scene["disclosure_state"]} · {scene["width"]}</title>'
        ),
    ]
    for command in scene["commands"]:
        command_id = html.escape(str(command["id"]), quote=True)
        if command["kind"] == "rect":
            lines.append(
                f'  <rect id="{command_id}" x="{command["x"]}" '
                f'y="{command["y"]}" width="{command["width"]}" '
                f'height="{command["height"]}" rx="{command["radius"]}" '
                f'fill="{command["fill"]}" '
                f'stroke="{command["stroke"] or "none"}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "text":
            weight = 700 if command["weight"] == "bold" else 400
            lines.append(
                f'  <text id="{command_id}" x="{command["x"]}" '
                f'y="{command["y"]}" fill="{command["fill"]}" '
                f'font-family="Segoe UI" font-size="{command["font_size"]}" '
                f'font-weight="{weight}" '
                'dominant-baseline="text-before-edge">'
                f'{html.escape(str(command["text"]))}</text>'
            )
        elif command["kind"] == "polyline":
            points = " ".join(
                f"{x},{y}" for x, y in command["points"]
            )
            dash = (
                f' stroke-dasharray="{",".join(map(str, command["dash"]))}"'
                if command["dash"]
                else ""
            )
            lines.append(
                f'  <polyline id="{command_id}" points="{points}" '
                f'fill="none" stroke="{command["stroke"]}" '
                f'stroke-width="{command["stroke_width"]}"{dash} '
                'stroke-linecap="round"/>'
            )
        elif command["kind"] == "polygon":
            points = " ".join(
                f"{x},{y}" for x, y in command["points"]
            )
            lines.append(
                f'  <polygon id="{command_id}" points="{points}" '
                f'fill="{command["fill"]}" stroke="{command["stroke"]}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "circle":
            lines.append(
                f'  <circle id="{command_id}" cx="{command["cx"]}" '
                f'cy="{command["cy"]}" r="{command["radius"]}" '
                f'fill="{command["fill"]}"/>'
            )
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def png_for_selected_scene(
    scene: Mapping[str, Any], path: str | Path
) -> None:
    """Render one validated selected scene as deterministic PNG."""

    checks = selected_scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid scene: {checks}")
    image = Image.new(
        "RGB", (scene["width"], scene["height"]), TOKENS["background"]
    )
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        if command["kind"] == "rect":
            draw.rounded_rectangle(
                [
                    command["x"],
                    command["y"],
                    command["x"] + command["width"],
                    command["y"] + command["height"],
                ],
                radius=command["radius"],
                fill=command["fill"],
                outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif command["kind"] == "text":
            draw.text(
                (command["x"], command["y"]),
                command["text"],
                font=_base._font(
                    command["font_size"], command["weight"]
                ),
                fill=command["fill"],
                anchor="lt",
            )
        elif command["kind"] == "polyline":
            points = [tuple(point) for point in command["points"]]
            if command["dash"]:
                for start, end in zip(points, points[1:]):
                    _base._draw_dashed_segment(
                        draw,
                        start,
                        end,
                        command["stroke"],
                        command["stroke_width"],
                        command["dash"],
                    )
            else:
                draw.line(
                    points,
                    fill=command["stroke"],
                    width=command["stroke_width"],
                )
        elif command["kind"] == "polygon":
            draw.polygon(
                [tuple(point) for point in command["points"]],
                fill=command["fill"],
                outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif command["kind"] == "circle":
            draw.ellipse(
                [
                    command["cx"] - command["radius"],
                    command["cy"] - command["radius"],
                    command["cx"] + command["radius"],
                    command["cy"] + command["radius"],
                ],
                fill=command["fill"],
            )
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target, format="PNG", optimize=False, compress_level=9)


def _responsive_board(
    scenes: list[Mapping[str, Any]],
    png_paths: Mapping[str, Path],
    output: Path,
) -> None:
    widths = list(TARGET_WIDTHS)
    column_x = {
        430: 20,
        480: 20 + 430 + 20,
        700: 20 + 430 + 20 + 480 + 20,
    }
    board_width = 20 + sum(widths) + 20 * 3
    row_height = 1370
    board_height = 70 + len(DISCLOSURE_STATES) * row_height
    image = Image.new(
        "RGB", (board_width, board_height), TOKENS["background"]
    )
    draw = ImageDraw.Draw(image)
    draw.text(
        (20, 20),
        "LC6 Four-Track · selected responsive states",
        font=_base._font(20, "bold"),
        fill=TOKENS["text"],
        anchor="lt",
    )
    for scene in scenes:
        row = DISCLOSURE_STATES.index(scene["disclosure_state"])
        width = scene["width"]
        path = png_paths[scene["effect_paths"]["png"]]
        with Image.open(path) as effect:
            image.paste(
                effect.convert("RGB"),
                (column_x[width], 70 + row * row_height),
            )
    image.save(output, format="PNG", optimize=False, compress_level=9)


def _packet_hashes(directory: Path, names: list[str]) -> dict[str, Any]:
    entries = []
    for name in sorted(names):
        payload = (directory / name).read_bytes()
        entries.append(
            {
                "path": name,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    return {
        "format": "blueprint-lens-lc6-selected-self-excluding-hashes",
        "format_version": "1.0.0",
        "algorithm": "sha256",
        "excluded_path": "hashes.json",
        "entry_count": len(entries),
        "entries": entries,
    }


def _directories_identical(first: Path, second: Path) -> bool:
    first_files = sorted(path.name for path in first.iterdir())
    second_files = sorted(path.name for path in second.iterdir())
    return first_files == second_files and all(
        (first / name).read_bytes() == (second / name).read_bytes()
        for name in first_files
    )


def build_selected_artifacts(
    evidence_dir: str | Path,
    prior_packet_dir: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Atomically publish the exact 23-file selected responsive packet."""

    destination = Path(output_dir).resolve()
    _require(
        not destination.exists() or destination.is_dir(),
        "LC6 selected destination is not a directory",
    )
    for font_path in FONTS.values():
        _require(
            Path(font_path).is_file(),
            f"LC6 selected font is unavailable: {font_path}",
        )
    ledger = load_lc6_visual_ledger(evidence_dir)
    prior_lock = build_prior_packet_lock(prior_packet_dir)
    manifest = build_selected_manifest(ledger, prior_packet_dir)
    scenes = [
        build_selected_scene(ledger, width, state)
        for width in TARGET_WIDTHS
        for state in DISCLOSURE_STATES
    ]
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(
            prefix=f".{destination.name}.staging-",
            dir=destination.parent,
        )
    )
    try:
        files: dict[str, Path] = {}
        manifest_name = "lc6-four-track-selected-manifest.json"
        oracle_name = "lc6-four-track-selected-oracle.json"
        prior_name = "lc6-four-track-selected-prior-lock.json"
        (staging / manifest_name).write_bytes(_json_bytes(manifest))
        files[manifest_name] = staging / manifest_name
        oracle = {
            "format": "blueprint-lens-lc6-selected-responsive-oracle",
            "format_version": "1.0.0",
            "checks": {
                scene["state_id"]: selected_scene_checks(scene)
                for scene in scenes
            },
            "overview_geometry_sha256_by_width": {
                str(width): overview_geometry_sha256(
                    next(
                        scene
                        for scene in scenes
                        if scene["width"] == width
                    )
                )
                for width in TARGET_WIDTHS
            },
        }
        _require(
            all(check["pass"] for check in oracle["checks"].values()),
            "LC6 selected responsive geometry oracle failed",
        )
        (staging / oracle_name).write_bytes(_json_bytes(oracle))
        files[oracle_name] = staging / oracle_name
        (staging / prior_name).write_bytes(_json_bytes(prior_lock))
        files[prior_name] = staging / prior_name
        png_paths: dict[str, Path] = {}
        for scene in scenes:
            svg_name = scene["effect_paths"]["svg"]
            png_name = scene["effect_paths"]["png"]
            (staging / svg_name).write_text(
                svg_for_selected_scene(scene),
                encoding="utf-8",
                newline="\n",
            )
            png_for_selected_scene(scene, staging / png_name)
            files[svg_name] = staging / svg_name
            files[png_name] = staging / png_name
            png_paths[png_name] = staging / png_name
        board_name = "lc6-four-track-selected-responsive-board.png"
        _responsive_board(scenes, png_paths, staging / board_name)
        files[board_name] = staging / board_name
        _require(len(files) == 22, "LC6 selected pre-hash count differs")
        (staging / "hashes.json").write_bytes(
            _json_bytes(_packet_hashes(staging, list(files)))
        )
        files["hashes.json"] = staging / "hashes.json"
        _require(
            len(list(staging.iterdir())) == 23,
            "LC6 selected packet must contain 23 files",
        )
        if destination.exists():
            _require(
                _directories_identical(staging, destination),
                "LC6 selected destination exists with different bytes",
            )
        else:
            os.replace(staging, destination)
        return {
            name: destination / name for name in sorted(files)
        }
    finally:
        if staging.exists():
            shutil.rmtree(staging)
