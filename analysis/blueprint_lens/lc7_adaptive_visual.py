"""Deterministic adaptive-layout concept boards for the frozen LC7 truth."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from types import MappingProxyType
from typing import Any, Mapping

from PIL import Image, ImageDraw

from blueprint_lens.lc6_visual import TOKENS, _font
from blueprint_lens.lc7_visual import (
    ACTIONS,
    CANVAS,
    NON_CLAIMS,
    QUESTION,
    _node,
    _polyline,
    _rect,
    _route,
    _text,
    load_lc7_visual_truth,
    png_for_scene,
    scene_checks,
    scene_information_set,
    svg_for_scene,
)


class LC7AdaptiveVisualError(ValueError):
    """Raised when an adaptive LC7 proposal loses truth or geometry."""


CONDITIONS = MappingProxyType(
    {
        "LC7_CANONICAL_RECURRENCE_BACKBONE": (
            "A3 · Adaptive Recurrence Backbone",
            "lc7-adaptive-recurrence-backbone-effect-700",
            "bounded context cue plus one focused SCC spine with isolated relation lanes",
        ),
        "LC7_LINEAR_RECURRENCE_SPINE": (
            "B3 · Linear Relation Ledger",
            "lc7-linear-relation-ledger-effect-700",
            "complete relation rows replace graph wires when the SCC has a canonical single spine",
        ),
        "LC7_COMPOUND_SCC_GATE": (
            "C3 · Fixed-Port SCC Gate",
            "lc7-fixed-port-scc-gate-effect-700",
            "structural SCC boundary with fixed entry, predicate, return, and exit ports",
        ),
    }
)
CONDITION_IDS = tuple(CONDITIONS)
RECOMMENDED_CONDITION_ID = "LC7_CANONICAL_RECURRENCE_BACKBONE"
OUTPUT_STATUS = "OWNER_SELECTED_A3__IMPLEMENTATION_NOT_STARTED"
OVERVIEW = (24, 142, 440, 540)
DETAIL = (476, 142, 200, 540)
OUTPUT_FILES = (
    "lc7-adaptive-recurrence-backbone-effect-700.svg",
    "lc7-adaptive-recurrence-backbone-effect-700.png",
    "lc7-linear-relation-ledger-effect-700.svg",
    "lc7-linear-relation-ledger-effect-700.png",
    "lc7-fixed-port-scc-gate-effect-700.svg",
    "lc7-fixed-port-scc-gate-effect-700.png",
    "lc7-adaptive-layout-comparison-board.svg",
    "lc7-adaptive-layout-comparison-board.png",
    "lc7-adaptive-scale-policy-board.png",
    "lc7-adaptive-layout-manifest.json",
    "lc7-adaptive-layout-oracle.json",
    "hashes.json",
)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC7AdaptiveVisualError(message)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _base_scene(condition_id: str) -> list[dict[str, Any]]:
    label = CONDITIONS[condition_id][0]
    decision_state = "SELECTED" if condition_id == RECOMMENDED_CONDITION_ID else "RETAINED"
    commands: list[dict[str, Any]] = []
    _rect(commands, "canvas", 0, 0, *CANVAS, fill=TOKENS["background"], stroke=None, radius=0)
    _rect(commands, "header", 24, 24, 652, 100, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=10)
    _text(commands, "header.title", 42, 40, "Static Recurrence · Adaptive Layout", font_size=18, weight="bold")
    _text(commands, "header.question", 42, 76, QUESTION, font_size=10, weight="bold", fact_id="shared.question")
    _text(commands, "header.state", 600, 42, decision_state, fill=TOKENS["muted"], font_size=7, weight="bold")
    _rect(commands, "overview", *OVERVIEW, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=10)
    _text(commands, "overview.condition", 40, 156, label.upper(), fill=TOKENS["core"], font_size=9, weight="bold")
    _rect(commands, "overview.count", 40, 178, 176, 22, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=11)
    _text(commands, "overview.count.text", 50, 184, "SCC · 3 MEMBERS · 8 RELATIONS", fill=TOKENS["query"], font_size=7, weight="bold", fact_id="shared.counts")
    _rect(commands, "overview.runtime", 224, 178, 222, 22, fill="#241D31", stroke=TOKENS["query"], radius=11)
    _text(commands, "overview.runtime.text", 234, 184, "RUNTIME ITERATIONS · NOT CLAIMED", fill=TOKENS["query"], font_size=7, weight="bold", fact_id="shared.runtime_boundary")
    _rect(commands, "detail", *DETAIL, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=10)
    _text(commands, "detail.label", 492, 158, "DETAIL · NO SELECTION", fill=TOKENS["muted"], font_size=8, weight="bold")
    _text(commands, "detail.title", 492, 202, "Select one unit", font_size=14, weight="bold")
    _text(commands, "detail.help", 492, 226, "Overview geometry stays fixed", fill=TOKENS["muted"], font_size=8)
    for index, tab in enumerate(("SUMMARY", "RELATIONS", "EVIDENCE")):
        y = 260 + index * 36
        _rect(commands, f"detail.tab.{index}", 492, y, 168, 26, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=5)
        _text(commands, f"detail.tab.{index}.text", 504, y + 7, tab, fill=TOKENS["muted"], font_size=7, weight="bold")
    _text(commands, "detail.scale", 492, 404, "ADAPTIVE SCALE MODES", fill=TOKENS["muted"], font_size=8, weight="bold")
    for index, (mode, note) in enumerate((("FULL", "all facts fit"), ("FOCUS", "fold context"), ("INDEX", "one SCC open"))):
        y = 430 + index * 42
        _rect(commands, f"detail.mode.{index}", 492, y, 50, 24, fill="#182231", stroke=TOKENS["core"], radius=12)
        _text(commands, f"detail.mode.{index}.name", 502, y + 7, mode, fill=TOKENS["core"], font_size=7, weight="bold")
        _text(commands, f"detail.mode.{index}.note", 550, y + 7, note, font_size=8)
    _text(commands, "detail.fallback", 492, 626, "Complete text always available", fill=TOKENS["muted"], font_size=7)
    _rect(commands, "actions", 24, 700, 652, 36, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=8)
    widths = (112, 142, 100)
    x = 38
    for index, (label, width) in enumerate(zip(ACTIONS, widths, strict=True)):
        _rect(commands, f"action.{index}", x, 706, width, 24, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=5)
        _text(
            commands,
            f"action.{index}.text",
            x + 12,
            712,
            label,
            font_size=8,
            weight="bold",
            action_id=label.casefold().replace(" ", "_"),
        )
        x += width + 8
    _text(commands, "actions.state", 532, 712, "DESIGN · NOT SLATE EVIDENCE", fill=TOKENS["muted"], font_size=7, weight="bold")
    return commands


def _context_cue(commands: list[dict[str, Any]], labels: tuple[str, ...]) -> None:
    x = 42
    widths = (72, 72, 82, 94)
    for index, (label, width) in enumerate(zip(labels, widths, strict=True)):
        fill = "#241D31" if "SCC" in label else TOKENS["surface_alt"]
        stroke = TOKENS["query"] if "SCC" in label else TOKENS["line"]
        _rect(commands, f"context.{index}", x, 212, width, 24, fill=fill, stroke=stroke, radius=12)
        _text(commands, f"context.{index}.text", x + 10, 219, label, fill=stroke, font_size=7, weight="bold")
        if index < len(labels) - 1:
            _polyline(commands, f"context.link.{index}", [(x + width + 3, 224), (x + width + 13, 224)], stroke=TOKENS["muted"], stroke_width=1)
        x += width + 16


def _scc_bracket(commands: list[dict[str, Any]], x: int, top: int, bottom: int) -> None:
    _polyline(commands, "scc.bracket", [(x + 10, top), (x, top), (x, bottom), (x + 10, bottom)], stroke=TOKENS["query"], stroke_width=2)
    _text(commands, "scc.bracket.label", x - 2, top - 18, "SCC · 3", fill=TOKENS["query"], font_size=7, weight="bold", fact_id="shared.scc_membership")


def _adaptive_backbone_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_CANONICAL_RECURRENCE_BACKBONE")
    _context_cue(commands, ("ENTRY", "INIT", "SCC · 3", "CRITERION"))
    _rect(commands, "scc.member-band", 184, 324, 138, 234, fill="#1A1720", stroke=None, radius=10)
    _text(commands, "scc.member-band.label", 190, 314, "SCC · 3", fill=TOKENS["query"], font_size=7, weight="bold", fact_id="shared.scc_membership")
    boxes = {
        "event": (40, 260, 118, 38),
        "initialise": (40, 316, 118, 40),
        "get_counter": (188, 254, 108, 38),
        "compare": (308, 254, 124, 38),
        "branch": (194, 334, 118, 42),
        "visited": (194, 420, 118, 42),
        "advance": (194, 506, 118, 42),
        "criterion": (326, 334, 124, 42),
    }
    _rect(commands, "predicate.dock", 178, 244, 272, 58, fill="#201C17", stroke=TOKENS["opaque"], radius=9)
    _text(commands, "predicate.dock.label", 188, 246, "PREDICATE DOCK", fill=TOKENS["opaque"], font_size=6, weight="bold")
    _text(commands, "lane.exit", 382, 388, "EXIT", fill=TOKENS["criterion"], font_size=7, weight="bold")
    _route(commands, truth, "event_to_initialise", [(99, 298), (99, 316)], label="then", stroke=TOKENS["core"], label_at=(106, 300))
    _route(commands, truth, "initialise_to_branch", [(158, 336), (176, 336), (176, 346), (194, 346)], label="then", stroke=TOKENS["core"], label_at=(160, 320))
    _route(commands, truth, "get_to_compare", [(296, 273), (308, 273)], label="A", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(298, 260))
    _route(commands, truth, "compare_to_branch", [(370, 292), (370, 316), (253, 316), (253, 334)], label="condition", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(318, 304))
    _route(commands, truth, "branch_to_visited", [(253, 376), (253, 420)], label="then", stroke=TOKENS["core"], label_at=(260, 390))
    _route(commands, truth, "visited_to_advance", [(253, 462), (253, 506)], label="then", stroke=TOKENS["core"], label_at=(260, 476))
    _route(commands, truth, "advance_to_branch", [(194, 527), (162, 527), (162, 364), (194, 364)], label="return", stroke=TOKENS["query"], label_at=(126, 450))
    _route(commands, truth, "branch_to_criterion", [(312, 346), (326, 346)], label="else", stroke=TOKENS["criterion"], label_at=(314, 330))
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def _ledger_relation(
    commands: list[dict[str, Any]],
    truth: Mapping[str, Any],
    key: str,
    y: int,
    left: str,
    label: str,
    right: str,
    *,
    colour: str,
) -> None:
    _rect(commands, f"ledger.{key}", 48, y, 398, 26, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=5)
    _text(commands, f"ledger.{key}.left", 58, y + 7, left, font_size=7, weight="bold")
    _route(commands, truth, key, [(202, y + 13), (244, y + 13)], label=label, stroke=colour, label_at=(208, y + 1))
    _text(commands, f"ledger.{key}.right", 256, y + 7, right, font_size=7, weight="bold")


def _linear_ledger_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_LINEAR_RECURRENCE_SPINE")
    _context_cue(commands, ("ENTRY", "INIT", "SCC · 3", "CRITERION"))
    boxes = {
        "event": (40, 252, 98, 34),
        "initialise": (148, 252, 104, 34),
        "branch": (40, 306, 118, 38),
        "visited": (168, 306, 118, 38),
        "advance": (296, 306, 118, 38),
        "get_counter": (40, 552, 112, 36),
        "compare": (164, 552, 120, 36),
        "criterion": (296, 552, 136, 36),
    }
    _text(commands, "ledger.member.label", 40, 292, "MEMBERS · CANONICAL SPINE ORDER", fill=TOKENS["query"], font_size=7, weight="bold", fact_id="shared.scc_membership")
    _ledger_relation(commands, truth, "event_to_initialise", 360, "LC7_STATIC_SCC", "THEN", "Set Counter", colour=TOKENS["core"])
    _ledger_relation(commands, truth, "initialise_to_branch", 392, "Set Counter", "THEN", "Branch", colour=TOKENS["core"])
    _ledger_relation(commands, truth, "branch_to_visited", 424, "Branch", "THEN", "Set Visited", colour=TOKENS["core"])
    _ledger_relation(commands, truth, "visited_to_advance", 456, "Set Visited", "THEN", "Advance", colour=TOKENS["core"])
    _ledger_relation(commands, truth, "advance_to_branch", 488, "Advance", "RETURN", "Branch", colour=TOKENS["query"])
    _ledger_relation(commands, truth, "branch_to_criterion", 520, "Branch", "ELSE", "Set Complete", colour=TOKENS["criterion"])
    _ledger_relation(commands, truth, "get_to_compare", 596, "Get Counter", "A", "integer < integer", colour=TOKENS["opaque"])
    _ledger_relation(commands, truth, "compare_to_branch", 628, "integer < integer", "CONDITION", "Branch", colour=TOKENS["opaque"])
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def _fixed_port_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_COMPOUND_SCC_GATE")
    _context_cue(commands, ("ENTRY", "INIT", "SCC · 3", "CRITERION"))
    _rect(commands, "gate.scc", 170, 350, 280, 220, fill="#1C1825", stroke=TOKENS["query"], stroke_width=2, radius=12)
    _text(commands, "gate.scc.label", 184, 356, "STRUCTURAL SCC · 3", fill=TOKENS["query"], font_size=8, weight="bold", fact_id="shared.scc_membership")
    _rect(commands, "gate.entry.port", 160, 382, 20, 24, fill="#182231", stroke=TOKENS["core"], radius=10)
    _text(commands, "gate.entry.port.text", 164, 389, "IN", fill=TOKENS["core"], font_size=6, weight="bold")
    _rect(commands, "gate.exit.port", 382, 338, 22, 24, fill="#16261D", stroke=TOKENS["criterion"], radius=10)
    _text(commands, "gate.exit.port.text", 385, 345, "OUT", fill=TOKENS["criterion"], font_size=6, weight="bold")
    boxes = {
        "event": (38, 286, 112, 36),
        "initialise": (38, 342, 112, 38),
        "get_counter": (184, 252, 108, 38),
        "compare": (304, 252, 124, 38),
        "branch": (246, 378, 124, 42),
        "visited": (188, 478, 118, 42),
        "advance": (326, 478, 118, 42),
        "criterion": (316, 298, 134, 40),
    }
    _route(commands, truth, "event_to_initialise", [(94, 322), (94, 342)], label="then", stroke=TOKENS["core"], label_at=(100, 326))
    _route(commands, truth, "initialise_to_branch", [(150, 360), (160, 360), (160, 394), (246, 394)], label="then", stroke=TOKENS["core"], label_at=(182, 378))
    _route(commands, truth, "get_to_compare", [(292, 271), (304, 271)], label="A", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(295, 258))
    _route(commands, truth, "compare_to_branch", [(366, 290), (366, 328), (308, 328), (308, 378)], label="condition", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(314, 316))
    _route(commands, truth, "branch_to_visited", [(266, 420), (247, 478)], label="then", stroke=TOKENS["core"], label_at=(230, 442))
    _route(commands, truth, "visited_to_advance", [(306, 499), (326, 499)], label="then", stroke=TOKENS["core"], label_at=(306, 486))
    _route(commands, truth, "advance_to_branch", [(386, 478), (350, 420)], label="return", stroke=TOKENS["query"], label_at=(372, 442))
    _route(commands, truth, "branch_to_criterion", [(370, 390), (393, 390), (393, 338)], label="else", stroke=TOKENS["criterion"], label_at=(376, 374))
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def build_scene(truth: Mapping[str, Any], condition_id: str) -> dict[str, Any]:
    builders = {
        "LC7_CANONICAL_RECURRENCE_BACKBONE": _adaptive_backbone_scene,
        "LC7_LINEAR_RECURRENCE_SPINE": _linear_ledger_scene,
        "LC7_COMPOUND_SCC_GATE": _fixed_port_scene,
    }
    _require(condition_id in builders, f"unknown adaptive LC7 condition: {condition_id}")
    return {
        "format": "blueprint-lens-lc7-adaptive-authoring-scene",
        "format_version": "1.0.0",
        "condition_id": condition_id,
        "condition_label": CONDITIONS[condition_id][0],
        "disclosure_state": "NEUTRAL",
        "width": CANVAS[0],
        "height": CANVAS[1],
        "commands": builders[condition_id](truth),
    }


def _axis_segments(command: Mapping[str, Any]) -> list[tuple[tuple[int, int], tuple[int, int]]]:
    if command.get("kind") != "polyline" or not command.get("relation_id"):
        return []
    return [(tuple(a), tuple(b)) for a, b in zip(command["points"], command["points"][1:])]


def _strict_collinear_overlap(
    first: tuple[tuple[int, int], tuple[int, int]],
    second: tuple[tuple[int, int], tuple[int, int]],
) -> bool:
    (ax, ay), (bx, by) = first
    (cx, cy), (dx, dy) = second
    if ay == by == cy == dy:
        return max(min(ax, bx), min(cx, dx)) < min(max(ax, bx), max(cx, dx))
    if ax == bx == cx == dx:
        return max(min(ay, by), min(cy, dy)) < min(max(ay, by), max(cy, dy))
    return False


def route_geometry(scene: Mapping[str, Any]) -> dict[str, Any]:
    routes = [command for command in scene["commands"] if command.get("kind") == "polyline" and command.get("relation_id")]
    overlap_pairs: set[tuple[str, str]] = set()
    for index, first in enumerate(routes):
        for second in routes[index + 1 :]:
            if any(_strict_collinear_overlap(a, b) for a in _axis_segments(first) for b in _axis_segments(second)):
                overlap_pairs.add(tuple(sorted((first["id"], second["id"]))))
    bend_counts = {route["id"]: max(0, len(route["points"]) - 2) for route in routes}
    return {
        "collinear_overlap_pairs": [list(pair) for pair in sorted(overlap_pairs)],
        "bend_counts": bend_counts,
        "max_bends": max(bend_counts.values(), default=0),
        "relation_route_count": len(routes),
    }


def adaptive_scene_checks(scene: Mapping[str, Any], truth: Mapping[str, Any]) -> dict[str, Any]:
    checks = scene_checks(scene, truth)
    geometry = route_geometry(scene)
    checks["geometry"] = geometry
    checks["pass"] = bool(checks["pass"] and not geometry["collinear_overlap_pairs"] and geometry["max_bends"] <= 3)
    return checks


def _comparison_board_svg(scenes: list[Mapping[str, Any]], truth: Mapping[str, Any]) -> str:
    width, height = 2180, 850
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f'<rect width="{width}" height="{height}" fill="{TOKENS["background"]}"/>',
        '<text x="24" y="18" fill="#F4F7FB" font-family="Segoe UI" font-size="22" font-weight="700" dominant-baseline="text-before-edge">LC7 adaptive layout · three truth-matched options</text>',
        '<text x="24" y="48" fill="#98A7BA" font-family="Segoe UI" font-size="11" dominant-baseline="text-before-edge">A3 owner-selected; B3/C3 retained. Same 8 units, 8 relations, SCC membership, actions and runtime boundary.</text>',
    ]
    for index, scene in enumerate(scenes):
        inner = svg_for_scene(scene, truth).splitlines()[2:-1]
        lines.append(f'<g transform="translate({20 + index * 720},72)">')
        lines.extend(f"  {line}" for line in inner)
        lines.append("</g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _comparison_board_png(effect_paths: Mapping[str, Path], path: Path) -> None:
    image = Image.new("RGB", (2180, 850), TOKENS["background"])
    draw = ImageDraw.Draw(image)
    draw.text((24, 18), "LC7 adaptive layout · three truth-matched options", font=_font(22, "bold"), fill=TOKENS["text"], anchor="lt")
    draw.text((24, 48), "A3 owner-selected; B3/C3 retained. Same 8 units, 8 relations, SCC membership, actions and runtime boundary.", font=_font(11, "regular"), fill=TOKENS["muted"], anchor="lt")
    for index, condition_id in enumerate(CONDITION_IDS):
        with Image.open(effect_paths[condition_id]) as effect:
            _require(effect.size == CANVAS, f"adaptive LC7 effect dimensions differ: {condition_id}")
            image.paste(effect.convert("RGB"), (20 + index * 720, 72))
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _scale_policy_board(path: Path) -> None:
    image = Image.new("RGB", (1500, 900), TOKENS["background"])
    draw = ImageDraw.Draw(image)

    def rr(box: tuple[int, int, int, int], fill: str, outline: str, radius: int = 10, width: int = 1) -> None:
        draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)

    def tx(x: int, y: int, value: str, size: int = 12, colour: str = TOKENS["text"], weight: str = "regular") -> None:
        draw.text((x, y), value, font=_font(size, weight), fill=colour, anchor="lt")

    draw.text((30, 22), "A3 · owner-selected Adaptive Recurrence Backbone · scale policy", font=_font(24, "bold"), fill=TOKENS["text"], anchor="lt")
    draw.text((30, 58), "Synthetic behaviour illustration · not fixture, UE, scalability or comprehension evidence", font=_font(12, "regular"), fill=TOKENS["muted"], anchor="lt")
    panels = ((30, "FULL", "All accountable facts fit"), (520, "FOCUS", "Context exceeds route budget"), (1010, "INDEX", "Multiple SCCs exceed viewport"))
    for x, mode, note in panels:
        rr((x, 96, x + 460, 852), TOKENS["surface"], TOKENS["line"], 14)
        rr((x + 18, 116, x + 88, 144), "#182231", TOKENS["core"], 14)
        tx(x + 34, 124, mode, 9, TOKENS["core"], "bold")
        tx(x + 106, 122, note, 11, TOKENS["text"], "bold")
        tx(x + 20, 164, "CONTEXT CUE", 9, TOKENS["muted"], "bold")
    # FULL: all current truth remains directly visible.
    x = 30
    labels = (("Entry", 78), ("Init", 72), ("SCC · 3", 88), ("Criterion", 92))
    cx = x + 20
    for label, width in labels:
        rr((cx, 190, cx + width, 220), TOKENS["surface_alt"], TOKENS["query"] if "SCC" in label else TOKENS["line"], 15)
        tx(cx + 12, 199, label, 9, TOKENS["query"] if "SCC" in label else TOKENS["text"], "bold")
        cx += width + 12
    rr((x + 44, 278, x + 416, 684), "#151A22", TOKENS["line"], 12)
    tx(x + 64, 298, "Focused SCC", 10, TOKENS["query"], "bold")
    for index, label in enumerate(("Branch", "Set Visited", "Advance Counter")):
        y = 342 + index * 104
        rr((x + 150, y, x + 310, y + 54), "#211B2B", TOKENS["query"], 8)
        tx(x + 166, y + 12, label, 10, TOKENS["text"], "bold")
        if index < 2:
            draw.line((x + 230, y + 54, x + 230, y + 104), fill=TOKENS["core"], width=3)
    draw.line((x + 150, 552, x + 112, 552, x + 112, 366, x + 150, 366), fill=TOKENS["query"], width=3)
    tx(x + 62, 444, "RETURN", 8, TOKENS["query"], "bold")
    # FOCUS: counted context folds, one SCC detail.
    x = 520
    for left, width, label, colour in ((x + 20, 118, "12 upstream units", TOKENS["muted"]), (x + 150, 92, "SCC · 3", TOKENS["query"]), (x + 254, 120, "9 downstream units", TOKENS["muted"]), (x + 386, 54, "Target", TOKENS["criterion"])):
        rr((left, 190, left + width, 220), TOKENS["surface_alt"], colour, 15)
        tx(left + 10, 199, label, 8, colour, "bold")
    tx(x + 20, 246, "COUNT-PRESERVING FOLDS", 9, TOKENS["muted"], "bold")
    rr((x + 40, 282, x + 420, 684), "#151A22", TOKENS["query"], 12, 2)
    tx(x + 60, 302, "Selected SCC · exact local detail", 10, TOKENS["query"], "bold")
    for index, label in enumerate(("Branch", "Set Visited", "Advance Counter")):
        y = 350 + index * 98
        rr((x + 150, y, x + 310, y + 50), "#211B2B", TOKENS["query"], 8)
        tx(x + 166, y + 12, label, 10, TOKENS["text"], "bold")
        if index < 2:
            draw.line((x + 230, y + 50, x + 230, y + 98), fill=TOKENS["core"], width=3)
    draw.line((x + 150, 546, x + 112, 546, x + 112, 374, x + 150, 374), fill=TOKENS["query"], width=3)
    rr((x + 330, 350, x + 408, 400), "#16261D", TOKENS["criterion"], 8)
    tx(x + 342, 366, "EXIT", 9, TOKENS["criterion"], "bold")
    # INDEX: many SCCs become an index; one item owns detail.
    x = 1010
    rr((x + 20, 190, x + 150, 704), TOKENS["surface_alt"], TOKENS["line"], 10)
    tx(x + 36, 208, "SCC INDEX", 9, TOKENS["muted"], "bold")
    rows = (("SCC 1 · 6", False), ("SCC 2 · 3", True), ("SCC 3 · 11", False), ("SCC 4 · 4", False), ("+ 1 more", False))
    for index, (label, selected) in enumerate(rows):
        y = 246 + index * 58
        rr((x + 32, y, x + 138, y + 38), "#241D31" if selected else TOKENS["surface"], TOKENS["query"] if selected else TOKENS["line"], 7)
        tx(x + 46, y + 12, label, 8, TOKENS["query"] if selected else TOKENS["text"], "bold")
    rr((x + 168, 190, x + 440, 704), "#151A22", TOKENS["query"], 10, 2)
    tx(x + 188, 208, "SCC 2 · focused detail", 10, TOKENS["query"], "bold")
    for index, label in enumerate(("Branch", "Set Visited", "Advance")):
        y = 280 + index * 110
        rr((x + 222, y, x + 388, y + 52), "#211B2B", TOKENS["query"], 8)
        tx(x + 238, y + 13, label, 10, TOKENS["text"], "bold")
        if index < 2:
            draw.line((x + 305, y + 52, x + 305, y + 110), fill=TOKENS["core"], width=3)
    draw.line((x + 222, 500, x + 196, 500, x + 196, 306, x + 222, 306), fill=TOKENS["query"], width=3)
    rr((x + 222, 624, x + 388, 672), "#16261D", TOKENS["criterion"], 8)
    tx(x + 244, 638, "Criterion anchor", 9, TOKENS["criterion"], "bold")
    for x, _, _ in panels:
        tx(x + 22, 760, "First screen: no paragraph", 9, TOKENS["text"], "bold")
        tx(x + 22, 784, "Show complete text · Open source", 9, TOKENS["muted"], "regular")
        tx(x + 22, 816, "Mode changes disclosure, never truth", 8, TOKENS["core"], "bold")
    image.save(path, format="PNG", optimize=False, compress_level=9)


def build_manifest(truth: Mapping[str, Any], scenes: list[Mapping[str, Any]]) -> dict[str, Any]:
    information_sets = {scene["condition_id"]: scene_information_set(scene) for scene in scenes}
    return {
        "format": "blueprint-lens-lc7-adaptive-layout-manifest",
        "format_version": "1.0.0",
        "status": OUTPUT_STATUS,
        "profile_id": "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1",
        "claim_scope": "STATIC_SOURCE_VISIBLE_SCC",
        "comparison_control": "LC7_COMPLETE_TEXT",
        "recommended_condition_id": RECOMMENDED_CONDITION_ID,
        "selected_condition_id": RECOMMENDED_CONDITION_ID,
        "default_condition_id": None,
        "conditions": [
            {
                "condition_id": condition_id,
                "display_name": CONDITIONS[condition_id][0],
                "mechanism": CONDITIONS[condition_id][2],
                "disposition": (
                    "OWNER_SELECTED__IMPLEMENTATION_CANDIDATE"
                    if condition_id == RECOMMENDED_CONDITION_ID
                    else "NOT_SELECTED__RETAINED_COMPARISON"
                ),
            }
            for condition_id in CONDITION_IDS
        ],
        "fixed_variables": [
            "unit membership and source identity",
            "relation identity and ownership",
            "short labels and runtime boundary",
            "actions and one-selection disclosure depth",
        ],
        "independent_variables": ["topology placement", "containment", "route versus ledger geometry"],
        "information_sets": information_sets,
        "scale_policy": {
            "trigger_basis": "measured viewport and geometry budget; no fixed node-count threshold is claimed",
            "FULL": "show every accountable fact directly when it fits",
            "FOCUS": "replace off-focus context with counted recoverable folds; keep one SCC exact",
            "INDEX": "show SCC index and one selected exact detail when multiple SCCs exceed the viewport",
            "invariant": "mode changes disclosure only; no unit, relation, uncertainty, boundary, or source action is silently removed",
        },
        "decision": {
            "date": "2026-08-17",
            "owner_decision": "select A3",
            "selected_mechanism": (
                "context cue plus focused recurrence backbone with isolated entry, "
                "predicate, return, and exit geometry"
            ),
            "selection_basis": [
                "preserves the accepted A2 reading order",
                "removes the prior close parallel SCC bracket and separates relation-family attachment points",
                "supports FULL, FOCUS, and INDEX disclosure without fixing layout to the three-member fixture",
            ],
            "does_not_establish": [
                "product default",
                "human superiority",
                "real large-slice scalability",
                "Slate fidelity",
            ],
        },
        "defect_ledger": [
            {
                "defect_id": "LC7-VIS-D01",
                "status": "RESOLVED_IN_A3_DESIGN",
                "observation": "earlier graph candidates produced too many dense or nearly coincident routes",
                "resolution": (
                    "A3 uses a bounded context cue, one focused SCC spine, "
                    "and isolated attachment geometry"
                ),
            },
            {
                "defect_id": "LC7-VIS-D02",
                "status": "RESOLVED_IN_A3_DESIGN",
                "observation": (
                    "A2 brought differently coloured relation families into the same narrow visual corridor"
                ),
                "resolution": (
                    "entry, predicate, recurrence, forward execution, and exit use distinct attachment points; "
                    "the SCC bracket became a borderless member band"
                ),
            },
            {
                "defect_id": "LC7-VIS-D03",
                "status": "NOT_SELECTED_ALTERNATIVE",
                "observation": "B2 was visually simple but used excessive orthogonal bends and weak criterion placement",
                "resolution": (
                    "B3 is retained as a line-free relation ledger; "
                    "it is not the selected topology surface"
                ),
            },
            {
                "defect_id": "LC7-VIS-D04",
                "status": "RESOLVED_IN_CURRENT_AUTHORING",
                "observation": "the first B3 draft placed its final relation row too close to the overview boundary",
                "resolution": "predicate cards and rows were compacted; all commands are in bounds",
            },
            {
                "defect_id": "LC7-VIS-D05",
                "status": "RESOLVED_IN_CURRENT_AUTHORING",
                "observation": "the first C3 draft retained a long folded exit route to a low criterion",
                "resolution": (
                    "criterion and OUT port moved above the SCC so the exit is local; C3 remains unselected"
                ),
            },
            {
                "defect_id": "LC7-VIS-R01",
                "status": "OPEN_VALIDATION_RISK",
                "observation": "the only real LC7 fixture has three SCC members and one entry/exit",
                "next_validation": (
                    "apply A3 to future real medium/large slices and multi-SCC cases "
                    "before making a scalability claim"
                ),
            },
            {
                "defect_id": "LC7-VIS-R02",
                "status": "OPEN_VALIDATION_RISK",
                "observation": (
                    "FULL, FOCUS, and INDEX are synthetic scale-policy illustrations "
                    "without frozen activation thresholds"
                ),
                "next_validation": (
                    "derive transitions from measured viewport and geometry budgets "
                    "and prove counted-fold recoverability"
                ),
            },
            {
                "defect_id": "LC7-VIS-R03",
                "status": "OPEN_TARGET_RENDERER_REVIEW",
                "observation": (
                    "A3 retains one long returning route although it has two bends "
                    "and no collinear overlap"
                ),
                "next_validation": (
                    "inspect the 430/480/700 Slate renders and selected/detail states "
                    "for clearance and traceability"
                ),
            },
            {
                "defect_id": "LC7-VIS-R04",
                "status": "OPEN_VALIDATION_RISK",
                "observation": (
                    "no native Slate, source interaction, human comprehension, preference, "
                    "or product-default evidence exists"
                ),
                "next_validation": (
                    "complete responsive authoring, implementation, automation, exact-width Editor review, "
                    "and later authorised evaluation"
                ),
            },
        ],
        "truth_binding": truth["binding"],
        "non_claims": [*NON_CLAIMS, "synthetic FULL/FOCUS/INDEX board is policy illustration, not a validated large fixture"],
    }


def _hashes(directory: Path, names: list[str]) -> dict[str, Any]:
    return {
        "format": "blueprint-lens-self-excluding-hashes",
        "format_version": "1.0.0",
        "excluded": ["hashes.json"],
        "files": {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in sorted(names)},
    }


def validate_packet(directory: str | Path) -> dict[str, Any]:
    root = Path(directory)
    missing = sorted(set(OUTPUT_FILES) - {path.name for path in root.iterdir() if path.is_file()})
    _require(not missing, f"adaptive LC7 packet is missing files: {missing}")
    hashes = json.loads((root / "hashes.json").read_text(encoding="utf-8"))
    expected = _hashes(root, [name for name in OUTPUT_FILES if name != "hashes.json"])
    _require(hashes == expected, "adaptive LC7 packet hash inventory differs")
    oracle = json.loads((root / "lc7-adaptive-layout-oracle.json").read_text(encoding="utf-8"))
    _require(oracle.get("status") == "PASS", "adaptive LC7 oracle does not pass")
    return {"status": "PASS", "files": len(OUTPUT_FILES), "hashed_files": len(expected["files"])}


def build_adaptive_layout_artifacts(evidence_dir: str | Path, output_dir: str | Path) -> dict[str, Path]:
    truth = load_lc7_visual_truth(evidence_dir)
    target = Path(output_dir)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lc7-adaptive-", dir=target.parent) as temp_name:
        temp = Path(temp_name)
        scenes = [build_scene(truth, condition_id) for condition_id in CONDITION_IDS]
        checks = {scene["condition_id"]: adaptive_scene_checks(scene, truth) for scene in scenes}
        _require(all(check["pass"] for check in checks.values()), f"adaptive LC7 scene checks failed: {checks}")
        effect_paths: dict[str, Path] = {}
        for scene in scenes:
            condition_id = scene["condition_id"]
            slug = CONDITIONS[condition_id][1]
            svg_path = temp / f"{slug}.svg"
            png_path = temp / f"{slug}.png"
            svg_path.write_text(svg_for_scene(scene, truth), encoding="utf-8", newline="\n")
            png_for_scene(scene, truth, png_path)
            effect_paths[condition_id] = png_path
        (temp / "lc7-adaptive-layout-comparison-board.svg").write_text(
            _comparison_board_svg(scenes, truth), encoding="utf-8", newline="\n"
        )
        _comparison_board_png(effect_paths, temp / "lc7-adaptive-layout-comparison-board.png")
        _scale_policy_board(temp / "lc7-adaptive-scale-policy-board.png")
        (temp / "lc7-adaptive-layout-manifest.json").write_bytes(_json_bytes(build_manifest(truth, scenes)))
        oracle = {
            "format": "blueprint-lens-lc7-adaptive-layout-oracle",
            "format_version": "1.0.0",
            "status": "PASS",
            "canvas": list(CANVAS),
            "checks": checks,
        }
        (temp / "lc7-adaptive-layout-oracle.json").write_bytes(_json_bytes(oracle))
        hash_names = [name for name in OUTPUT_FILES if name != "hashes.json"]
        (temp / "hashes.json").write_bytes(_json_bytes(_hashes(temp, hash_names)))
        validate_packet(temp)
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(temp, target)
    return {name: target / name for name in OUTPUT_FILES}
