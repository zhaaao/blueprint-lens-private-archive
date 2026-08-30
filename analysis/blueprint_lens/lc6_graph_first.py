"""Graph-first progressive-disclosure authoring views for frozen LC6 truth."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import tempfile
from types import MappingProxyType
from typing import Any, Mapping

from PIL import Image, ImageDraw

from blueprint_lens.lc6_visual import (
    FONTS,
    SCENARIO_ORDER,
    TOKENS,
    _draw_dashed_segment,
    _font,
    _text_bounds,
    information_set,
    load_lc6_visual_ledger as _load_lc6_visual_ledger,
    validate_lc6_visual_ledger,
)


class LC6GraphFirstError(ValueError):
    """Raised when the graph-first projection differs from accepted LC6 truth."""


GRAPH_CONDITIONS = MappingProxyType(
    {
        "LC6_SPLIT_FRONTIER_ROUTES": (
            "Four-Track Boundary Overview",
            "lc6-four-track-boundary",
        ),
        "LC6_KNOWN_UNKNOWN_THRESHOLD": (
            "Dual-Domain Threshold Overview",
            "lc6-dual-domain-threshold",
        ),
    }
)
GRAPH_CONDITION_IDS = tuple(GRAPH_CONDITIONS)
DISCLOSURE_STATES = ("NEUTRAL", "CORE_SELECTED", "QUERY_SELECTED")
SELECTED_SCENARIO_BY_STATE = MappingProxyType(
    {
        "NEUTRAL": None,
        "CORE_SELECTED": "LC6_UNCERTAIN",
        "QUERY_SELECTED": "LC6_TRUNCATED",
    }
)
RECOMMENDED_CONDITION_ID = "LC6_SPLIT_FRONTIER_ROUTES"
CANVAS = (700, 760)
HEADER_BOUNDS = (24, 24, 652, 84)
OVERVIEW_BOUNDS = (24, 126, 404, 570)
DETAIL_BOUNDS = (442, 126, 234, 570)
ACTIONS_BOUNDS = (24, 714, 652, 30)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC6GraphFirstError(message)


def load_lc6_visual_ledger(evidence_dir):
    """Expose the frozen first-packet ledger loader to graph-first callers."""

    return _load_lc6_visual_ledger(evidence_dir)


def display_facts(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Flatten every accountable LC6 fact into one canonical fact map."""

    validate_lc6_visual_ledger(ledger)
    facts: dict[str, Any] = {
        "shared.question": ledger["question"],
        "shared.asset_path": ledger["asset_path"],
        "shared.graph_id": ledger["graph_id"],
    }
    for key, value in ledger["profile_binding"].items():
        facts[f"evidence.{key}"] = deepcopy(value)
    for scenario in ledger["scenarios"]:
        prefix = f"scenario.{scenario['scenario_id']}"
        for key, value in scenario.items():
            if key == "scenario_id":
                continue
            facts[f"{prefix}.{key}"] = deepcopy(value)
    for action in ledger["actions"]:
        facts[f"action.{action['action_id']}"] = action["label"]
    for index, claim in enumerate(ledger["non_claims"]):
        facts[f"nonclaim.{index}"] = claim
    return facts


def _overview_fact_ids(ledger: Mapping[str, Any]) -> list[str]:
    ids = ["shared.question", "action.show_complete_text"]
    for scenario in ledger["scenarios"]:
        prefix = f"scenario.{scenario['scenario_id']}"
        ids.extend(
            [
                f"{prefix}.truth_owner",
                f"{prefix}.status",
                f"{prefix}.criterion_node_id",
                f"{prefix}.criterion_title",
            ]
        )
        if scenario["scenario_id"] == "LC6_TRUNCATED":
            ids.extend([f"{prefix}.frontiers", f"{prefix}.counts"])
        else:
            ids.extend(
                [f"{prefix}.boundary_node_id", f"{prefix}.boundary_title"]
            )
    return ids


def _summary_fact_ids(scenario_id: str) -> list[str]:
    prefix = f"scenario.{scenario_id}"
    return [
        f"{prefix}.reason",
        f"{prefix}.stop_kind",
        f"{prefix}.root_node_id",
        f"{prefix}.root_title",
        "action.open_source",
    ]


def _relation_fact_ids(ledger: Mapping[str, Any], scenario_id: str) -> list[str]:
    _require(
        any(item["scenario_id"] == scenario_id for item in ledger["scenarios"]),
        "unknown LC6 relation scenario",
    )
    prefix = f"scenario.{scenario_id}"
    keys = ["nodes", "edges"]
    if scenario_id == "LC6_TRUNCATED":
        keys.extend(
            [
                "max_upstream_hops",
                "complete_nodes",
                "complete_edges",
            ]
        )
    else:
        keys.extend(["counts", "incident_edge_ids"])
    return [f"{prefix}.{key}" for key in keys]


def _evidence_fact_ids(facts: Mapping[str, Any]) -> list[str]:
    return sorted(
        key
        for key in facts
        if key.startswith("evidence.") or key == "action.show_evidence"
    )


def recoverability_contract(
    ledger: Mapping[str, Any], selected_scenario_id: str | None
) -> dict[str, list[str]]:
    """Partition complete LC6 information by disclosure depth."""

    _require(
        selected_scenario_id is None or selected_scenario_id in SCENARIO_ORDER,
        "unknown LC6 selected scenario",
    )
    facts = display_facts(ledger)
    overview = _overview_fact_ids(ledger)
    summary = (
        [] if selected_scenario_id is None else _summary_fact_ids(selected_scenario_id)
    )
    relations = (
        []
        if selected_scenario_id is None
        else _relation_fact_ids(ledger, selected_scenario_id)
    )
    evidence = [] if selected_scenario_id is None else _evidence_fact_ids(facts)
    assigned = set(overview) | set(summary) | set(relations) | set(evidence)
    fallback = sorted(set(facts) - assigned)
    contract = {
        "overview_visible": overview,
        "summary_visible": summary,
        "relations_collapsed": relations,
        "evidence_collapsed": evidence,
        "global_fallback": fallback,
    }
    flat = [item for values in contract.values() for item in values]
    _require(
        set(flat) == set(facts) and len(flat) == len(set(flat)),
        "LC6 recoverability partitions do not cover every fact exactly once",
    )
    return contract


def _expected_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    validate_lc6_visual_ledger(ledger)
    states = []
    for condition_id, (_, slug) in GRAPH_CONDITIONS.items():
        for disclosure_state in DISCLOSURE_STATES:
            selected = SELECTED_SCENARIO_BY_STATE[disclosure_state]
            state_slug = disclosure_state.casefold().replace("_", "-")
            states.append(
                {
                    "state_id": f"{condition_id}__{disclosure_state}__W700",
                    "condition_id": condition_id,
                    "disclosure_state": disclosure_state,
                    "selected_scenario_id": selected,
                    "width": 700,
                    "recoverability": recoverability_contract(ledger, selected),
                    "ledger_semantic_sha256": ledger["semantic_sha256"],
                    "evidence_state": "authoring_design_target",
                    "effect_paths": {
                        "svg": f"{slug}-{state_slug}-700.svg",
                        "png": f"{slug}-{state_slug}-700.png",
                    },
                }
            )
    return {
        "format": "blueprint-lens-lc6-graph-first-manifest",
        "format_version": "1.0.0",
        "status": "GRAPH_FIRST_DISCLOSURE_ACCEPTED__SIX_STATES",
        "recommended_condition_id": RECOMMENDED_CONDITION_ID,
        "selected_condition_id": None,
        "default_condition_id": None,
        "complete_text_fallback_condition_id": "LC6_COMPLETE_TEXT",
        "historical_packet": {
            "path": "artifacts/r1/lc6-visual-candidates",
            "disposition": "REVISE__TEXT_DOMINANT",
        },
        "conditions": [
            {"condition_id": condition_id, "label": label}
            for condition_id, (label, _) in GRAPH_CONDITIONS.items()
        ],
        "states": states,
        "information_set": information_set(ledger),
    }


def build_graph_first_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Build the exact accepted six-state graph-first manifest."""

    manifest = _expected_manifest(ledger)
    validate_graph_first_manifest(manifest, ledger)
    return manifest


def validate_graph_first_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    """Reject every deviation from the accepted disclosure contract."""

    _require(
        manifest == _expected_manifest(ledger),
        "LC6 graph-first manifest differs from accepted disclosure contract",
    )


def _add_rect(
    commands: list[dict[str, Any]],
    command_id: str,
    bounds: tuple[int, int, int, int],
    *,
    fill: str,
    stroke: str | None = None,
    stroke_width: int = 1,
    radius: int = 0,
    region: str | None = None,
    scenario_id: str | None = None,
    role: str | None = None,
    selected: bool = False,
    fact_id: str | None = None,
) -> None:
    x, y, width, height = bounds
    commands.append(
        {
            "kind": "rect",
            "id": command_id,
            "x": x,
            "y": y,
            "width": width,
            "height": height,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "radius": radius,
            "region": region,
            "scenario_id": scenario_id,
            "semantic_role": role,
            "selected": selected,
            "fact_id": fact_id,
        }
    )


def _add_text(
    commands: list[dict[str, Any]],
    command_id: str,
    x: int,
    y: int,
    text: str,
    *,
    size: int = 9,
    weight: str = "regular",
    fill: str | None = None,
    region: str | None = None,
    scenario_id: str | None = None,
    role: str | None = None,
    fact_id: str | None = None,
    fact_ref_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "text",
            "id": command_id,
            "x": x,
            "y": y,
            "text": text,
            "font_size": size,
            "weight": weight,
            "font_path": FONTS[weight],
            "fill": fill or TOKENS["text"],
            "bounds": _text_bounds(x, y, text, size, weight),
            "region": region,
            "scenario_id": scenario_id,
            "semantic_role": role,
            "fact_id": fact_id,
            "fact_ref_id": fact_ref_id,
        }
    )


def _add_polyline(
    commands: list[dict[str, Any]],
    command_id: str,
    points: list[list[int]],
    *,
    stroke: str,
    width: int = 2,
    dash: list[int] | None = None,
    region: str = "overview",
    scenario_id: str | None = None,
    role: str | None = None,
    start_anchor_id: str | None = None,
    end_anchor_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "polyline",
            "id": command_id,
            "points": points,
            "stroke": stroke,
            "stroke_width": width,
            "dash": dash,
            "region": region,
            "scenario_id": scenario_id,
            "semantic_role": role,
            "start_anchor_id": start_anchor_id,
            "end_anchor_id": end_anchor_id,
        }
    )


def _add_polygon(
    commands: list[dict[str, Any]],
    command_id: str,
    points: list[list[int]],
    *,
    fill: str,
    stroke: str,
    width: int = 1,
    region: str = "overview",
    scenario_id: str | None = None,
    role: str | None = None,
    fact_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "polygon",
            "id": command_id,
            "points": points,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": width,
            "region": region,
            "scenario_id": scenario_id,
            "semantic_role": role,
            "fact_id": fact_id,
        }
    )


def _add_circle(
    commands: list[dict[str, Any]],
    command_id: str,
    cx: int,
    cy: int,
    radius: int,
    *,
    fill: str,
    region: str = "overview",
    scenario_id: str | None = None,
    role: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "circle",
            "id": command_id,
            "cx": cx,
            "cy": cy,
            "radius": radius,
            "fill": fill,
            "stroke": None,
            "stroke_width": 0,
            "region": region,
            "scenario_id": scenario_id,
            "semantic_role": role,
        }
    )


def _status_colour(status: str) -> str:
    return str(TOKENS[status])


def _scenario_by_id(ledger: Mapping[str, Any], scenario_id: str) -> Mapping[str, Any]:
    return next(item for item in ledger["scenarios"] if item["scenario_id"] == scenario_id)


def _base_commands(
    condition_id: str, disclosure_state: str
) -> list[dict[str, Any]]:
    commands: list[dict[str, Any]] = []
    for command_id, bounds, fill in (
        ("canvas.background", (0, 0, *CANVAS), TOKENS["background"]),
        ("region.header", HEADER_BOUNDS, TOKENS["surface"]),
        ("region.overview", OVERVIEW_BOUNDS, TOKENS["surface"]),
        ("region.detail", DETAIL_BOUNDS, TOKENS["surface_alt"]),
        ("region.actions", ACTIONS_BOUNDS, TOKENS["surface"]),
    ):
        _add_rect(
            commands,
            command_id,
            bounds,
            fill=fill,
            stroke=None if command_id == "canvas.background" else TOKENS["line"],
            radius=0 if command_id == "canvas.background" else 10,
        )
    _add_text(
        commands,
        "header.title",
        42,
        38,
        GRAPH_CONDITIONS[condition_id][0],
        size=17,
        weight="bold",
        region="header",
    )
    _add_text(
        commands,
        "header.state",
        532,
        42,
        disclosure_state.replace("_", " "),
        size=8,
        weight="bold",
        fill=TOKENS["muted"],
        region="header",
    )
    _add_text(
        commands,
        "header.question",
        42,
        73,
        "Why does analysis stop here — and who owns the stop?",
        size=10,
        weight="bold",
        region="header",
        fact_id="shared.question",
    )
    return commands


def _four_track_overview(
    commands: list[dict[str, Any]],
    ledger: Mapping[str, Any],
    selected_scenario_id: str | None,
) -> None:
    _add_text(commands, "overview.core.owner", 40, 142, "CORE CLASSIFICATION · 3", size=9, weight="bold", fill=TOKENS["core"], region="overview")
    core_y = (176, 268, 360)
    for scenario, y in zip(ledger["scenarios"][:3], core_y):
        sid = scenario["scenario_id"]
        selected = sid == selected_scenario_id
        prefix = f"scenario.{sid}"
        _add_rect(commands, f"overview.{sid}.hit", (38, y - 8, 374, 78), fill="#1B2530" if selected else TOKENS["surface_alt"], stroke=_status_colour(scenario["status"]) if selected else TOKENS["line"], stroke_width=2 if selected else 1, radius=8, region="overview", scenario_id=sid, role="scenario_hit_target", selected=selected, fact_id=f"{prefix}.truth_owner")
        _add_text(commands, f"overview.{sid}.label", 48, y, sid.replace("LC6_", ""), size=9, weight="bold", fill=_status_colour(scenario["status"]), region="overview", scenario_id=sid, fact_id=f"{prefix}.status")
        _add_rect(commands, f"overview.{sid}.boundary", (48, y + 25, 78, 28), fill=TOKENS["background"], stroke=_status_colour(scenario["status"]), radius=5, region="overview", scenario_id=sid, role="semantic_boundary", fact_id=f"{prefix}.boundary_node_id")
        _add_text(commands, f"overview.{sid}.boundary.label", 56, y + 32, scenario["boundary_title"], size=8, weight="bold", region="overview", scenario_id=sid, fact_id=f"{prefix}.boundary_title")
        _add_polyline(commands, f"overview.{sid}.fence", [[137, y + 21], [137, y + 57]], stroke=_status_colour(scenario["status"]), width=3, scenario_id=sid, role="semantic_fence")
        _add_polyline(commands, f"overview.{sid}.route", [[140, y + 39], [318, y + 39]], stroke=TOKENS["core"], width=3, scenario_id=sid, role="known_route")
        _add_rect(commands, f"overview.{sid}.criterion", (318, y + 25, 84, 28), fill=TOKENS["surface"], stroke=TOKENS["criterion"], radius=5, region="overview", scenario_id=sid, role="criterion_dock", fact_id=f"{prefix}.criterion_node_id")
        _add_text(commands, f"overview.{sid}.criterion.label", 326, y + 32, scenario["criterion_title"].replace("Set LC6", "Set "), size=7, weight="bold", fill=TOKENS["criterion"], region="overview", scenario_id=sid, fact_id=f"{prefix}.criterion_title")
        _add_circle(commands, f"overview.{sid}.criterion.dot", 393, y + 39, 3, fill=TOKENS["criterion"], scenario_id=sid, role="criterion_marker")
    query = ledger["scenarios"][3]
    sid = query["scenario_id"]
    prefix = f"scenario.{sid}"
    selected = sid == selected_scenario_id
    _add_text(commands, "overview.query.owner", 40, 458, "QUERY BUDGET · 1", size=9, weight="bold", fill=TOKENS["query"], region="overview")
    _add_rect(commands, f"overview.{sid}.hit", (38, 482, 374, 192), fill="#2A2035" if selected else TOKENS["query_fill"], stroke=TOKENS["query"] if selected else TOKENS["line"], stroke_width=2 if selected else 1, radius=8, region="overview", scenario_id=sid, role="scenario_hit_target", selected=selected, fact_id=f"{prefix}.truth_owner")
    _add_text(commands, f"overview.{sid}.label", 48, 494, "TRUNCATED", size=9, weight="bold", fill=TOKENS["query"], region="overview", scenario_id=sid, fact_id=f"{prefix}.status")
    _add_rect(commands, f"overview.{sid}.omitted", (48, 531, 62, 42), fill=TOKENS["background"], stroke=TOKENS["muted"], radius=5, region="overview", scenario_id=sid, role="omitted_aggregate", fact_id=f"{prefix}.counts")
    _add_text(commands, f"overview.{sid}.omitted.a", 56, 539, "3 nodes", size=8, weight="bold", fill=TOKENS["muted"], region="overview", scenario_id=sid)
    _add_text(commands, f"overview.{sid}.omitted.b", 56, 554, "3 edges", size=8, fill=TOKENS["muted"], region="overview", scenario_id=sid)
    _add_polyline(commands, f"overview.{sid}.route", [[110, 552], [318, 552]], stroke=TOKENS["query"], width=3, dash=[7, 4], scenario_id=sid, role="known_route")
    _add_polygon(commands, f"overview.{sid}.frontier", [[126, 538], [139, 552], [126, 566], [113, 552]], fill=TOKENS["background"], stroke=TOKENS["query"], width=2, scenario_id=sid, role="frontier_break", fact_id=f"{prefix}.frontiers")
    for index, x in enumerate((158, 198, 238, 278)):
        _add_rect(commands, f"overview.{sid}.node.{index}", (x, 540, 30, 24), fill=TOKENS["surface"], stroke=TOKENS["query"], radius=5, region="overview", scenario_id=sid, role="selected_node")
        _add_text(commands, f"overview.{sid}.node.{index}.label", x + 9, 546, str(index + 3), size=7, weight="bold", region="overview", scenario_id=sid)
    _add_rect(commands, f"overview.{sid}.criterion", (318, 538, 84, 28), fill=TOKENS["surface"], stroke=TOKENS["criterion"], radius=5, region="overview", scenario_id=sid, role="criterion_dock", fact_id=f"{prefix}.criterion_node_id")
    _add_text(commands, f"overview.{sid}.criterion.label", 326, 545, "Set …06", size=8, weight="bold", fill=TOKENS["criterion"], region="overview", scenario_id=sid, fact_id=f"{prefix}.criterion_title")
    _add_circle(commands, f"overview.{sid}.criterion.dot", 393, 552, 3, fill=TOKENS["criterion"], scenario_id=sid, role="criterion_marker")
    _add_text(commands, f"overview.{sid}.caption", 48, 594, "budget 3 · selected 4/3 · complete 7/6", size=8, region="overview", scenario_id=sid)
    _add_text(commands, f"overview.{sid}.hint", 48, 633, "Select any track to inspect the stopping evidence →", size=8, fill=TOKENS["muted"], region="overview", scenario_id=sid)


def _dual_domain_overview(
    commands: list[dict[str, Any]],
    ledger: Mapping[str, Any],
    selected_scenario_id: str | None,
) -> None:
    _add_text(commands, "overview.core.owner", 40, 142, "CORE CLASSIFICATION OWNER", size=9, weight="bold", fill=TOKENS["core"], region="overview")
    _add_rect(commands, "overview.core.domain", (38, 164, 374, 292), fill=TOKENS["core_fill"], stroke=TOKENS["line"], radius=8, region="overview", role="owner_domain")
    _add_text(commands, "overview.threshold.known", 50, 174, "KNOWN ANSWER", size=7, weight="bold", fill=TOKENS["criterion"], region="overview")
    _add_text(commands, "overview.threshold.beyond", 300, 174, "BEYOND", size=7, weight="bold", fill=TOKENS["muted"], region="overview")
    for scenario, y in zip(ledger["scenarios"][:3], (206, 288, 370)):
        sid = scenario["scenario_id"]
        prefix = f"scenario.{sid}"
        selected = sid == selected_scenario_id
        _add_rect(commands, f"overview.{sid}.hit", (46, y - 8, 358, 68), fill="#1B3040" if selected else TOKENS["core_fill"], stroke=_status_colour(scenario["status"]) if selected else TOKENS["line"], stroke_width=2 if selected else 1, radius=7, region="overview", scenario_id=sid, role="scenario_hit_target", selected=selected, fact_id=f"{prefix}.truth_owner")
        _add_text(commands, f"overview.{sid}.label", 54, y, sid.replace("LC6_", ""), size=8, weight="bold", fill=_status_colour(scenario["status"]), region="overview", scenario_id=sid, fact_id=f"{prefix}.status")
        _add_rect(commands, f"overview.{sid}.criterion", (54, y + 25, 120, 25), fill=TOKENS["surface"], stroke=TOKENS["criterion"], radius=5, region="overview", scenario_id=sid, role="criterion_dock", fact_id=f"{prefix}.criterion_node_id")
        _add_text(commands, f"overview.{sid}.criterion.label", 62, y + 31, scenario["criterion_title"].replace("Set LC6", "Set "), size=7, weight="bold", fill=TOKENS["criterion"], region="overview", scenario_id=sid, fact_id=f"{prefix}.criterion_title")
        _add_polyline(commands, f"overview.{sid}.route", [[174, y + 38], [284, y + 38]], stroke=TOKENS["muted"], width=2, dash=[5, 4], scenario_id=sid, role="threshold_crossing")
        _add_polyline(commands, f"overview.{sid}.threshold", [[250, y + 20], [250, y + 54]], stroke=_status_colour(scenario["status"]), width=3, scenario_id=sid, role="semantic_fence")
        _add_rect(commands, f"overview.{sid}.boundary", (284, y + 25, 108, 25), fill=TOKENS["background"], stroke=_status_colour(scenario["status"]), radius=5, region="overview", scenario_id=sid, role="semantic_boundary", fact_id=f"{prefix}.boundary_node_id")
        _add_text(commands, f"overview.{sid}.boundary.label", 292, y + 31, scenario["boundary_title"], size=7, weight="bold", region="overview", scenario_id=sid, fact_id=f"{prefix}.boundary_title")
        _add_circle(commands, f"overview.{sid}.criterion.dot", 166, y + 38, 3, fill=TOKENS["criterion"], scenario_id=sid, role="criterion_marker")
    query = ledger["scenarios"][3]
    sid = query["scenario_id"]
    prefix = f"scenario.{sid}"
    selected = sid == selected_scenario_id
    _add_text(commands, "overview.query.owner", 40, 474, "QUERY BUDGET OWNER", size=9, weight="bold", fill=TOKENS["query"], region="overview")
    _add_rect(commands, "overview.query.domain", (38, 496, 374, 178), fill=TOKENS["query_fill"], stroke=TOKENS["query"] if selected else TOKENS["line"], stroke_width=2 if selected else 1, radius=8, region="overview", scenario_id=sid, role="scenario_hit_target", selected=selected, fact_id=f"{prefix}.truth_owner")
    _add_text(commands, f"overview.{sid}.label", 50, 508, "TRUNCATED · selected 4/3", size=8, weight="bold", fill=TOKENS["query"], region="overview", scenario_id=sid, fact_id=f"{prefix}.status")
    _add_rect(commands, f"overview.{sid}.criterion", (50, 542, 130, 30), fill=TOKENS["surface"], stroke=TOKENS["criterion"], radius=5, region="overview", scenario_id=sid, role="criterion_dock", fact_id=f"{prefix}.criterion_node_id")
    _add_text(commands, f"overview.{sid}.criterion.label", 60, 550, "Known route → Set …06", size=7, weight="bold", fill=TOKENS["criterion"], region="overview", scenario_id=sid, fact_id=f"{prefix}.criterion_title")
    _add_polyline(commands, f"overview.{sid}.route", [[180, 557], [294, 557]], stroke=TOKENS["query"], width=3, dash=[6, 4], scenario_id=sid, role="threshold_crossing")
    _add_polygon(commands, f"overview.{sid}.frontier", [[250, 543], [263, 557], [250, 571], [237, 557]], fill=TOKENS["background"], stroke=TOKENS["query"], width=2, scenario_id=sid, role="frontier_break", fact_id=f"{prefix}.frontiers")
    _add_rect(commands, f"overview.{sid}.omitted", (294, 538, 100, 38), fill=TOKENS["background"], stroke=TOKENS["query"], radius=5, region="overview", scenario_id=sid, role="omitted_aggregate", fact_id=f"{prefix}.counts")
    _add_text(commands, f"overview.{sid}.omitted.label", 303, 546, "Beyond · 3/3", size=8, weight="bold", fill=TOKENS["query"], region="overview", scenario_id=sid)
    _add_circle(commands, f"overview.{sid}.criterion.dot", 170, 557, 3, fill=TOKENS["criterion"], scenario_id=sid, role="criterion_marker")
    _add_text(commands, f"overview.{sid}.caption", 50, 598, "Frontier at the real crossing edge", size=8, region="overview", scenario_id=sid)
    _add_text(commands, f"overview.{sid}.hint", 50, 640, "Select a row to inspect the evidence →", size=8, fill=TOKENS["muted"], region="overview", scenario_id=sid)


def _detail_commands(
    commands: list[dict[str, Any]],
    ledger: Mapping[str, Any],
    selected_scenario_id: str | None,
) -> None:
    _add_text(commands, "detail.heading", 458, 144, "DETAIL", size=10, weight="bold", fill=TOKENS["muted"], region="detail")
    if selected_scenario_id is None:
        _add_text(commands, "detail.empty.title", 458, 190, "Select a scenario", size=14, weight="bold", region="detail")
        _add_text(commands, "detail.empty.subtitle", 458, 214, "to inspect why analysis stops", size=9, fill=TOKENS["muted"], region="detail")
        _add_polyline(commands, "detail.legend.core.fence", [[466, 278], [466, 310]], stroke=TOKENS["core"], width=3, region="detail", role="legend")
        _add_text(commands, "detail.legend.core", 482, 286, "Core semantic boundary", size=9, weight="bold", region="detail")
        _add_polygon(commands, "detail.legend.query.frontier", [[466, 348], [477, 360], [466, 372], [455, 360]], fill=TOKENS["background"], stroke=TOKENS["query"], width=2, region="detail", role="legend")
        _add_text(commands, "detail.legend.query", 486, 352, "Query Frontier Break", size=9, weight="bold", region="detail")
        _add_text(commands, "detail.empty.note", 458, 430, "Overview positions stay fixed", size=8, fill=TOKENS["muted"], region="detail")
        return
    scenario = _scenario_by_id(ledger, selected_scenario_id)
    prefix = f"scenario.{selected_scenario_id}"
    _add_text(commands, "detail.selected.title", 458, 174, selected_scenario_id.replace("LC6_", ""), size=13, weight="bold", fill=_status_colour(scenario["status"]), region="detail", scenario_id=selected_scenario_id)
    rows = [
        ("status", f"Status · {scenario['status']}", f"{prefix}.status", f"{prefix}.status"),
        ("owner", f"Owner · {scenario['truth_owner'].replace('_', ' ')}", f"{prefix}.truth_owner", f"{prefix}.truth_owner"),
        ("reason", f"Why · {scenario['reason']}", f"{prefix}.reason", None),
        ("stop", f"Stop · {scenario['stop_kind']}", f"{prefix}.stop_kind", None),
        ("root", f"Root · {scenario['root_title']}", f"{prefix}.root_title", f"{prefix}.root_node_id"),
        ("criterion", f"Criterion · {scenario['criterion_title']}", f"{prefix}.criterion_title", f"{prefix}.criterion_title"),
    ]
    if scenario["boundary_title"] is not None:
        rows.insert(5, ("boundary", f"Boundary · {scenario['boundary_title']}", f"{prefix}.boundary_title", f"{prefix}.boundary_title"))
    y = 210
    for row_id, text, fact_id, fact_ref_id in rows:
        _add_text(commands, f"detail.selected.{row_id}", 458, y, text, size=8, weight="bold" if row_id in {"status", "owner"} else "regular", region="detail", scenario_id=selected_scenario_id, fact_id=fact_id, fact_ref_id=fact_ref_id)
        y += 27
    contract = recoverability_contract(ledger, selected_scenario_id)
    for row_id, label, count, row_y in (
        ("relations", "Complete relations", len(contract["relations_collapsed"]), 420),
        ("evidence", "Technical evidence", len(contract["evidence_collapsed"]), 470),
    ):
        _add_rect(commands, f"detail.{row_id}.collapsed", (458, row_y, 202, 38), fill=TOKENS["background"], stroke=TOKENS["line"], radius=6, region="detail", scenario_id=selected_scenario_id, role="collapsed_disclosure")
        _add_text(commands, f"detail.{row_id}.label", 470, row_y + 8, f"› {label} · {count}", size=9, weight="bold", region="detail", scenario_id=selected_scenario_id)
    _add_rect(commands, "detail.open_source", (458, 540, 202, 36), fill=TOKENS["surface"], stroke=TOKENS["core"], radius=6, region="detail", scenario_id=selected_scenario_id, role="open_source_action")
    _add_text(commands, "detail.open_source.label", 474, 549, "Open source", size=9, weight="bold", region="detail", scenario_id=selected_scenario_id, fact_id="action.open_source")
    _add_text(commands, "detail.toggle.hint", 458, 610, "Click selected track again to clear", size=8, fill=TOKENS["muted"], region="detail")


def build_graph_first_scene(
    ledger: Mapping[str, Any],
    condition_id: str,
    disclosure_state: str,
    width: int = 700,
) -> dict[str, Any]:
    """Build one accepted graph-first authored interaction state."""

    validate_lc6_visual_ledger(ledger)
    _require(condition_id in GRAPH_CONDITION_IDS, "unknown LC6 graph-first condition")
    _require(disclosure_state in DISCLOSURE_STATES, "unknown LC6 disclosure state")
    _require(width == 700, "LC6 graph-first authoring is 700px only")
    selected = SELECTED_SCENARIO_BY_STATE[disclosure_state]
    commands = _base_commands(condition_id, disclosure_state)
    if condition_id == "LC6_SPLIT_FRONTIER_ROUTES":
        _four_track_overview(commands, ledger, selected)
    else:
        _dual_domain_overview(commands, ledger, selected)
    _detail_commands(commands, ledger, selected)
    _add_rect(commands, "action.complete_text", (38, 718, 190, 22), fill=TOKENS["surface_alt"], stroke=TOKENS["line"], radius=5, region="actions", role="complete_text_fallback")
    _add_text(commands, "action.complete_text.label", 54, 722, "Show complete text", size=8, weight="bold", region="actions", fact_id="action.show_complete_text")
    _add_text(commands, "action.scope", 454, 722, "AUTHORING STATE · NOT SLATE", size=7, weight="bold", fill=TOKENS["muted"], region="actions")
    contract = recoverability_contract(ledger, selected)
    return {
        "format": "blueprint-lens-lc6-graph-first-scene",
        "format_version": "1.0.0",
        "condition_id": condition_id,
        "condition_label": GRAPH_CONDITIONS[condition_id][0],
        "disclosure_state": disclosure_state,
        "selected_scenario_id": selected,
        "width": CANVAS[0],
        "height": CANVAS[1],
        "overview_bounds": list(OVERVIEW_BOUNDS),
        "detail_bounds": list(DETAIL_BOUNDS),
        "overview": {"scenario_ids": list(SCENARIO_ORDER)},
        "detail": {"selected_scenario_id": selected},
        "recoverability": contract,
        "information_sha256": hashlib.sha256(
            json.dumps(information_set(ledger), sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        "commands": commands,
    }


def next_selected_scenario(current: str | None, clicked: str) -> str | None:
    """Toggle one presentation-only LC6 scenario selection."""

    _require(clicked in SCENARIO_ORDER, "unknown LC6 scenario selection")
    return None if current == clicked else clicked


def _command_bounds(command: Mapping[str, Any]) -> list[float] | None:
    kind = command.get("kind")
    if kind == "rect":
        values = [command.get(key) for key in ("x", "y", "width", "height")]
        if not all(isinstance(value, (int, float)) for value in values):
            return None
        x, y, width, height = values
        return [x, y, x + width, y + height]
    if kind == "text":
        bounds = command.get("bounds")
        return list(bounds) if isinstance(bounds, list) and len(bounds) == 4 else None
    if kind == "circle":
        values = [command.get(key) for key in ("cx", "cy", "radius")]
        if not all(isinstance(value, (int, float)) for value in values):
            return None
        cx, cy, radius = values
        return [cx - radius, cy - radius, cx + radius, cy + radius]
    if kind in {"polyline", "polygon"}:
        points = command.get("points")
        if not isinstance(points, list) or len(points) < 2:
            return None
        half = float(command.get("stroke_width") or 0) / 2
        return [
            min(point[0] for point in points) - half,
            min(point[1] for point in points) - half,
            max(point[0] for point in points) + half,
            max(point[1] for point in points) + half,
        ]
    return None


def _overlap(first: list[float], second: list[float]) -> bool:
    return not (
        first[2] <= second[0]
        or second[2] <= first[0]
        or first[3] <= second[1]
        or second[3] <= first[1]
    )


def _segment_hits_rect(
    start: list[int], end: list[int], rect: list[float]
) -> bool:
    x1, y1 = start
    x2, y2 = end
    left, top, right, bottom = rect
    if x1 == x2:
        return left < x1 < right and max(y1, y2) > top and min(y1, y2) < bottom
    if y1 == y2:
        return top < y1 < bottom and max(x1, x2) > left and min(x1, x2) < right
    steps = max(abs(x2 - x1), abs(y2 - y1), 1)
    return any(
        left < x1 + (x2 - x1) * index / steps < right
        and top < y1 + (y2 - y1) * index / steps < bottom
        for index in range(steps + 1)
    )


def _canonical_overview_geometry(scene: Mapping[str, Any]) -> list[dict[str, Any]]:
    canonical = []
    for command in scene["commands"]:
        if command.get("region") != "overview":
            continue
        item = deepcopy(dict(command))
        for key in ("fill", "stroke", "stroke_width", "selected"):
            item.pop(key, None)
        canonical.append(item)
    return canonical


def overview_geometry_sha256(scene: Mapping[str, Any]) -> str:
    """Seal overview command identity/geometry while ignoring selection paint."""

    payload = json.dumps(
        _canonical_overview_geometry(scene),
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def graph_first_scene_checks(scene: Mapping[str, Any]) -> dict[str, Any]:
    """Validate graph-first semantic coverage and authored-state geometry."""

    commands = scene.get("commands", [])
    command_ids = [command.get("id") for command in commands]
    duplicate_ids = sorted(
        {command_id for command_id in command_ids if command_ids.count(command_id) > 1},
        key=str,
    )
    malformed_ids = []
    out_of_bounds_ids = []
    text_measurement_errors = []
    for index, command in enumerate(commands):
        command_id = str(command.get("id", index))
        bounds = _command_bounds(command)
        if bounds is None:
            malformed_ids.append(command_id)
            continue
        if (
            bounds[0] < 0
            or bounds[1] < 0
            or bounds[2] > scene.get("width", 0)
            or bounds[3] > scene.get("height", 0)
        ):
            out_of_bounds_ids.append(command_id)
        if command.get("kind") == "text" and command.get("bounds") != _text_bounds(
            command["x"],
            command["y"],
            command["text"],
            command["font_size"],
            command["weight"],
        ):
            text_measurement_errors.append(command_id)
    text_commands = [command for command in commands if command.get("kind") == "text"]
    text_overlap_pairs = []
    for index, first in enumerate(text_commands):
        first_bounds = _command_bounds(first)
        for second in text_commands[index + 1 :]:
            second_bounds = _command_bounds(second)
            if (
                first_bounds is not None
                and second_bounds is not None
                and _overlap(first_bounds, second_bounds)
            ):
                text_overlap_pairs.append([first["id"], second["id"]])
    route_text_collisions = []
    for route in (command for command in commands if command.get("kind") == "polyline"):
        if route.get("semantic_role") in {"legend", "semantic_fence"}:
            continue
        for text_command in text_commands:
            if (
                route.get("scenario_id") is not None
                and route.get("scenario_id") == text_command.get("scenario_id")
                and (
                    ".node." in str(text_command.get("id"))
                    or ".criterion.label" in str(text_command.get("id"))
                )
            ):
                continue
            bounds = _command_bounds(text_command)
            if bounds is None:
                continue
            expanded = [bounds[0] - 1, bounds[1] - 1, bounds[2] + 1, bounds[3] + 1]
            if any(
                _segment_hits_rect(start, end, expanded)
                for start, end in zip(route["points"], route["points"][1:])
            ):
                route_text_collisions.append([route["id"], text_command["id"]])
    hit_scenarios = {
        command.get("scenario_id")
        for command in commands
        if command.get("semantic_role") == "scenario_hit_target"
    }
    criterion_scenarios = {
        command.get("scenario_id")
        for command in commands
        if command.get("semantic_role") == "criterion_dock"
    }
    frontier_commands = [
        command
        for command in commands
        if command.get("semantic_role") == "frontier_break"
    ]
    core_omission_errors = [
        command["id"]
        for command in commands
        if command.get("scenario_id") in SCENARIO_ORDER[:3]
        and "omitted" in str(command.get("text", "")).casefold()
    ]
    selected = scene.get("selected_scenario_id")
    neutral_errors = []
    if scene.get("disclosure_state") == "NEUTRAL" and selected is not None:
        neutral_errors.append("selected_scenario_id")
    selected_detail_scenarios = {
        command.get("scenario_id")
        for command in commands
        if command.get("region") == "detail" and command.get("scenario_id") is not None
    }
    detail_owner_errors = []
    if selected is None and selected_detail_scenarios:
        detail_owner_errors.append("neutral_detail")
    if selected is not None and selected_detail_scenarios != {selected}:
        detail_owner_errors.append(str(selected))
    expected_visible = set(scene.get("recoverability", {}).get("overview_visible", [])) | set(
        scene.get("recoverability", {}).get("summary_visible", [])
    )
    fact_commands = [command for command in commands if command.get("fact_id")]
    painted_fact_ids = {command["fact_id"] for command in fact_commands}
    referenced_fact_ids = {
        command["fact_ref_id"]
        for command in commands
        if command.get("fact_ref_id") is not None
    }
    visible_fact_errors = sorted(expected_visible - (painted_fact_ids | referenced_fact_ids))
    overview_fact_commands = [
        command for command in fact_commands if command.get("region") in {"overview", "actions", "header"}
    ]
    unexpected_overview_facts = sorted(
        {
            command["fact_id"]
            for command in overview_fact_commands
            if command["fact_id"] not in scene["recoverability"]["overview_visible"]
        }
    )
    fallback_errors = (
        []
        if "action.show_complete_text" in painted_fact_ids
        else ["action.show_complete_text"]
    )
    checks = {
        "canvas": [scene.get("width"), scene.get("height")],
        "duplicate_command_ids": duplicate_ids,
        "malformed_command_ids": malformed_ids,
        "out_of_bounds_ids": out_of_bounds_ids,
        "text_measurement_errors": text_measurement_errors,
        "text_overlap_pairs": text_overlap_pairs,
        "route_text_collision_pairs": route_text_collisions,
        "missing_hit_scenarios": sorted(set(SCENARIO_ORDER) - hit_scenarios),
        "missing_criterion_scenarios": sorted(set(SCENARIO_ORDER) - criterion_scenarios),
        "frontier_errors": [] if len(frontier_commands) == 1 and frontier_commands[0].get("scenario_id") == "LC6_TRUNCATED" else ["query_frontier"],
        "core_omission_errors": core_omission_errors,
        "neutral_errors": neutral_errors,
        "detail_owner_errors": detail_owner_errors,
        "visible_fact_errors": visible_fact_errors,
        "unexpected_overview_facts": unexpected_overview_facts,
        "fallback_errors": fallback_errors,
        "overview_geometry_sha256": overview_geometry_sha256(scene),
        "command_count": len(commands),
    }
    checks["pass"] = (
        checks["canvas"] == list(CANVAS)
        and not any(
            value
            for key, value in checks.items()
            if key not in {"canvas", "overview_geometry_sha256", "command_count", "pass"}
        )
    )
    return checks


def svg_for_graph_first_scene(scene: Mapping[str, Any]) -> str:
    """Render one validated graph-first state as deterministic SVG."""

    checks = graph_first_scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid graph-first scene: {checks}")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{scene["width"]}" height="{scene["height"]}" viewBox="0 0 {scene["width"]} {scene["height"]}">',
        f'  <title>{html.escape(scene["condition_label"])} · {scene["disclosure_state"]}</title>',
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
            lines.append(f'  <polyline id="{command_id}" points="{points}" fill="none" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"{dash} stroke-linecap="round"/>')
        elif command["kind"] == "polygon":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            lines.append(f'  <polygon id="{command_id}" points="{points}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>')
        elif command["kind"] == "circle":
            lines.append(f'  <circle id="{command_id}" cx="{command["cx"]}" cy="{command["cy"]}" r="{command["radius"]}" fill="{command["fill"]}"/>')
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def png_for_graph_first_scene(scene: Mapping[str, Any], path: str | Path) -> None:
    """Render one validated graph-first state as deterministic PNG."""

    checks = graph_first_scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid graph-first scene: {checks}")
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
                draw.line(points, fill=command["stroke"], width=command["stroke_width"])
        elif command["kind"] == "polygon":
            draw.polygon([tuple(point) for point in command["points"]], fill=command["fill"], outline=command["stroke"], width=command["stroke_width"])
        elif command["kind"] == "circle":
            draw.ellipse([command["cx"] - command["radius"], command["cy"] - command["radius"], command["cx"] + command["radius"], command["cy"] + command["radius"]], fill=command["fill"])
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target, format="PNG", optimize=False, compress_level=9)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _board_svg(
    scenes: list[Mapping[str, Any]], title: str, columns: int
) -> str:
    rows = (len(scenes) + columns - 1) // columns
    width = columns * 720 + 20
    height = rows * 780 + 80
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        f"  <title>{html.escape(title)}</title>",
        f'  <rect width="{width}" height="{height}" fill="{TOKENS["background"]}"/>',
        f'  <text x="20" y="20" fill="{TOKENS["text"]}" font-family="Segoe UI" font-size="20" font-weight="700" dominant-baseline="text-before-edge">{html.escape(title)}</text>',
    ]
    for index, scene in enumerate(scenes):
        x = 20 + (index % columns) * 720
        y = 70 + (index // columns) * 780
        body = svg_for_graph_first_scene(scene).splitlines()[3:-1]
        prefix = f"board{index}__"
        lines.append(f'  <g id="board{index}" transform="translate({x} {y})">')
        lines.extend("  " + line.replace('id="', f'id="{prefix}') for line in body)
        lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _board_png(
    scenes: list[Mapping[str, Any]],
    effect_paths: Mapping[str, Path],
    title: str,
    columns: int,
    path: Path,
) -> None:
    rows = (len(scenes) + columns - 1) // columns
    width = columns * 720 + 20
    height = rows * 780 + 80
    image = Image.new("RGB", (width, height), TOKENS["background"])
    draw = ImageDraw.Draw(image)
    draw.text((20, 20), title, font=_font(20, "bold"), fill=TOKENS["text"], anchor="lt")
    for index, scene in enumerate(scenes):
        state_slug = scene["disclosure_state"].casefold().replace("_", "-")
        slug = GRAPH_CONDITIONS[scene["condition_id"]][1]
        name = f"{slug}-{state_slug}-700.png"
        with Image.open(effect_paths[name]) as effect:
            _require(effect.size == CANVAS, f"LC6 graph-first effect dimensions differ: {name}")
            image.paste(effect.convert("RGB"), (20 + (index % columns) * 720, 70 + (index // columns) * 780))
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _packet_hashes(directory: Path, names: list[str]) -> dict[str, Any]:
    entries = []
    for name in sorted(names):
        payload = (directory / name).read_bytes()
        entries.append({"path": name, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
    return {
        "format": "blueprint-lens-lc6-graph-first-self-excluding-hashes",
        "format_version": "1.0.0",
        "algorithm": "sha256",
        "excluded_path": "hashes.json",
        "entry_count": len(entries),
        "entries": entries,
    }


def _validate_packet(directory: Path) -> None:
    entries = sorted(directory.iterdir(), key=lambda path: path.name)
    _require(len(entries) == 19 and all(path.is_file() for path in entries), "LC6 graph-first packet must contain exactly 19 files")
    hashes = json.loads((directory / "hashes.json").read_text(encoding="utf-8"))
    _require(hashes.get("excluded_path") == "hashes.json" and hashes.get("entry_count") == 18, "LC6 graph-first hash inventory is not self-excluding 18/19")
    expected_names = {path.name for path in entries} - {"hashes.json"}
    _require({item.get("path") for item in hashes.get("entries", [])} == expected_names, "LC6 graph-first hash coverage differs")
    for item in hashes["entries"]:
        payload = (directory / item["path"]).read_bytes()
        _require(item.get("bytes") == len(payload) and item.get("sha256") == hashlib.sha256(payload).hexdigest(), f"LC6 graph-first hash differs: {item['path']}")


def _directories_identical(first: Path, second: Path) -> bool:
    first_entries = sorted((path.name, path.is_file()) for path in first.iterdir())
    second_entries = sorted((path.name, path.is_file()) for path in second.iterdir())
    if first_entries != second_entries or not all(is_file for _, is_file in first_entries):
        return False
    return all((first / name).read_bytes() == (second / name).read_bytes() for name, _ in first_entries)


def build_lc6_graph_first_artifacts(
    evidence_dir: str | Path, output_dir: str | Path
) -> dict[str, Path]:
    """Atomically publish the exact deterministic 19-file graph-first packet."""

    destination = Path(output_dir).resolve()
    _require(not destination.exists() or destination.is_dir(), "LC6 graph-first destination is not a directory")
    for font_path in FONTS.values():
        _require(Path(font_path).is_file(), f"LC6 graph-first font is unavailable: {font_path}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{destination.name}.staging-", dir=destination.parent))
    try:
        ledger = load_lc6_visual_ledger(evidence_dir)
        manifest = build_graph_first_manifest(ledger)
        scenes = [
            build_graph_first_scene(ledger, condition_id, state_id)
            for condition_id in GRAPH_CONDITION_IDS
            for state_id in DISCLOSURE_STATES
        ]
        checks = {
            f"{scene['condition_id']}__{scene['disclosure_state']}": graph_first_scene_checks(scene)
            for scene in scenes
        }
        _require(all(item["pass"] for item in checks.values()), "LC6 graph-first geometry oracle failed")
        for condition_id in GRAPH_CONDITION_IDS:
            condition_scenes = [scene for scene in scenes if scene["condition_id"] == condition_id]
            _require(len({overview_geometry_sha256(scene) for scene in condition_scenes}) == 1, "LC6 graph-first overview geometry differs across states")
        files: dict[str, Path] = {}
        manifest_name = "lc6-graph-first-manifest.json"
        oracle_name = "lc6-graph-first-oracle.json"
        (staging / manifest_name).write_bytes(_json_bytes(manifest))
        oracle = {
            "format": "blueprint-lens-lc6-graph-first-oracle",
            "format_version": "1.0.0",
            "status": "PASS",
            "canvas": list(CANVAS),
            "checks": checks,
            "overview_geometry": {
                condition_id: overview_geometry_sha256(next(scene for scene in scenes if scene["condition_id"] == condition_id))
                for condition_id in GRAPH_CONDITION_IDS
            },
        }
        (staging / oracle_name).write_bytes(_json_bytes(oracle))
        files[manifest_name] = staging / manifest_name
        files[oracle_name] = staging / oracle_name
        for scene in scenes:
            slug = GRAPH_CONDITIONS[scene["condition_id"]][1]
            state_slug = scene["disclosure_state"].casefold().replace("_", "-")
            svg_name = f"{slug}-{state_slug}-700.svg"
            png_name = f"{slug}-{state_slug}-700.png"
            (staging / svg_name).write_text(svg_for_graph_first_scene(scene), encoding="utf-8", newline="\n")
            png_for_graph_first_scene(scene, staging / png_name)
            files[svg_name] = staging / svg_name
            files[png_name] = staging / png_name
        neutral_scenes = [scene for scene in scenes if scene["disclosure_state"] == "NEUTRAL"]
        selected_scenes = [scene for scene in scenes if scene["disclosure_state"] != "NEUTRAL"]
        boards = (
            ("lc6-graph-first-neutral-comparison-board", neutral_scenes, "LC6 graph-first · neutral overview comparison", 2),
            ("lc6-graph-first-selected-comparison-board", selected_scenes, "LC6 graph-first · selected detail comparison", 2),
        )
        for slug, board_scenes, title, columns in boards:
            svg_name, png_name = f"{slug}.svg", f"{slug}.png"
            (staging / svg_name).write_text(_board_svg(board_scenes, title, columns), encoding="utf-8", newline="\n")
            _board_png(board_scenes, files, title, columns, staging / png_name)
            files[svg_name] = staging / svg_name
            files[png_name] = staging / png_name
        _require(len(files) == 18, "LC6 graph-first pre-hash file count differs")
        (staging / "hashes.json").write_bytes(_json_bytes(_packet_hashes(staging, list(files))))
        files["hashes.json"] = staging / "hashes.json"
        _validate_packet(staging)
        if destination.exists():
            _require(_directories_identical(staging, destination), "LC6 graph-first destination exists with different bytes")
        else:
            os.replace(staging, destination)
        return {name: destination / name for name in sorted(files)}
    finally:
        if staging.exists():
            shutil.rmtree(staging)
