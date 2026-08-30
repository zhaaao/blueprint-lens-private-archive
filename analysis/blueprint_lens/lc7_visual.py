"""Deterministic 700px authoring effects for the frozen LC7 static-SCC truth."""

from __future__ import annotations

import hashlib
import html
import json
import math
import os
from pathlib import Path
import shutil
import tempfile
from types import MappingProxyType
from typing import Any, Mapping

from PIL import Image, ImageDraw

from blueprint_lens.lc6_visual import FONTS, TOKENS, _draw_dashed_segment, _font


class LC7VisualError(ValueError):
    """Raised when an LC7 visual projection differs from frozen truth."""


CONDITIONS = MappingProxyType(
    {
        "LC7_PORT_DOCKED_CYCLE_SPINE": (
            "Port-Docked Cycle Spine",
            "lc7-port-docked-cycle-spine-effect-700",
            "local predicate dock with a central cycle spine and isolated return and exit lanes",
        ),
        "LC7_DOCKED_CYCLE_RING": (
            "Docked Cycle Ring",
            "lc7-docked-cycle-ring-effect-700",
            "local predicate dock with three short SCC relation sides arranged as a directed ring",
        ),
        "LC7_SEGMENTED_RELATION_BAND": (
            "Segmented Relation Band",
            "lc7-segmented-relation-band-effect-700",
            "local predicate dock with individually accountable relations placed on one cycle band",
        ),
    }
)
CONDITION_IDS = tuple(CONDITIONS)
RECOMMENDED_CONDITION_ID = "LC7_PORT_DOCKED_CYCLE_SPINE"
CANVAS = (700, 760)
HEADER_BOUNDS = (24, 24, 652, 100)
OVERVIEW_BOUNDS = (24, 142, 420, 540)
DETAIL_BOUNDS = (458, 142, 218, 540)
ACTIONS_BOUNDS = (24, 700, 652, 36)
QUESTION = (
    "Which source-visible units and relations form the recurrence upstream of Set LC7Complete?"
)
ACTIONS = ("Inspect cycle", "Show complete text", "Open source")
NON_CLAIMS = (
    "Runtime iterations: NOT_CLAIMED",
    "authoring target only; not Slate or UE-visible evidence",
    "no human comprehension, preference, scalability, superiority or product-default evidence",
)
EXPECTED_UNITS = MappingProxyType(
    {
        "event": (
            "unit.control.d1cf3d85-4bef-3d12-afee-379a21e7ad2c",
            "LC7_STATIC_SCC",
        ),
        "initialise": (
            "unit.control.3cb511cf-454a-72bb-42b4-0f9ca778afec",
            "Set LC7Counter",
        ),
        "branch": (
            "unit.control.cbd7b169-4f74-9746-29b8-539f9f4310f9",
            "Branch",
        ),
        "visited": (
            "unit.control.08b1b7d1-4cfa-e2de-8089-b5832da5a751",
            "Set LC7Visited",
        ),
        "advance": (
            "unit.control.0805eaa8-47d4-f0af-bd2f-ada703333412",
            "Set LC7Counter",
        ),
        "criterion": (
            "unit.criterion.c0a8dfab-41b4-77d0-0c2f-19b475bd5dac",
            "Set LC7Complete",
        ),
        "get_counter": (
            "unit.predicate.4f0babd6-4b21-68c6-e817-00bb773b9b47",
            "Get LC7Counter",
        ),
        "compare": (
            "unit.predicate.06b0187f-460a-e671-1c92-929e0d8457b9",
            "integer < integer",
        ),
    }
)
EXPECTED_RELATIONS = MappingProxyType(
    {
        "event_to_initialise": ("event", "initialise", "then"),
        "initialise_to_branch": ("initialise", "branch", "then"),
        "branch_to_visited": ("branch", "visited", "then"),
        "visited_to_advance": ("visited", "advance", "then"),
        "advance_to_branch": ("advance", "branch", "then"),
        "branch_to_criterion": ("branch", "criterion", "else"),
        "get_to_compare": ("get_counter", "compare", "A"),
        "compare_to_branch": ("compare", "branch", "Condition"),
    }
)
NODE_SUBTITLES = MappingProxyType(
    {
        "event": "entry event",
        "initialise": "outside SCC",
        "branch": "SCC member · entry / exit",
        "visited": "SCC member",
        "advance": "SCC member",
        "criterion": "criterion",
        "get_counter": "predicate input",
        "compare": "opaque predicate",
    }
)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC7VisualError(message)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC7VisualError(f"cannot read LC7 visual input: {path}") from error
    _require(isinstance(value, dict), f"LC7 visual input is not an object: {path}")
    return value


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_lc7_visual_truth(evidence_dir: str | Path) -> dict[str, Any]:
    """Load and bind the frozen Explanation and SCC profile used by every effect."""

    root = Path(evidence_dir)
    explanation_path = root / "BP_LC7_StaticSCC.explanation.v1.json"
    profile_path = root / "BP_LC7_StaticSCC.scc-profile.v1.json"
    reviewed_path = root / "reviewed-ground-truth.v1.json"
    explanation = _read_json(explanation_path)
    profile = _read_json(profile_path)
    reviewed = _read_json(reviewed_path)
    _require(explanation.get("format") == "blueprint-lens-explanation", "LC7 Explanation format differs")
    _require(profile.get("profile_id") == "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1", "LC7 profile id differs")
    _require(profile.get("claim_scope") == "STATIC_SOURCE_VISIBLE_SCC", "LC7 claim scope differs")
    _require(profile.get("runtime_iterations") == "NOT_CLAIMED", "LC7 runtime boundary differs")
    _require(reviewed.get("review", {}).get("status") == "frozen", "LC7 reviewed truth is not frozen")
    units = {unit["id"]: unit for unit in explanation.get("units", [])}
    _require(len(units) == 8, "LC7 visible unit inventory must contain 8 unique units")
    for key, (unit_id, title) in EXPECTED_UNITS.items():
        _require(unit_id in units, f"LC7 unit is missing: {key}")
        _require(units[unit_id].get("title") == title, f"LC7 unit title differs: {key}")
    relations = {relation["id"]: relation for relation in explanation.get("relations", [])}
    _require(len(relations) == 8, "LC7 visible relation inventory must contain 8 unique relations")
    relation_ids: dict[str, str] = {}
    for key, (source_key, target_key, label) in EXPECTED_RELATIONS.items():
        source_id = EXPECTED_UNITS[source_key][0]
        target_id = EXPECTED_UNITS[target_key][0]
        matches = [
            relation_id
            for relation_id, relation in relations.items()
            if relation.get("source_unit_id") == source_id
            and relation.get("target_unit_id") == target_id
            and relation.get("label") == label
        ]
        _require(len(matches) == 1, f"LC7 relation binding differs: {key}")
        relation_ids[key] = matches[0]
    group = explanation.get("groups", [{}])[0]
    member_ids = set(group.get("ordered_unit_ids", []))
    expected_members = {
        EXPECTED_UNITS["branch"][0],
        EXPECTED_UNITS["visited"][0],
        EXPECTED_UNITS["advance"][0],
    }
    _require(group.get("kind") == "scc", "LC7 Explanation group is not an SCC")
    _require(group.get("projection_status") == "STRUCTURAL_ONLY", "LC7 SCC projection status differs")
    _require(member_ids == expected_members, "LC7 SCC member inventory differs")
    _require(profile.get("counts", {}).get("scc_members") == 3, "LC7 SCC member count differs")
    _require(profile.get("counts", {}).get("internal_edges") == 3, "LC7 internal relation count differs")
    return {
        "question": explanation["query"]["question"],
        "criterion_unit_id": explanation["criterion_unit_id"],
        "units": units,
        "relations": relations,
        "relation_ids": relation_ids,
        "scc_group": group,
        "profile": profile,
        "binding": {
            "explanation_path": explanation_path.as_posix(),
            "explanation_sha256": _sha256(explanation_path),
            "profile_path": profile_path.as_posix(),
            "profile_sha256": _sha256(profile_path),
            "reviewed_path": reviewed_path.as_posix(),
            "reviewed_sha256": _sha256(reviewed_path),
        },
    }


def _command(kind: str, command_id: str, **values: Any) -> dict[str, Any]:
    return {"kind": kind, "id": command_id, **values}


def _rect(commands: list[dict[str, Any]], command_id: str, x: int, y: int, width: int, height: int, *, fill: str, stroke: str | None = None, stroke_width: int = 1, radius: int = 8, **metadata: Any) -> None:
    commands.append(_command("rect", command_id, x=x, y=y, width=width, height=height, fill=fill, stroke=stroke, stroke_width=stroke_width, radius=radius, **metadata))


def _text(commands: list[dict[str, Any]], command_id: str, x: int, y: int, value: str, *, fill: str = TOKENS["text"], font_size: int = 11, weight: str = "regular", **metadata: Any) -> None:
    commands.append(_command("text", command_id, x=x, y=y, text=value, fill=fill, font_size=font_size, weight=weight, **metadata))


def _polyline(commands: list[dict[str, Any]], command_id: str, points: list[tuple[int, int]], *, stroke: str, stroke_width: int = 2, dash: tuple[int, int] | None = None, **metadata: Any) -> None:
    commands.append(_command("polyline", command_id, points=points, stroke=stroke, stroke_width=stroke_width, dash=list(dash) if dash else [], **metadata))


def _arrowhead(commands: list[dict[str, Any]], command_id: str, points: list[tuple[int, int]], *, fill: str) -> None:
    start_x, start_y = points[-2]
    end_x, end_y = points[-1]
    angle = math.atan2(end_y - start_y, end_x - start_x)
    length = 7
    spread = 4
    base_x = end_x - length * math.cos(angle)
    base_y = end_y - length * math.sin(angle)
    left = (round(base_x + spread * math.sin(angle)), round(base_y - spread * math.cos(angle)))
    right = (round(base_x - spread * math.sin(angle)), round(base_y + spread * math.cos(angle)))
    commands.append(_command("polygon", command_id, points=[(end_x, end_y), left, right], fill=fill, stroke=fill, stroke_width=1))


def _route(commands: list[dict[str, Any]], truth: Mapping[str, Any], key: str, points: list[tuple[int, int]], *, label: str, stroke: str, dash: tuple[int, int] | None = None, label_at: tuple[int, int] | None = None) -> None:
    relation_id = truth["relation_ids"][key]
    _polyline(commands, f"route.{key}", points, stroke=stroke, dash=dash, relation_id=relation_id, relation_key=key)
    _arrowhead(commands, f"route.{key}.arrow", points, fill=stroke)
    if label_at:
        _text(commands, f"route.{key}.label", label_at[0], label_at[1], label.upper(), fill=stroke, font_size=8, weight="bold")


def _node(commands: list[dict[str, Any]], truth: Mapping[str, Any], key: str, box: tuple[int, int, int, int]) -> None:
    x, y, width, height = box
    unit_id, title = EXPECTED_UNITS[key]
    role = truth["units"][unit_id]["role"]
    status = truth["units"][unit_id]["semantic_status"]
    if key == "criterion":
        fill, stroke = "#16261D", TOKENS["criterion"]
    elif key in {"get_counter", "compare"}:
        fill, stroke = "#241F16", TOKENS["opaque"]
    elif key in {"branch", "visited", "advance"}:
        fill, stroke = "#211B2B", TOKENS["query"]
    else:
        fill, stroke = TOKENS["surface"], TOKENS["core"]
    _rect(commands, f"node.{key}", x, y, width, height, fill=fill, stroke=stroke, stroke_width=1, radius=7, unit_id=unit_id, unit_key=key)
    _text(commands, f"node.{key}.title", x + 9, y + 7, title, font_size=10, weight="bold", unit_ref_id=unit_id)
    subtitle = NODE_SUBTITLES[key]
    subtitle_colour = TOKENS["opaque"] if status == "opaque" else TOKENS["muted"]
    _text(commands, f"node.{key}.subtitle", x + 9, y + 23, subtitle, fill=subtitle_colour, font_size=8, unit_ref_id=unit_id, role=role)
    commands.append(_command("circle", f"node.{key}.source", cx=x + width - 9, cy=y + 9, radius=3, fill=stroke))


def _base_scene(condition_id: str) -> list[dict[str, Any]]:
    label = CONDITIONS[condition_id][0]
    commands: list[dict[str, Any]] = []
    _rect(commands, "canvas", 0, 0, CANVAS[0], CANVAS[1], fill=TOKENS["background"], stroke=None, radius=0)
    _rect(commands, "header", *HEADER_BOUNDS, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=10)
    _text(commands, "header.title", 42, 40, "Static Recurrence Overview", font_size=18, weight="bold")
    _text(commands, "header.question", 42, 76, QUESTION, font_size=10, weight="bold", fact_id="shared.question")
    _text(commands, "header.state", 606, 42, "NEUTRAL", fill=TOKENS["muted"], font_size=7, weight="bold")
    _rect(commands, "overview", *OVERVIEW_BOUNDS, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=10)
    _text(commands, "overview.condition", 40, 158, label.upper(), fill=TOKENS["core"], font_size=10, weight="bold")
    _rect(commands, "overview.count-chip", 40, 180, 194, 24, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=12)
    _text(commands, "overview.count-chip.text", 51, 186, "STATIC SCC · 3 MEMBERS · 8 RELATIONS", fill=TOKENS["query"], font_size=8, weight="bold", fact_id="shared.counts")
    _rect(commands, "overview.runtime-chip", 244, 180, 184, 24, fill="#241D31", stroke=TOKENS["query"], radius=12)
    _text(commands, "overview.runtime-chip.text", 254, 186, "RUNTIME ITERATIONS · NOT CLAIMED", fill=TOKENS["query"], font_size=8, weight="bold", fact_id="shared.runtime_boundary")
    _rect(commands, "detail", *DETAIL_BOUNDS, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=10)
    _text(commands, "detail.label", 474, 158, "DETAIL", fill=TOKENS["muted"], font_size=9, weight="bold")
    _text(commands, "detail.title", 474, 208, "Select a unit", font_size=15, weight="bold")
    _text(commands, "detail.help", 474, 232, "to inspect cycle evidence", fill=TOKENS["muted"], font_size=9)
    for index, tab in enumerate(("SUMMARY", "RELATIONS", "EVIDENCE")):
        _rect(commands, f"detail.tab.{tab.casefold()}", 474, 266 + index * 38, 184, 28, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=5)
        _text(commands, f"detail.tab.{tab.casefold()}.text", 486, 274 + index * 38, tab, fill=TOKENS["muted"], font_size=8, weight="bold")
    _polyline(commands, "detail.legend.execution", [(478, 410), (506, 410)], stroke=TOKENS["core"], stroke_width=3)
    _text(commands, "detail.legend.execution.text", 516, 404, "Execution route", font_size=9, weight="bold")
    _polyline(commands, "detail.legend.recurrence", [(478, 450), (506, 450)], stroke=TOKENS["query"], stroke_width=3)
    _text(commands, "detail.legend.recurrence.text", 516, 444, "Returning edge", font_size=9, weight="bold")
    _polyline(commands, "detail.legend.data", [(478, 490), (506, 490)], stroke=TOKENS["opaque"], stroke_width=2, dash=(5, 4))
    _text(commands, "detail.legend.data.text", 516, 484, "Predicate data", font_size=9, weight="bold")
    _text(commands, "detail.fixed", 474, 636, "Overview positions stay fixed", fill=TOKENS["muted"], font_size=8)
    _rect(commands, "actions", *ACTIONS_BOUNDS, fill=TOKENS["surface"], stroke=TOKENS["line"], radius=8)
    button_widths = (112, 142, 100)
    x = 38
    for index, (label_text, width) in enumerate(zip(ACTIONS, button_widths, strict=True)):
        _rect(commands, f"action.{index}", x, 706, width, 24, fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=5)
        _text(commands, f"action.{index}.text", x + 12, 712, label_text, font_size=8, weight="bold", action_id=label_text.casefold().replace(" ", "_"))
        x += width + 8
    _text(commands, "actions.state", 558, 712, "AUTHORING · NOT SLATE", fill=TOKENS["muted"], font_size=7, weight="bold")
    return commands


def _scc_bracket(commands: list[dict[str, Any]], *, x: int, top: int, bottom: int, label_x: int, label_y: int) -> None:
    _polyline(commands, "scc.bracket", [(x + 10, top), (x, top), (x, bottom), (x + 10, bottom)], stroke=TOKENS["query"], stroke_width=2)
    _text(commands, "scc.bracket.label", label_x, label_y, "SCC · 3", fill=TOKENS["query"], font_size=8, weight="bold", fact_id="shared.scc_membership")


def _predicate_dock(commands: list[dict[str, Any]], *, x: int, y: int, width: int, height: int) -> None:
    _rect(commands, "predicate.dock", x, y, width, height, fill="#201C17", stroke=TOKENS["opaque"], radius=9)
    _text(commands, "predicate.dock.label", x + 10, y + 6, "LOCAL PREDICATE DOCK", fill=TOKENS["opaque"], font_size=7, weight="bold")


def _port_docked_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_PORT_DOCKED_CYCLE_SPINE")
    _predicate_dock(commands, x=176, y=216, width=252, height=76)
    boxes = {
        "event": (42, 236, 118, 38),
        "initialise": (42, 294, 124, 40),
        "get_counter": (186, 236, 108, 40),
        "compare": (306, 236, 112, 40),
        "branch": (206, 330, 122, 42),
        "visited": (206, 416, 122, 42),
        "advance": (206, 502, 122, 42),
        "criterion": (282, 612, 136, 40),
    }
    _scc_bracket(commands, x=188, top=320, bottom=554, label_x=198, label_y=308)
    _text(commands, "grammar.spine", 238, 562, "CYCLE SPINE", fill=TOKENS["muted"], font_size=8, weight="bold")
    _text(commands, "grammar.return", 166, 474, "RETURN LANE", fill=TOKENS["query"], font_size=7, weight="bold")
    _text(commands, "grammar.exit", 374, 474, "EXIT LANE", fill=TOKENS["criterion"], font_size=7, weight="bold")
    _route(commands, truth, "event_to_initialise", [(101, 274), (101, 294)], label="then", stroke=TOKENS["core"], label_at=(108, 279))
    _route(commands, truth, "initialise_to_branch", [(166, 314), (188, 314), (188, 344), (206, 344)], label="then", stroke=TOKENS["core"], label_at=(170, 300))
    _route(commands, truth, "get_to_compare", [(294, 256), (306, 256)], label="A", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(297, 242))
    _route(commands, truth, "compare_to_branch", [(362, 276), (362, 306), (267, 306), (267, 330)], label="condition", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(278, 294))
    _route(commands, truth, "branch_to_visited", [(267, 372), (267, 416)], label="then", stroke=TOKENS["core"], label_at=(274, 387))
    _route(commands, truth, "visited_to_advance", [(267, 458), (267, 502)], label="then", stroke=TOKENS["core"], label_at=(274, 473))
    _route(commands, truth, "advance_to_branch", [(206, 523), (182, 523), (182, 358), (206, 358)], label="then · return", stroke=TOKENS["query"], label_at=(166, 548))
    _route(commands, truth, "branch_to_criterion", [(328, 351), (426, 351), (426, 632), (418, 632)], label="else", stroke=TOKENS["criterion"], label_at=(378, 338))
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def _cycle_ring_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_DOCKED_CYCLE_RING")
    _predicate_dock(commands, x=176, y=216, width=252, height=76)
    boxes = {
        "event": (38, 316, 120, 38),
        "initialise": (38, 372, 120, 40),
        "get_counter": (186, 236, 108, 40),
        "compare": (306, 236, 112, 40),
        "branch": (220, 324, 126, 42),
        "visited": (184, 448, 120, 42),
        "advance": (312, 448, 112, 42),
        "criterion": (286, 612, 132, 40),
    }
    _text(commands, "ring.label", 250, 516, "DIRECTED SCC RING · 3", fill=TOKENS["query"], font_size=8, weight="bold", fact_id="shared.scc_membership")
    _route(commands, truth, "event_to_initialise", [(98, 354), (98, 372)], label="then", stroke=TOKENS["core"], label_at=(105, 358))
    _route(commands, truth, "initialise_to_branch", [(158, 392), (188, 392), (188, 345), (220, 345)], label="then", stroke=TOKENS["core"], label_at=(166, 378))
    _route(commands, truth, "get_to_compare", [(294, 256), (306, 256)], label="A", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(297, 242))
    _route(commands, truth, "compare_to_branch", [(362, 276), (362, 304), (283, 304), (283, 324)], label="condition", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(292, 292))
    _route(commands, truth, "branch_to_visited", [(248, 366), (248, 412), (244, 412), (244, 448)], label="then", stroke=TOKENS["core"], label_at=(254, 384))
    _route(commands, truth, "visited_to_advance", [(304, 469), (312, 469)], label="then", stroke=TOKENS["core"], label_at=(302, 478))
    _route(commands, truth, "advance_to_branch", [(368, 448), (368, 412), (316, 412), (316, 366)], label="then · return", stroke=TOKENS["query"], label_at=(326, 396))
    _route(commands, truth, "branch_to_criterion", [(346, 345), (428, 345), (428, 632), (418, 632)], label="else", stroke=TOKENS["criterion"], label_at=(374, 332))
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def _relation_band_scene(truth: Mapping[str, Any]) -> list[dict[str, Any]]:
    commands = _base_scene("LC7_SEGMENTED_RELATION_BAND")
    _predicate_dock(commands, x=176, y=216, width=252, height=76)
    _rect(commands, "band.entry-dock", 36, 322, 130, 100, fill=TOKENS["core_fill"], stroke=TOKENS["core"], radius=9)
    _text(commands, "band.entry-dock.label", 48, 332, "ENTRY DOCK", fill=TOKENS["core"], font_size=8, weight="bold")
    _rect(commands, "band.scc", 184, 310, 240, 282, fill="#1C1825", stroke=TOKENS["query"], stroke_width=2, radius=12)
    _text(commands, "band.scc.label", 200, 322, "SEGMENTED SCC BAND · 3", fill=TOKENS["query"], font_size=9, weight="bold", fact_id="shared.scc_membership")
    _text(commands, "band.scc.boundary", 200, 340, "each segment owns one relation", fill=TOKENS["muted"], font_size=8)
    boxes = {
        "event": (46, 350, 110, 34),
        "initialise": (46, 390, 110, 34),
        "get_counter": (186, 236, 108, 40),
        "compare": (306, 236, 112, 40),
        "branch": (242, 360, 124, 40),
        "visited": (242, 438, 124, 40),
        "advance": (242, 516, 124, 40),
        "criterion": (286, 622, 132, 34),
    }
    _route(commands, truth, "event_to_initialise", [(101, 384), (101, 390)], label="then", stroke=TOKENS["core"], label_at=(108, 382))
    _route(commands, truth, "initialise_to_branch", [(156, 407), (176, 407), (176, 380), (242, 380)], label="then", stroke=TOKENS["core"], label_at=(184, 366))
    _route(commands, truth, "get_to_compare", [(294, 256), (306, 256)], label="A", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(297, 242))
    _route(commands, truth, "compare_to_branch", [(362, 276), (362, 350), (304, 350), (304, 360)], label="condition", stroke=TOKENS["opaque"], dash=(5, 4), label_at=(366, 304))
    _route(commands, truth, "branch_to_visited", [(304, 400), (304, 438)], label="then", stroke=TOKENS["core"], label_at=(311, 411))
    _route(commands, truth, "visited_to_advance", [(304, 478), (304, 516)], label="then", stroke=TOKENS["core"], label_at=(311, 489))
    _route(commands, truth, "advance_to_branch", [(242, 536), (208, 536), (208, 380), (242, 380)], label="then · return", stroke=TOKENS["query"], label_at=(194, 558))
    _route(commands, truth, "branch_to_criterion", [(366, 380), (414, 380), (414, 639), (418, 639)], label="else", stroke=TOKENS["criterion"], label_at=(376, 366))
    for key, box in boxes.items():
        _node(commands, truth, key, box)
    return commands


def build_lc7_scene(truth: Mapping[str, Any], condition_id: str) -> dict[str, Any]:
    """Build one information-matched neutral authoring scene."""

    _require(condition_id in CONDITIONS, f"unknown LC7 condition: {condition_id}")
    builders = {
        "LC7_PORT_DOCKED_CYCLE_SPINE": _port_docked_scene,
        "LC7_DOCKED_CYCLE_RING": _cycle_ring_scene,
        "LC7_SEGMENTED_RELATION_BAND": _relation_band_scene,
    }
    return {
        "format": "blueprint-lens-lc7-authoring-scene",
        "format_version": "1.0.0",
        "condition_id": condition_id,
        "condition_label": CONDITIONS[condition_id][0],
        "disclosure_state": "NEUTRAL",
        "width": CANVAS[0],
        "height": CANVAS[1],
        "commands": builders[condition_id](truth),
    }


def _command_bounds(command: Mapping[str, Any]) -> tuple[int, int, int, int] | None:
    if command["kind"] == "rect":
        return command["x"], command["y"], command["x"] + command["width"], command["y"] + command["height"]
    if command["kind"] == "text":
        left, top, right, bottom = _font(command["font_size"], command["weight"]).getbbox(command["text"])
        return command["x"] + left, command["y"] + top, command["x"] + right, command["y"] + bottom
    if command["kind"] in {"polyline", "polygon"}:
        xs = [point[0] for point in command["points"]]
        ys = [point[1] for point in command["points"]]
        return min(xs), min(ys), max(xs), max(ys)
    if command["kind"] == "circle":
        return command["cx"] - command["radius"], command["cy"] - command["radius"], command["cx"] + command["radius"], command["cy"] + command["radius"]
    return None


def scene_information_set(scene: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "unit_ids": sorted(command["unit_id"] for command in scene["commands"] if command.get("unit_id")),
        "relation_ids": sorted(command["relation_id"] for command in scene["commands"] if command.get("relation_id")),
        "actions": sorted(command["action_id"] for command in scene["commands"] if command.get("action_id")),
        "fact_ids": sorted(command["fact_id"] for command in scene["commands"] if command.get("fact_id")),
    }


def scene_checks(scene: Mapping[str, Any], truth: Mapping[str, Any]) -> dict[str, Any]:
    commands = scene.get("commands", [])
    ids = [command.get("id") for command in commands]
    duplicate_ids = sorted({command_id for command_id in ids if ids.count(command_id) > 1})
    malformed_ids = sorted(str(command.get("id")) for command in commands if command.get("kind") not in {"rect", "text", "polyline", "polygon", "circle"})
    out_of_bounds = []
    for command in commands:
        bounds = _command_bounds(command)
        if bounds and (bounds[0] < 0 or bounds[1] < 0 or bounds[2] > CANVAS[0] or bounds[3] > CANVAS[1]):
            out_of_bounds.append(command["id"])
    information = scene_information_set(scene)
    expected_units = sorted(unit_id for unit_id, _ in EXPECTED_UNITS.values())
    expected_relations = sorted(truth["relations"])
    expected_actions = sorted(action.casefold().replace(" ", "_") for action in ACTIONS)
    required_facts = sorted(("shared.counts", "shared.question", "shared.runtime_boundary", "shared.scc_membership"))
    relation_route_lengths = {
        command["id"]: sum(
            abs(end[0] - start[0]) + abs(end[1] - start[1])
            for start, end in zip(command["points"], command["points"][1:])
        )
        for command in commands
        if command.get("relation_id") and command.get("kind") == "polyline"
    }
    long_relation_route_ids = sorted(
        command_id
        for command_id, length in relation_route_lengths.items()
        if length > 180
    )
    checks = {
        "canvas": [scene.get("width"), scene.get("height")],
        "duplicate_command_ids": duplicate_ids,
        "malformed_command_ids": malformed_ids,
        "out_of_bounds_ids": sorted(out_of_bounds),
        "unit_identity_errors": [] if information["unit_ids"] == expected_units else ["unit_identity"],
        "relation_identity_errors": [] if information["relation_ids"] == expected_relations else ["relation_identity"],
        "action_errors": [] if information["actions"] == expected_actions else ["actions"],
        "first_screen_fact_errors": [] if information["fact_ids"] == required_facts else ["first_screen_facts"],
        "route_density_errors": [] if len(long_relation_route_ids) <= 2 else ["more_than_two_long_relation_routes"],
        "relation_route_lengths": relation_route_lengths,
        "long_relation_route_ids": long_relation_route_ids,
        "command_count": len(commands),
        "information_sha256": hashlib.sha256((json.dumps(information, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")).hexdigest(),
    }
    checks["pass"] = checks["canvas"] == list(CANVAS) and not any(value for key, value in checks.items() if key not in {"canvas", "command_count", "information_sha256", "relation_route_lengths", "long_relation_route_ids", "pass"})
    return checks


def svg_for_scene(scene: Mapping[str, Any], truth: Mapping[str, Any]) -> str:
    checks = scene_checks(scene, truth)
    _require(checks["pass"], f"cannot render invalid LC7 scene: {checks}")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{scene["width"]}" height="{scene["height"]}" viewBox="0 0 {scene["width"]} {scene["height"]}">',
        f'  <title>{html.escape(scene["condition_label"])} · NEUTRAL</title>',
    ]
    for command in scene["commands"]:
        command_id = html.escape(str(command["id"]), quote=True)
        if command["kind"] == "rect":
            lines.append(f'  <rect id="{command_id}" x="{command["x"]}" y="{command["y"]}" width="{command["width"]}" height="{command["height"]}" rx="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"] or "none"}" stroke-width="{command["stroke_width"]}"/>')
        elif command["kind"] == "text":
            weight = 700 if command["weight"] == "bold" else 400
            lines.append(f'  <text id="{command_id}" x="{command["x"]}" y="{command["y"]}" fill="{command["fill"]}" font-family="Segoe UI" font-size="{command["font_size"]}" font-weight="{weight}" dominant-baseline="text-before-edge">{html.escape(str(command["text"]))}</text>')
        elif command["kind"] == "polyline":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            dash = f' stroke-dasharray="{",".join(map(str, command["dash"]))}"' if command["dash"] else ""
            lines.append(f'  <polyline id="{command_id}" points="{points}" fill="none" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"{dash} stroke-linecap="round" stroke-linejoin="round"/>')
        elif command["kind"] == "polygon":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            lines.append(f'  <polygon id="{command_id}" points="{points}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>')
        elif command["kind"] == "circle":
            lines.append(f'  <circle id="{command_id}" cx="{command["cx"]}" cy="{command["cy"]}" r="{command["radius"]}" fill="{command["fill"]}"/>')
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def png_for_scene(scene: Mapping[str, Any], truth: Mapping[str, Any], path: str | Path) -> None:
    checks = scene_checks(scene, truth)
    _require(checks["pass"], f"cannot render invalid LC7 scene: {checks}")
    image = Image.new("RGB", CANVAS, TOKENS["background"])
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        if command["kind"] == "rect":
            draw.rounded_rectangle([command["x"], command["y"], command["x"] + command["width"], command["y"] + command["height"]], radius=command["radius"], fill=command["fill"], outline=command["stroke"], width=command["stroke_width"])
        elif command["kind"] == "text":
            draw.text((command["x"], command["y"]), command["text"], font=_font(command["font_size"], command["weight"]), fill=command["fill"], anchor="lt")
        elif command["kind"] == "polyline":
            points = [tuple(point) for point in command["points"]]
            if command["dash"]:
                for start, end in zip(points, points[1:]):
                    _draw_dashed_segment(draw, start, end, command["stroke"], command["stroke_width"], command["dash"])
            else:
                draw.line(points, fill=command["stroke"], width=command["stroke_width"], joint="curve")
        elif command["kind"] == "polygon":
            draw.polygon([tuple(point) for point in command["points"]], fill=command["fill"], outline=command["stroke"])
        elif command["kind"] == "circle":
            draw.ellipse([command["cx"] - command["radius"], command["cy"] - command["radius"], command["cx"] + command["radius"], command["cy"] + command["radius"]], fill=command["fill"])
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target, format="PNG", optimize=False, compress_level=9)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _board_svg(scenes: list[Mapping[str, Any]], truth: Mapping[str, Any]) -> str:
    width, height = 2180, 840
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "  <title>LC7 static SCC · information-matched 700px comparison</title>",
        f'  <rect width="{width}" height="{height}" fill="{TOKENS["background"]}"/>',
        f'  <text x="20" y="18" fill="{TOKENS["text"]}" font-family="Segoe UI" font-size="20" font-weight="700" dominant-baseline="text-before-edge">LC7 static SCC · information-matched 700px comparison</text>',
        f'  <text x="20" y="48" fill="{TOKENS["muted"]}" font-family="Segoe UI" font-size="11" dominant-baseline="text-before-edge">Same 8 units, 8 relations, 3 SCC members and runtime boundary; only placement, containment and route geometry vary.</text>',
    ]
    for index, scene in enumerate(scenes):
        x = 20 + index * 720
        body = svg_for_scene(scene, truth).splitlines()[3:-1]
        lines.append(f'  <g id="board{index}" transform="translate({x} 76)">')
        lines.extend("  " + line.replace('id="', f'id="board{index}__') for line in body)
        lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _board_png(scenes: list[Mapping[str, Any]], effect_paths: Mapping[str, Path], path: Path) -> None:
    image = Image.new("RGB", (2180, 840), TOKENS["background"])
    draw = ImageDraw.Draw(image)
    draw.text((20, 18), "LC7 static SCC · information-matched 700px comparison", font=_font(20, "bold"), fill=TOKENS["text"], anchor="lt")
    draw.text((20, 48), "Same 8 units, 8 relations, 3 SCC members and runtime boundary; only placement, containment and route geometry vary.", font=_font(11, "regular"), fill=TOKENS["muted"], anchor="lt")
    for index, scene in enumerate(scenes):
        slug = CONDITIONS[scene["condition_id"]][1]
        with Image.open(effect_paths[f"{slug}.png"]) as effect:
            _require(effect.size == CANVAS, f"LC7 effect dimensions differ: {slug}.png")
            image.paste(effect.convert("RGB"), (20 + index * 720, 76))
    image.save(path, format="PNG", optimize=False, compress_level=9)


def build_manifest(truth: Mapping[str, Any], scenes: list[Mapping[str, Any]]) -> dict[str, Any]:
    information_sets = {scene["condition_id"]: scene_information_set(scene) for scene in scenes}
    _require(len({json.dumps(value, sort_keys=True) for value in information_sets.values()}) == 1, "LC7 candidates are not information matched")
    return {
        "format": "blueprint-lens-lc7-visual-comparison-manifest",
        "format_version": "1.0.0",
        "status": "PROPOSED_VISUAL_DESIGN_ONLY",
        "profile_id": truth["profile"]["profile_id"],
        "claim_scope": truth["profile"]["claim_scope"],
        "canvas": {"width": 700, "height": 760},
        "comparison_control": "LC7_COMPLETE_TEXT",
        "recommended_condition_id": RECOMMENDED_CONDITION_ID,
        "selected_condition_id": None,
        "default_condition_id": None,
        "conditions": [
            {"condition_id": condition_id, "display_name": CONDITIONS[condition_id][0], "mechanism": CONDITIONS[condition_id][2]}
            for condition_id in CONDITION_IDS
        ],
        "shared_information": {
            "question": truth["question"],
            "criterion_unit_id": truth["criterion_unit_id"],
            "unit_ids": sorted(truth["units"]),
            "relation_ids": sorted(truth["relations"]),
            "scc_member_ids": sorted(truth["scc_group"]["ordered_unit_ids"]),
            "runtime_iterations": "NOT_CLAIMED",
            "actions": list(ACTIONS),
            "neutral_disclosure": "no member preselected; detail modes are mutually exclusive after selection",
            "complete_text": "permanent fail-closed route; not a fourth graphical candidate",
        },
        "independent_variables": ["topology placement", "containment", "route geometry"],
        "fixed_variables": ["membership", "relation ownership", "short labels", "source identities", "boundary facts", "actions", "maximum one-selection disclosure depth"],
        "truth_binding": truth["binding"],
        "information_sets": information_sets,
        "non_claims": list(NON_CLAIMS),
    }


def _packet_hashes(directory: Path, names: list[str]) -> dict[str, Any]:
    entries = []
    for name in sorted(names):
        payload = (directory / name).read_bytes()
        entries.append({"path": name, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
    return {"format": "blueprint-lens-lc7-visual-self-excluding-hashes", "format_version": "1.0.0", "algorithm": "sha256", "excluded_path": "hashes.json", "entry_count": len(entries), "entries": entries}


def _directories_identical(first: Path, second: Path) -> bool:
    first_entries = sorted((path.name, path.is_file()) for path in first.iterdir())
    second_entries = sorted((path.name, path.is_file()) for path in second.iterdir())
    if first_entries != second_entries or not all(is_file for _, is_file in first_entries):
        return False
    return all((first / name).read_bytes() == (second / name).read_bytes() for name, _ in first_entries)


def validate_packet(directory: str | Path) -> dict[str, Any]:
    root = Path(directory)
    entries = sorted(path for path in root.iterdir() if path.is_file())
    _require(len(entries) == 11, "LC7 visual packet must contain exactly 11 files")
    hashes = _read_json(root / "hashes.json")
    _require(hashes.get("excluded_path") == "hashes.json" and hashes.get("entry_count") == 10, "LC7 visual hash inventory is not self-excluding 10/11")
    expected_names = {path.name for path in entries} - {"hashes.json"}
    _require({item.get("path") for item in hashes.get("entries", [])} == expected_names, "LC7 visual hash coverage differs")
    for item in hashes["entries"]:
        payload = (root / item["path"]).read_bytes()
        _require(item.get("bytes") == len(payload) and item.get("sha256") == hashlib.sha256(payload).hexdigest(), f"LC7 visual hash differs: {item['path']}")
    return {"status": "PASS", "files": 11, "hashed_files": 10}


def build_lc7_visual_artifacts(evidence_dir: str | Path, output_dir: str | Path) -> dict[str, Path]:
    """Atomically publish the deterministic 11-file LC7 visual proposal packet."""

    destination = Path(output_dir).resolve()
    _require(not destination.exists() or destination.is_dir(), "LC7 visual destination is not a directory")
    for font_path in FONTS.values():
        _require(Path(font_path).is_file(), f"LC7 visual font is unavailable: {font_path}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{destination.name}.staging-", dir=destination.parent))
    try:
        truth = load_lc7_visual_truth(evidence_dir)
        scenes = [build_lc7_scene(truth, condition_id) for condition_id in CONDITION_IDS]
        checks = {scene["condition_id"]: scene_checks(scene, truth) for scene in scenes}
        _require(all(check["pass"] for check in checks.values()), "LC7 visual geometry/identity oracle failed")
        _require(len({check["information_sha256"] for check in checks.values()}) == 1, "LC7 visual information hashes differ")
        files: dict[str, Path] = {}
        manifest_name = "lc7-visual-comparison-manifest.json"
        oracle_name = "lc7-visual-oracle.json"
        (staging / manifest_name).write_bytes(_json_bytes(build_manifest(truth, scenes)))
        oracle = {"format": "blueprint-lens-lc7-visual-oracle", "format_version": "1.0.0", "status": "PASS", "canvas": list(CANVAS), "checks": checks}
        (staging / oracle_name).write_bytes(_json_bytes(oracle))
        files[manifest_name] = staging / manifest_name
        files[oracle_name] = staging / oracle_name
        for scene in scenes:
            slug = CONDITIONS[scene["condition_id"]][1]
            svg_name, png_name = f"{slug}.svg", f"{slug}.png"
            (staging / svg_name).write_text(svg_for_scene(scene, truth), encoding="utf-8", newline="\n")
            png_for_scene(scene, truth, staging / png_name)
            files[svg_name] = staging / svg_name
            files[png_name] = staging / png_name
        board_svg_name = "lc7-static-scc-comparison-board.svg"
        board_png_name = "lc7-static-scc-comparison-board.png"
        (staging / board_svg_name).write_text(_board_svg(scenes, truth), encoding="utf-8", newline="\n")
        _board_png(scenes, files, staging / board_png_name)
        files[board_svg_name] = staging / board_svg_name
        files[board_png_name] = staging / board_png_name
        _require(len(files) == 10, "LC7 visual pre-hash file count differs")
        (staging / "hashes.json").write_bytes(_json_bytes(_packet_hashes(staging, list(files))))
        files["hashes.json"] = staging / "hashes.json"
        validate_packet(staging)
        if destination.exists():
            _require(_directories_identical(staging, destination), "LC7 visual destination exists with different bytes")
        else:
            os.replace(staging, destination)
        return {name: destination / name for name in sorted(files)}
    finally:
        if staging.exists():
            shutil.rmtree(staging)
