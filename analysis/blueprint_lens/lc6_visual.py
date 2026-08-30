"""Deterministic authoring views over the frozen LC6 boundary truth."""

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

from PIL import Image, ImageDraw, ImageFont

from blueprint_lens.boundaries.lc6_artifacts import (
    AUDIT_NAME,
    BUDGET_NAME,
    CORE_NAME,
    RAW_NAME,
    REVIEWED_NAME,
    SOURCE_NAME,
    canonical_json_bytes,
    load_lc6_evidence,
    require_reviewed_truth,
)


class LC6VisualError(ValueError):
    """Raised when a visual projection differs from frozen LC6 truth."""


CONDITIONS = MappingProxyType(
    {
        "LC6_COMPLETE_TEXT": ("Complete Text", "permanent_control", "lc6-complete-text-effect-700"),
        "LC6_BOUNDARY_LEDGER": ("Boundary Ledger", "project_audit_grammar", "lc6-boundary-ledger-effect-700"),
        "LC6_SPLIT_FRONTIER_ROUTES": ("Split Frontier Routes", "project_relation_grammar", "lc6-split-frontier-routes-effect-700"),
        "LC6_KNOWN_UNKNOWN_THRESHOLD": ("Known / Unknown Threshold", "precedent:DPA-06+VDFG40", "lc6-known-unknown-threshold-effect-700"),
    }
)
CONDITION_IDS = tuple(CONDITIONS)
SCENARIO_ORDER = (
    "LC6_OPAQUE",
    "LC6_UNCERTAIN",
    "LC6_UNSUPPORTED",
    "LC6_TRUNCATED",
)
RECOMMENDED_CONDITION_ID = "LC6_SPLIT_FRONTIER_ROUTES"
_CORE_EXPECTED = MappingProxyType(
    {
        "LC6_OPAQUE": ("opaque", "function_body_not_expanded", 2, 1, 2),
        "LC6_UNCERTAIN": ("uncertain", "node_family_not_in_supported_matrix_v1", 3, 2, 1),
        "LC6_UNSUPPORTED": ("unsupported", "latent_function", 2, 1, 2),
    }
)
_ACTIONS = (
    {"action_id": "open_source", "label": "Open source"},
    {"action_id": "show_complete_text", "label": "Show complete text"},
    {"action_id": "show_evidence", "label": "Show evidence"},
)
_NON_CLAIMS = (
    "authoring target only; not Slate or UE-visible evidence",
    "no human comprehension, preference or scalability evidence",
    "recommended condition is neither selected nor a product default",
)
_QUESTION = (
    "Why does analysis stop here, who owns the stop, and what is known or omitted?"
)
CANVAS = (700, 1260)
FONTS = MappingProxyType(
    {
        "regular": "C:/Windows/Fonts/segoeui.ttf",
        "bold": "C:/Windows/Fonts/segoeuib.ttf",
    }
)
TOKENS = MappingProxyType(
    {
        "background": "#0E1117",
        "surface": "#171C24",
        "surface_alt": "#1D2430",
        "core_fill": "#142535",
        "query_fill": "#241D31",
        "text": "#F2F5F8",
        "muted": "#A9B3C1",
        "line": "#405066",
        "core": "#67B7FF",
        "query": "#D997FF",
        "opaque": "#F0B35A",
        "uncertain": "#E9D66B",
        "unsupported": "#F07178",
        "truncated": "#D997FF",
        "criterion": "#A7D46F",
        "radius": 10,
    }
)
REGION_BOUNDS = MappingProxyType(
    {
        "header": (24, 24, 652, 106),
        "core_owner": (24, 148, 652, 610),
        "query_owner": (24, 778, 652, 334),
        "actions": (24, 1130, 652, 62),
    }
)
_TEXT_MEASURE_IMAGE = Image.new("L", (1, 1))
_TEXT_MEASURE_DRAW = ImageDraw.Draw(_TEXT_MEASURE_IMAGE)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC6VisualError(f"cannot read LC6 visual input: {path}") from error
    if not isinstance(value, dict):
        raise LC6VisualError(f"LC6 visual input is not an object: {path}")
    return value


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC6VisualError(message)


def _title(node_by_id: Mapping[str, Any], node_id: str) -> str:
    node = node_by_id.get(node_id)
    _require(node is not None and isinstance(node.title, str) and bool(node.title), f"source title missing: {node_id}")
    return node.title


def _node_records(node_by_id: Mapping[str, Any], node_ids: list[str], distances: Mapping[str, int] | None = None) -> list[dict[str, Any]]:
    records = [
        {
            "node_id": node_id,
            "title": _title(node_by_id, node_id),
            "hop_distance": distances.get(node_id) if distances else None,
        }
        for node_id in node_ids
    ]
    if distances:
        records.sort(key=lambda item: (-item["hop_distance"], item["node_id"]))
    else:
        records.sort(key=lambda item: item["node_id"])
    return records


def _edge_records(edge_by_id: Mapping[str, Any], edge_ids: list[str]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for edge_id in sorted(edge_ids):
        edge = edge_by_id.get(edge_id)
        _require(edge is not None, f"source edge missing: {edge_id}")
        records.append(
            {
                "edge_id": edge.id,
                "kind": edge.kind,
                "source_node_id": edge.source_node_id,
                "target_node_id": edge.target_node_id,
            }
        )
    return records


def _profile_hash_binding(evidence: Path, readiness: Mapping[str, Any], name: str, rebuilt: Mapping[str, Any]) -> str:
    path = evidence / name
    expected = readiness.get("hashes", {}).get(name)
    _require(isinstance(expected, str) and _sha256(path) == expected, f"readiness hash differs: {name}")
    _require(path.read_bytes() == canonical_json_bytes(rebuilt), f"retained profile differs from rebuild: {name}")
    return expected


def _semantic_sha256(ledger: Mapping[str, Any]) -> str:
    payload = {
        key: deepcopy(ledger[key])
        for key in (
            "question",
            "asset_path",
            "graph_id",
            "profile_binding",
            "scenarios",
            "actions",
            "non_claims",
        )
    }
    return hashlib.sha256(canonical_json_bytes(payload)).hexdigest()


def load_lc6_visual_ledger(evidence_dir: str | Path) -> dict[str, Any]:
    """Reopen LC6 evidence and project one owner-accountable visual ledger."""

    evidence = Path(evidence_dir).resolve()
    bundle = load_lc6_evidence(evidence)
    readiness = _read_json(evidence / "readiness.json")
    reviewed = _read_json(evidence / REVIEWED_NAME)
    require_reviewed_truth(bundle, reviewed)
    _require(
        readiness.get("status") == "TRUTH_FROZEN"
        and readiness.get("scope") == "LC6-F1"
        and readiness.get("checks_passed") == readiness.get("checks_total") == 16,
        "LC6 readiness is not frozen 16/16",
    )
    _require(
        readiness.get("profile_ids") == ["LC6_CORE_BOUNDARY_MATRIX_V1", "LC6_MAX_UPSTREAM_HOPS_V1"],
        "LC6 readiness profile IDs differ",
    )
    node_by_id = {node.id: node for node in bundle.document.nodes}
    edge_by_id = {edge.id: edge for edge in bundle.document.edges}
    core = bundle.profiles.core_boundary_matrix
    budget = bundle.profiles.upstream_budget
    core_hash = _profile_hash_binding(evidence, readiness, CORE_NAME, core)
    budget_hash = _profile_hash_binding(evidence, readiness, BUDGET_NAME, budget)
    for name in (RAW_NAME, SOURCE_NAME, AUDIT_NAME, REVIEWED_NAME):
        expected = readiness.get("hashes", {}).get(name)
        actual_path = evidence / REVIEWED_NAME if name == REVIEWED_NAME else evidence / "run1" / name
        _require(isinstance(expected, str) and _sha256(actual_path) == expected, f"readiness hash differs: {name}")

    core_by_id = {item["scenario_id"]: item for item in core["scenarios"]}
    _require(set(core_by_id) == set(_CORE_EXPECTED), "LC6 core scenario set differs")
    scenarios: list[dict[str, Any]] = []
    for scenario_id in SCENARIO_ORDER[:3]:
        source = core_by_id[scenario_id]
        status, reason, node_count, edge_count, incident_count = _CORE_EXPECTED[scenario_id]
        scenario = {
            "scenario_id": scenario_id,
            "truth_owner": "core_node_classification",
            "status": source["status"],
            "reason": source["reason"],
            "stop_kind": source["stop_location"]["kind"],
            "root_node_id": source["root_node_id"],
            "root_title": _title(node_by_id, source["root_node_id"]),
            "boundary_node_id": source["boundary_node_id"],
            "boundary_title": _title(node_by_id, source["boundary_node_id"]),
            "criterion_node_id": source["criterion_node_id"],
            "criterion_title": _title(node_by_id, source["criterion_node_id"]),
            "nodes": _node_records(node_by_id, source["slice_node_ids"]),
            "edges": _edge_records(edge_by_id, source["slice_edge_ids"]),
            "incident_edge_ids": sorted(source["incident_edge_ids"]),
            "counts": {
                "selected_nodes": len(source["slice_node_ids"]),
                "selected_edges": len(source["slice_edge_ids"]),
                "incident_edges": len(source["incident_edge_ids"]),
            },
        }
        _require(
            (scenario["status"], scenario["reason"], len(scenario["nodes"]), len(scenario["edges"]), len(scenario["incident_edge_ids"]))
            == (status, reason, node_count, edge_count, incident_count),
            f"LC6 core truth differs: {scenario_id}",
        )
        scenarios.append(scenario)

    distances = {item["node_id"]: item["distance"] for item in budget["hop_distances"]}
    scenarios.append(
        {
            "scenario_id": "LC6_TRUNCATED",
            "truth_owner": "query_profile",
            "status": budget["status"],
            "reason": budget["reason"],
            "stop_kind": "query_budget_frontier",
            "root_node_id": budget["root_node_id"],
            "root_title": _title(node_by_id, budget["root_node_id"]),
            "boundary_node_id": None,
            "boundary_title": None,
            "criterion_node_id": budget["criterion_node_id"],
            "criterion_title": _title(node_by_id, budget["criterion_node_id"]),
            "max_upstream_hops": budget["max_upstream_hops"],
            "nodes": _node_records(node_by_id, budget["selected_node_ids"], distances),
            "edges": _edge_records(edge_by_id, budget["selected_edge_ids"]),
            "complete_nodes": _node_records(node_by_id, budget["complete_node_ids"], distances),
            "complete_edges": _edge_records(edge_by_id, budget["complete_edge_ids"]),
            "frontiers": deepcopy(budget["frontiers"]),
            "counts": {
                "selected_nodes": budget["counts"]["selected_nodes"],
                "selected_edges": budget["counts"]["selected_edges"],
                "complete_nodes": budget["counts"]["complete_nodes"],
                "complete_edges": budget["counts"]["complete_edges"],
                "omitted_nodes": budget["counts"]["omitted_nodes"],
                "omitted_edges": budget["counts"]["omitted_edges"],
                "frontiers": budget["counts"]["frontiers"],
            },
        }
    )
    ledger = {
        "format": "blueprint-lens-lc6-visual-ledger",
        "format_version": "1.0.0",
        "question": _QUESTION,
        "asset_path": core["blueprint_asset_path"],
        "graph_id": core["graph_id"],
        "profile_binding": {
            "core_profile_id": core["profile_id"],
            "budget_profile_id": budget["profile_id"],
            "schema_gate_commit": readiness["schema_gate_commit"],
            "core_sha256": core_hash,
            "budget_sha256": budget_hash,
            "readiness_sha256": _sha256(evidence / "readiness.json"),
            "native_hashes": {name: readiness["hashes"][name] for name in (RAW_NAME, SOURCE_NAME, AUDIT_NAME)},
        },
        "scenarios": scenarios,
        "actions": deepcopy(list(_ACTIONS)),
        "non_claims": list(_NON_CLAIMS),
    }
    ledger["semantic_sha256"] = _semantic_sha256(ledger)
    validate_lc6_visual_ledger(ledger)
    return ledger


def information_set(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Return the exact semantic payload every visual condition must expose."""

    return {
        "question": ledger["question"],
        "profile_binding": deepcopy(dict(ledger["profile_binding"])),
        "scenarios": deepcopy(list(ledger["scenarios"])),
        "actions": deepcopy(list(ledger["actions"])),
        "non_claims": deepcopy(list(ledger["non_claims"])),
    }


def validate_lc6_visual_ledger(ledger: Mapping[str, Any]) -> None:
    """Reject ownership, status, disclosure or count drift in a projected ledger."""

    _require(ledger.get("format") == "blueprint-lens-lc6-visual-ledger", "LC6 ledger format differs")
    scenarios = ledger.get("scenarios")
    _require(isinstance(scenarios, list) and [item.get("scenario_id") for item in scenarios] == list(SCENARIO_ORDER), "LC6 ledger scenario order differs")
    for item in scenarios[:3]:
        status, reason, nodes, edges, incident = _CORE_EXPECTED[item["scenario_id"]]
        _require(item.get("truth_owner") == "core_node_classification", "LC6 core owner differs")
        _require(item.get("status") == status and item.get("reason") == reason, "LC6 core status/reason differs")
        _require(item.get("stop_kind") == "semantic_boundary", "LC6 core stop kind differs")
        _require(item.get("counts") == {"selected_nodes": nodes, "selected_edges": edges, "incident_edges": incident}, "LC6 core counts differ")
        selected_node_ids = {node.get("node_id") for node in item.get("nodes", [])}
        _require(
            item.get("criterion_node_id") in selected_node_ids
            and item.get("boundary_node_id") in selected_node_ids,
            "LC6 core criterion or boundary escapes selected nodes",
        )
        _require(
            item.get("root_node_id") != item.get("criterion_node_id"),
            "LC6 core root and criterion identities overlap",
        )
    query = scenarios[3]
    _require(query.get("truth_owner") == "query_profile", "LC6 query owner differs")
    _require(query.get("status") == "truncated" and query.get("reason") == "max_upstream_hops_exhausted", "LC6 query status/reason differs")
    _require(query.get("boundary_node_id") is None and query.get("max_upstream_hops") == 3, "LC6 query stop was promoted to a node")
    _require(query.get("counts") == {"selected_nodes": 4, "selected_edges": 3, "complete_nodes": 7, "complete_edges": 6, "omitted_nodes": 3, "omitted_edges": 3, "frontiers": 1}, "LC6 query counts differ")
    _require(len(query.get("frontiers", [])) == 1, "LC6 query Frontier differs")
    selected_node_ids = {node.get("node_id") for node in query.get("nodes", [])}
    complete_node_ids = {node.get("node_id") for node in query.get("complete_nodes", [])}
    _require(
        query.get("criterion_node_id") in selected_node_ids
        and query.get("root_node_id") in complete_node_ids
        and query.get("root_node_id") not in selected_node_ids,
        "LC6 query root/criterion membership differs",
    )
    frontier = query["frontiers"][0]
    _require(
        frontier.get("source_node_id") in complete_node_ids - selected_node_ids
        and frontier.get("target_node_id") in selected_node_ids,
        "LC6 query Frontier ownership differs",
    )
    _require(ledger.get("actions") == list(_ACTIONS), "LC6 ledger actions differ")
    _require(ledger.get("non_claims") == list(_NON_CLAIMS), "LC6 ledger non-claims differ")
    _require(all(node.get("title") for item in scenarios for node in item.get("nodes", [])), "LC6 source label missing")
    _require(
        ledger.get("semantic_sha256") == _semantic_sha256(ledger),
        "LC6 ledger semantic payload differs from its seal",
    )


def _manifest_conditions() -> list[dict[str, str]]:
    return [
        {
            "condition_id": condition_id,
            "label": label,
            "provenance_class": provenance,
        }
        for condition_id, (label, provenance, _) in CONDITIONS.items()
    ]


def _expected_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    validate_lc6_visual_ledger(ledger)
    matched = information_set(ledger)
    states = []
    for condition_id, (_, _, slug) in CONDITIONS.items():
        states.append(
            {
                "state_id": f"{condition_id}__W700",
                "condition_id": condition_id,
                "width": 700,
                "information_set": deepcopy(matched),
                "evidence_state": "authoring_design_target",
                "effect_paths": {"svg": f"{slug}.svg", "png": f"{slug}.png"},
            }
        )
    return {
        "format": "blueprint-lens-lc6-visual-manifest",
        "format_version": "1.0.0",
        "status": "INFORMATION_MATCHED__FOUR_700PX_AUTHORING_TARGETS",
        "profile_binding": deepcopy(dict(ledger["profile_binding"])),
        "recommended_condition_id": RECOMMENDED_CONDITION_ID,
        "selected_condition_id": None,
        "default_condition_id": None,
        "conditions": _manifest_conditions(),
        "target_widths_logical_px": [700],
        "responsive_states_deferred": [430, 480],
        "slate_implementation_deferred": True,
        "states": states,
        "non_claims": deepcopy(list(_NON_CLAIMS)),
    }


def build_lc6_visual_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Build the exact bounded four-condition authoring contract."""

    manifest = _expected_manifest(ledger)
    validate_lc6_visual_manifest(manifest, ledger)
    return manifest


def validate_lc6_visual_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    """Reject every manifest deviation from frozen truth and authoring scope."""

    _require(
        manifest == _expected_manifest(ledger),
        "LC6 visual manifest differs from the frozen four-condition contract",
    )


def _short_id(value: str | None) -> str:
    if value is None:
        return "none"
    tail = value.rsplit("::", 1)[-1]
    return tail.rsplit("-", 4)[0][:8] if "-" in tail else tail[:8]


def _display_facts(information: Mapping[str, Any]) -> dict[str, str]:
    binding = information["profile_binding"]
    facts = {
        "shared.question": str(information["question"]),
        "shared.profiles": (
            f"Profiles · {binding['core_profile_id']} #{binding['core_sha256'][:10]}"
            f" · {binding['budget_profile_id']} #{binding['budget_sha256'][:10]}"
        ),
        "shared.readiness": (
            f"Readiness #{binding['readiness_sha256'][:10]} · schema gate "
            f"{binding['schema_gate_commit'][:10]}"
        ),
        "shared.native": "Native evidence · "
        + " · ".join(
            f"{label} #{digest[:8]}"
            for label, digest in zip(
                ("raw", "source", "audit"),
                binding["native_hashes"].values(),
            )
        ),
    }
    for scenario in information["scenarios"]:
        scenario_id = scenario["scenario_id"]
        prefix = f"scenario.{scenario_id}"
        node_titles = {
            item["node_id"]: item["title"]
            for item in scenario.get("complete_nodes", scenario["nodes"])
        }
        member_text = "; ".join(
            f"{item['title']} #{_short_id(item['node_id'])}"
            for item in scenario["nodes"]
        )
        relation_text = "; ".join(
            f"{node_titles[item['source_node_id']]} → "
            f"{node_titles[item['target_node_id']]}"
            for item in scenario["edges"]
        )
        identity = (
            f"Root {scenario['root_title']} #{_short_id(scenario['root_node_id'])}"
        )
        if scenario["boundary_node_id"] is not None:
            identity += (
                f" · Boundary {scenario['boundary_title']} "
                f"#{_short_id(scenario['boundary_node_id'])}"
            )
        identity += (
            f" · Criterion {scenario['criterion_title']} "
            f"#{_short_id(scenario['criterion_node_id'])}"
        )
        facts.update(
            {
                f"{prefix}.status": f"{scenario_id} · {scenario['status'].upper()}",
                f"{prefix}.owner": f"Owner · {scenario['truth_owner']}",
                f"{prefix}.reason": f"Why · {scenario['reason']}",
                f"{prefix}.stop": f"Stop · {scenario['stop_kind']}",
                f"{prefix}.identity": identity,
                f"{prefix}.members": f"Selected members · {member_text}",
                f"{prefix}.relations": f"Selected relations · {relation_text}",
            }
        )
        counts = scenario["counts"]
        if scenario_id != "LC6_TRUNCATED":
            facts[f"{prefix}.counts"] = (
                f"Counts · selected {counts['selected_nodes']} nodes / "
                f"{counts['selected_edges']} edges · incident "
                f"{counts['incident_edges']} edges"
            )
            continue
        complete_members = " → ".join(
            (
                "root"
                if index == 0
                else "criterion"
                if index == len(scenario["complete_nodes"]) - 1
                else item["title"].replace("Set LC6Truncated", "")
            )
            + f"#{_short_id(item['node_id'])[:4]}"
            for index, item in enumerate(scenario["complete_nodes"])
        )
        frontier = scenario["frontiers"][0]
        facts.update(
            {
                f"{prefix}.budget": (
                    f"Budget · max upstream hops {scenario['max_upstream_hops']}"
                ),
                f"{prefix}.counts": (
                    f"Counts · selected {counts['selected_nodes']}/{counts['selected_edges']}"
                    f" · complete {counts['complete_nodes']}/{counts['complete_edges']}"
                    f" · omitted {counts['omitted_nodes']}/{counts['omitted_edges']}"
                    f" · Frontiers {counts['frontiers']}"
                ),
                f"{prefix}.complete": f"Complete route · {complete_members}",
                f"{prefix}.frontier": (
                    "Crossing Frontier · "
                    f"{node_titles[frontier['source_node_id']]} "
                    f"#{_short_id(frontier['source_node_id'])} → "
                    f"{node_titles[frontier['target_node_id']]} "
                    f"#{_short_id(frontier['target_node_id'])}"
                ),
            }
        )
    for action in information["actions"]:
        facts[f"action.{action['action_id']}"] = action["label"]
    for index, claim in enumerate(information["non_claims"]):
        facts[f"nonclaim.{index}"] = claim
    return facts


def _font(size: int, weight: str) -> ImageFont.FreeTypeFont:
    path = Path(FONTS[weight])
    _require(path.is_file(), f"LC6 visual font is unavailable: {path}")
    return ImageFont.truetype(str(path), size=size)


def _text_bounds(
    x: int, y: int, text: str, size: int, weight: str
) -> list[int]:
    return list(
        _TEXT_MEASURE_DRAW.textbbox(
            (x, y), text, font=_font(size, weight), anchor="lt"
        )
    )


def _add_rect(
    commands: list[dict[str, Any]],
    command_id: str,
    x: int,
    y: int,
    width: int,
    height: int,
    *,
    fill: str,
    stroke: str | None = None,
    stroke_width: int = 1,
    radius: int = 0,
    container_id: str | None = None,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
) -> None:
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
            "container_id": container_id,
            "semantic_role": semantic_role,
            "scenario_id": scenario_id,
        }
    )


def _add_text(
    commands: list[dict[str, Any]],
    command_id: str,
    x: int,
    y: int,
    text: str,
    *,
    size: int = 10,
    weight: str = "regular",
    fill: str | None = None,
    container_id: str | None = None,
    fact_id: str | None = None,
    fact_value: str | None = None,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
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
            "container_id": container_id,
            "fact_id": fact_id,
            "fact_value": fact_value,
            "semantic_role": semantic_role,
            "scenario_id": scenario_id,
        }
    )


def _add_fact(
    commands: list[dict[str, Any]],
    facts: Mapping[str, str],
    fact_id: str,
    x: int,
    y: int,
    *,
    size: int = 9,
    weight: str = "regular",
    fill: str | None = None,
    container_id: str,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
) -> None:
    _add_text(
        commands,
        f"fact.{fact_id}",
        x,
        y,
        facts[fact_id],
        size=size,
        weight=weight,
        fill=fill,
        container_id=container_id,
        fact_id=fact_id,
        fact_value=facts[fact_id],
        semantic_role=semantic_role,
        scenario_id=scenario_id,
    )


def _add_polyline(
    commands: list[dict[str, Any]],
    command_id: str,
    points: list[list[int]],
    *,
    stroke: str,
    stroke_width: int = 2,
    dash: list[int] | None = None,
    start_anchor_id: str | None = None,
    end_anchor_id: str | None = None,
    container_id: str | None = None,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "polyline",
            "id": command_id,
            "points": points,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "dash": dash,
            "start_anchor_id": start_anchor_id,
            "end_anchor_id": end_anchor_id,
            "container_id": container_id,
            "semantic_role": semantic_role,
            "scenario_id": scenario_id,
        }
    )


def _add_polygon(
    commands: list[dict[str, Any]],
    command_id: str,
    points: list[list[int]],
    *,
    fill: str,
    stroke: str | None = None,
    stroke_width: int = 1,
    container_id: str | None = None,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "polygon",
            "id": command_id,
            "points": points,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "container_id": container_id,
            "semantic_role": semantic_role,
            "scenario_id": scenario_id,
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
    stroke: str | None = None,
    stroke_width: int = 1,
    container_id: str | None = None,
    semantic_role: str | None = None,
    scenario_id: str | None = None,
) -> None:
    commands.append(
        {
            "kind": "circle",
            "id": command_id,
            "cx": cx,
            "cy": cy,
            "radius": radius,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "container_id": container_id,
            "semantic_role": semantic_role,
            "scenario_id": scenario_id,
        }
    )


def _base_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, str]]:
    information = information_set(ledger)
    facts = _display_facts(information)
    commands: list[dict[str, Any]] = []
    _add_rect(
        commands,
        "canvas.background",
        0,
        0,
        *CANVAS,
        fill=TOKENS["background"],
    )
    region_styles = {
        "header": TOKENS["surface"],
        "core_owner": TOKENS["core_fill"],
        "query_owner": TOKENS["query_fill"],
        "actions": TOKENS["surface"],
    }
    for region_id, bounds in REGION_BOUNDS.items():
        _add_rect(
            commands,
            f"region.{region_id}",
            *bounds,
            fill=region_styles[region_id],
            stroke=TOKENS["line"],
            radius=int(TOKENS["radius"]),
        )
    _add_rect(
        commands,
        "region.footer",
        24,
        1202,
        652,
        48,
        fill=TOKENS["background"],
    )
    label = CONDITIONS[condition_id][0]
    _add_text(
        commands,
        "header.condition",
        42,
        37,
        label,
        size=18,
        weight="bold",
        container_id="region.header",
    )
    _add_text(
        commands,
        "header.scope",
        498,
        40,
        "700 PX · AUTHORING",
        size=9,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.header",
    )
    _add_fact(
        commands,
        facts,
        "shared.question",
        42,
        66,
        size=10,
        weight="bold",
        container_id="region.header",
    )
    _add_fact(
        commands,
        facts,
        "shared.profiles",
        42,
        87,
        size=8,
        fill=TOKENS["muted"],
        container_id="region.header",
    )
    _add_fact(
        commands,
        facts,
        "shared.readiness",
        42,
        104,
        size=8,
        fill=TOKENS["muted"],
        container_id="region.header",
    )
    _add_fact(
        commands,
        facts,
        "shared.native",
        360,
        104,
        size=8,
        fill=TOKENS["muted"],
        container_id="region.header",
    )
    _add_text(
        commands,
        "core.heading",
        42,
        165,
        "CORE-OWNED SEMANTIC BOUNDARIES · 3",
        size=11,
        weight="bold",
        fill=TOKENS["core"],
        container_id="region.core_owner",
    )
    _add_text(
        commands,
        "query.heading",
        42,
        795,
        "QUERY-OWNED BUDGET FRONTIER · 1",
        size=11,
        weight="bold",
        fill=TOKENS["query"],
        container_id="region.query_owner",
    )
    action_x = [42, 250, 458]
    for index, (action, x) in enumerate(zip(information["actions"], action_x)):
        _add_rect(
            commands,
            f"action.{action['action_id']}.button",
            x,
            1142,
            176,
            34,
            fill=TOKENS["surface_alt"],
            stroke=TOKENS["line"],
            radius=8,
            container_id="region.actions",
        )
        _add_fact(
            commands,
            facts,
            f"action.{action['action_id']}",
            x + 16,
            1151,
            size=10,
            weight="bold",
            container_id="region.actions",
        )
    for index in range(3):
        _add_fact(
            commands,
            facts,
            f"nonclaim.{index}",
            42,
            1204 + index * 15,
            size=8,
            fill=TOKENS["muted"],
            container_id="region.footer",
        )
    regions = {
        "header": {"bounds": list(REGION_BOUNDS["header"])},
        "core_owner": {
            "bounds": list(REGION_BOUNDS["core_owner"]),
            "scenario_ids": list(SCENARIO_ORDER[:3]),
        },
        "query_owner": {
            "bounds": list(REGION_BOUNDS["query_owner"]),
            "scenario_ids": [SCENARIO_ORDER[3]],
        },
        "actions": {"bounds": list(REGION_BOUNDS["actions"])},
        "footer": {"bounds": [24, 1202, 652, 48]},
    }
    scene = {
        "format": "blueprint-lens-lc6-scene",
        "format_version": "1.0.0",
        "condition_id": condition_id,
        "condition_label": label,
        "width": CANVAS[0],
        "height": CANVAS[1],
        "regions": regions,
        "semantic_coverage": information,
        "semantic_sha256": hashlib.sha256(
            canonical_json_bytes(information)
        ).hexdigest(),
        "display_facts": facts,
        "commands": commands,
    }
    return scene, commands, facts


def _scenario_fact_ids(scenario_id: str) -> list[str]:
    base = f"scenario.{scenario_id}"
    common = [
        f"{base}.status",
        f"{base}.owner",
        f"{base}.reason",
        f"{base}.stop",
        f"{base}.identity",
        f"{base}.members",
        f"{base}.relations",
        f"{base}.counts",
    ]
    if scenario_id == "LC6_TRUNCATED":
        common.extend(
            [f"{base}.budget", f"{base}.complete", f"{base}.frontier"]
        )
    return common


def _scenario_colour(scenario: Mapping[str, Any]) -> str:
    return str(TOKENS[scenario["status"]])


def _add_scenario_fact_block(
    commands: list[dict[str, Any]],
    facts: Mapping[str, str],
    scenario: Mapping[str, Any],
    *,
    x: int,
    y: int,
    container_id: str,
    line_height: int = 17,
    size: int = 8,
) -> None:
    scenario_id = scenario["scenario_id"]
    for index, fact_id in enumerate(_scenario_fact_ids(scenario_id)):
        role = "criterion" if fact_id.endswith(".identity") else None
        _add_fact(
            commands,
            facts,
            fact_id,
            x,
            y + index * line_height,
            size=size,
            weight="bold" if fact_id.endswith(".status") else "regular",
            fill=(
                _scenario_colour(scenario)
                if fact_id.endswith(".status")
                else TOKENS["text"]
            ),
            container_id=container_id,
            semantic_role=role,
            scenario_id=scenario_id,
        )


def _complete_text_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> dict[str, Any]:
    scene, commands, facts = _base_scene(ledger, condition_id)
    scenarios = ledger["scenarios"]
    for index, scenario in enumerate(scenarios[:3]):
        y = 190 + index * 181
        _add_rect(
            commands,
            f"complete.{scenario['scenario_id']}.record",
            42,
            y,
            616,
            166,
            fill=TOKENS["surface"],
            stroke=TOKENS["line"],
            radius=8,
            container_id="region.core_owner",
            scenario_id=scenario["scenario_id"],
        )
        _add_scenario_fact_block(
            commands,
            facts,
            scenario,
            x=55,
            y=y + 12,
            container_id="region.core_owner",
            line_height=18,
            size=8,
        )
    query = scenarios[3]
    _add_rect(
        commands,
        "complete.LC6_TRUNCATED.record",
        42,
        820,
        616,
        272,
        fill=TOKENS["surface"],
        stroke=TOKENS["line"],
        radius=8,
        container_id="region.query_owner",
        scenario_id="LC6_TRUNCATED",
    )
    _add_scenario_fact_block(
        commands,
        facts,
        query,
        x=55,
        y=833,
        container_id="region.query_owner",
        line_height=21,
        size=8,
    )
    return scene


def _boundary_ledger_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> dict[str, Any]:
    scene, commands, facts = _base_scene(ledger, condition_id)
    _add_text(
        commands,
        "ledger.columns",
        42,
        190,
        "CASE / OWNER                STATUS / WHY                   KNOWN ROUTE / BOUNDARY",
        size=8,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.core_owner",
    )
    for index, scenario in enumerate(ledger["scenarios"][:3]):
        y = 216 + index * 166
        _add_polyline(
            commands,
            f"ledger.{scenario['scenario_id']}.divider",
            [[42, y - 8], [658, y - 8]],
            stroke=TOKENS["line"],
            stroke_width=1,
            container_id="region.core_owner",
            scenario_id=scenario["scenario_id"],
        )
        base = f"scenario.{scenario['scenario_id']}"
        placements = [
            ("status", 42, 0, 9, "bold"),
            ("owner", 235, 0, 8, "regular"),
            ("reason", 42, 25, 8, "regular"),
            ("stop", 390, 25, 8, "regular"),
            ("identity", 42, 52, 8, "regular"),
            ("members", 42, 77, 8, "regular"),
            ("relations", 42, 102, 8, "regular"),
            ("counts", 42, 127, 8, "regular"),
        ]
        for suffix, x, offset, size, weight in placements:
            fact_id = f"{base}.{suffix}"
            _add_fact(
                commands,
                facts,
                fact_id,
                x,
                y + offset,
                size=size,
                weight=weight,
                fill=(
                    _scenario_colour(scenario)
                    if suffix == "status"
                    else TOKENS["text"]
                ),
                container_id="region.core_owner",
                semantic_role="criterion" if suffix == "identity" else None,
                scenario_id=scenario["scenario_id"],
            )
    _add_text(
        commands,
        "ledger.query.columns",
        42,
        820,
        "QUERY CASE / OWNER          INCLUDED ANSWER                 BUDGET / BEYOND",
        size=8,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.query_owner",
    )
    _add_scenario_fact_block(
        commands,
        facts,
        ledger["scenarios"][3],
        x=42,
        y=847,
        container_id="region.query_owner",
        line_height=21,
        size=8,
    )
    return scene


def _add_core_route_lane(
    commands: list[dict[str, Any]],
    facts: Mapping[str, str],
    scenario: Mapping[str, Any],
    y: int,
) -> None:
    scenario_id = scenario["scenario_id"]
    base = f"scenario.{scenario_id}"
    _add_fact(
        commands,
        facts,
        f"{base}.status",
        42,
        y,
        size=9,
        weight="bold",
        fill=_scenario_colour(scenario),
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_fact(
        commands,
        facts,
        f"{base}.owner",
        235,
        y,
        size=8,
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_fact(
        commands,
        facts,
        f"{base}.reason",
        42,
        y + 20,
        size=8,
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_fact(
        commands,
        facts,
        f"{base}.stop",
        420,
        y + 20,
        size=8,
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_fact(
        commands,
        facts,
        f"{base}.identity",
        42,
        y + 40,
        size=8,
        container_id="region.core_owner",
        semantic_role="criterion",
        scenario_id=scenario_id,
    )
    boundary_id = f"split.{scenario_id}.boundary"
    criterion_id = f"split.{scenario_id}.criterion"
    _add_rect(
        commands,
        boundary_id,
        60,
        y + 63,
        172,
        38,
        fill=TOKENS["surface_alt"],
        stroke=_scenario_colour(scenario),
        radius=7,
        container_id="region.core_owner",
        semantic_role="boundary",
        scenario_id=scenario_id,
    )
    _add_rect(
        commands,
        criterion_id,
        482,
        y + 63,
        158,
        38,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["criterion"],
        radius=7,
        container_id="region.core_owner",
        semantic_role="criterion_dock",
        scenario_id=scenario_id,
    )
    route_y = y + 94
    _add_polyline(
        commands,
        f"split.{scenario_id}.route",
        [[232, route_y], [482, route_y]],
        stroke=TOKENS["core"],
        stroke_width=3,
        start_anchor_id=boundary_id,
        end_anchor_id=criterion_id,
        container_id="region.core_owner",
        semantic_role="known_route",
        scenario_id=scenario_id,
    )
    _add_polygon(
        commands,
        f"split.{scenario_id}.arrow",
        [[472, route_y - 5], [482, route_y], [472, route_y + 5]],
        fill=TOKENS["core"],
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_circle(
        commands,
        f"split.{scenario_id}.criterion.dot",
        625,
        route_y,
        4,
        fill=TOKENS["criterion"],
        container_id="region.core_owner",
        semantic_role="criterion_marker",
        scenario_id=scenario_id,
    )
    _add_text(
        commands,
        f"split.{scenario_id}.boundary.label",
        72,
        y + 72,
        scenario["boundary_title"],
        size=10,
        weight="bold",
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    _add_text(
        commands,
        f"split.{scenario_id}.criterion.label",
        494,
        y + 72,
        scenario["criterion_title"],
        size=9,
        weight="bold",
        fill=TOKENS["criterion"],
        container_id="region.core_owner",
        scenario_id=scenario_id,
    )
    for index, suffix in enumerate(("members", "relations", "counts")):
        _add_fact(
            commands,
            facts,
            f"{base}.{suffix}",
            42,
            y + 108 + index * 18,
            size=8,
            container_id="region.core_owner",
            scenario_id=scenario_id,
        )


def _split_frontier_routes_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> dict[str, Any]:
    scene, commands, facts = _base_scene(ledger, condition_id)
    for index, scenario in enumerate(ledger["scenarios"][:3]):
        _add_core_route_lane(commands, facts, scenario, 190 + index * 178)
    query = ledger["scenarios"][3]
    base = "scenario.LC6_TRUNCATED"
    for suffix, x, y in (
        ("status", 42, 820),
        ("owner", 235, 820),
        ("reason", 42, 840),
        ("stop", 420, 840),
        ("identity", 42, 860),
    ):
        _add_fact(
            commands,
            facts,
            f"{base}.{suffix}",
            x,
            y,
            size=8 if suffix != "status" else 9,
            weight="bold" if suffix == "status" else "regular",
            fill=_scenario_colour(query) if suffix == "status" else None,
            container_id="region.query_owner",
            semantic_role="criterion" if suffix == "identity" else None,
            scenario_id="LC6_TRUNCATED",
    )
    omitted_id = "split.LC6_TRUNCATED.omitted"
    _add_rect(
        commands,
        omitted_id,
        50,
        891,
        120,
        46,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["muted"],
        radius=7,
        container_id="region.query_owner",
        semantic_role="omitted_aggregate",
        scenario_id="LC6_TRUNCATED",
    )
    _add_text(
        commands,
        "split.LC6_TRUNCATED.omitted.label",
        61,
        900,
        "3 nodes / 3 edges omitted",
        size=8,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.query_owner",
        scenario_id="LC6_TRUNCATED",
    )
    selected = query["nodes"]
    node_x = [236, 337, 438, 539]
    node_ids = [
        f"split.LC6_TRUNCATED.node.{index}" for index in range(len(selected))
    ]
    route_y = 928
    _add_polyline(
        commands,
        "split.LC6_TRUNCATED.route",
        [[170, route_y], [539, route_y]],
        stroke=TOKENS["query"],
        stroke_width=3,
        dash=[8, 5],
        start_anchor_id=omitted_id,
        end_anchor_id=node_ids[-1],
        container_id="region.query_owner",
        semantic_role="query_route",
        scenario_id="LC6_TRUNCATED",
    )
    for index, (node, x) in enumerate(zip(selected, node_x)):
        node_id = node_ids[index]
        _add_rect(
            commands,
            node_id,
            x,
            891,
            86,
            46,
            fill=TOKENS["surface_alt"],
            stroke=(
                TOKENS["criterion"] if index == len(selected) - 1 else TOKENS["query"]
            ),
            radius=7,
            container_id="region.query_owner",
            semantic_role=(
                "criterion_dock" if index == len(selected) - 1 else "selected_node"
            ),
            scenario_id="LC6_TRUNCATED",
        )
        _add_text(
            commands,
            f"{node_id}.label",
            x + 8,
            900,
            node["title"].replace("Set LC6Truncated", "Set …"),
            size=8,
            weight="bold",
            fill=(
                TOKENS["criterion"] if index == len(selected) - 1 else TOKENS["text"]
            ),
            container_id="region.query_owner",
            scenario_id="LC6_TRUNCATED",
        )
    _add_polygon(
        commands,
        "split.LC6_TRUNCATED.frontier",
        [[190, 906], [208, 928], [190, 950], [172, 928]],
        fill=TOKENS["background"],
        stroke=TOKENS["query"],
        stroke_width=3,
        container_id="region.query_owner",
        semantic_role="frontier_break",
        scenario_id="LC6_TRUNCATED",
    )
    _add_text(
        commands,
        "split.LC6_TRUNCATED.frontier.label",
        173,
        912,
        "F",
        size=10,
        weight="bold",
        fill=TOKENS["query"],
        container_id="region.query_owner",
        scenario_id="LC6_TRUNCATED",
    )
    for index, suffix in enumerate(
        ("members", "relations", "counts", "budget", "complete", "frontier")
    ):
        _add_fact(
            commands,
            facts,
            f"{base}.{suffix}",
            42,
            962 + index * 20,
            size=8,
            container_id="region.query_owner",
            scenario_id="LC6_TRUNCATED",
        )
    return scene


def _threshold_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> dict[str, Any]:
    scene, commands, facts = _base_scene(ledger, condition_id)
    _add_text(
        commands,
        "threshold.known.heading",
        58,
        190,
        "KNOWN IN THIS ANSWER",
        size=9,
        weight="bold",
        fill=TOKENS["criterion"],
        container_id="region.core_owner",
    )
    _add_text(
        commands,
        "threshold.beyond.heading",
        450,
        190,
        "BEYOND THIS ANSWER",
        size=9,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.core_owner",
    )
    for index, scenario in enumerate(ledger["scenarios"][:3]):
        scenario_id = scenario["scenario_id"]
        base = f"scenario.{scenario_id}"
        y = 218 + index * 174
        for suffix, x, offset in (
            ("status", 42, 0),
            ("owner", 220, 0),
            ("reason", 42, 42),
            ("stop", 430, 42),
            ("identity", 42, 22),
            ("members", 42, 112),
            ("relations", 42, 132),
            ("counts", 42, 152),
        ):
            _add_fact(
                commands,
                facts,
                f"{base}.{suffix}",
                x,
                y + offset,
                size=8 if suffix != "status" else 9,
                weight="bold" if suffix == "status" else "regular",
                fill=(
                    _scenario_colour(scenario) if suffix == "status" else None
                ),
                container_id="region.core_owner",
                semantic_role="criterion" if suffix == "identity" else None,
                scenario_id=scenario_id,
            )
        known_id = f"threshold.{scenario_id}.known"
        beyond_id = f"threshold.{scenario_id}.beyond"
        _add_rect(
            commands,
            known_id,
            60,
            y + 60,
            280,
            40,
            fill=TOKENS["surface_alt"],
            stroke=TOKENS["criterion"],
            radius=7,
            container_id="region.core_owner",
            semantic_role="known_answer",
            scenario_id=scenario_id,
        )
        _add_rect(
            commands,
            beyond_id,
            480,
            y + 60,
            150,
            40,
            fill=TOKENS["background"],
            stroke=_scenario_colour(scenario),
            radius=7,
            container_id="region.core_owner",
            semantic_role="beyond_answer",
            scenario_id=scenario_id,
        )
        _add_text(
            commands,
            f"{known_id}.label",
            72,
            y + 70,
            scenario["criterion_title"],
            size=9,
            weight="bold",
            fill=TOKENS["criterion"],
            container_id="region.core_owner",
            scenario_id=scenario_id,
        )
        _add_text(
            commands,
            f"{beyond_id}.label",
            492,
            y + 70,
            scenario["boundary_title"],
            size=9,
            weight="bold",
            fill=_scenario_colour(scenario),
            container_id="region.core_owner",
            scenario_id=scenario_id,
        )
        _add_polyline(
            commands,
            f"threshold.{scenario_id}.crossing",
            [[340, y + 92], [480, y + 92]],
            stroke=TOKENS["muted"],
            stroke_width=2,
            dash=[5, 5],
            start_anchor_id=known_id,
            end_anchor_id=beyond_id,
            container_id="region.core_owner",
            semantic_role="threshold_crossing",
            scenario_id=scenario_id,
        )
        _add_polyline(
            commands,
            f"threshold.{scenario_id}.axis",
            [[410, y + 60], [410, y + 100]],
            stroke=TOKENS["query"],
            stroke_width=2,
            dash=[6, 5],
            container_id="region.core_owner",
            semantic_role="threshold_axis",
            scenario_id=scenario_id,
        )
    query = ledger["scenarios"][3]
    base = "scenario.LC6_TRUNCATED"
    _add_text(
        commands,
        "threshold.query.known.heading",
        58,
        820,
        "KNOWN · SELECTED 4 / 3",
        size=9,
        weight="bold",
        fill=TOKENS["criterion"],
        container_id="region.query_owner",
    )
    _add_text(
        commands,
        "threshold.query.beyond.heading",
        450,
        820,
        "BEYOND · OMITTED 3 / 3",
        size=9,
        weight="bold",
        fill=TOKENS["muted"],
        container_id="region.query_owner",
    )
    for suffix, x, y in (
        ("status", 42, 846),
        ("owner", 220, 846),
        ("identity", 42, 868),
        ("reason", 430, 868),
        ("stop", 430, 888),
    ):
        _add_fact(
            commands,
            facts,
            f"{base}.{suffix}",
            x,
            y,
            size=8 if suffix != "status" else 9,
            weight="bold" if suffix == "status" else "regular",
            fill=_scenario_colour(query) if suffix == "status" else None,
            container_id="region.query_owner",
            semantic_role="criterion" if suffix == "identity" else None,
            scenario_id="LC6_TRUNCATED",
        )
    known_id = "threshold.LC6_TRUNCATED.known"
    beyond_id = "threshold.LC6_TRUNCATED.beyond"
    _add_rect(
        commands,
        known_id,
        60,
        915,
        280,
        44,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["criterion"],
        radius=7,
        container_id="region.query_owner",
        semantic_role="known_answer",
        scenario_id="LC6_TRUNCATED",
    )
    _add_rect(
        commands,
        beyond_id,
        480,
        915,
        150,
        44,
        fill=TOKENS["background"],
        stroke=TOKENS["query"],
        radius=7,
        container_id="region.query_owner",
        semantic_role="beyond_answer",
        scenario_id="LC6_TRUNCATED",
    )
    _add_text(
        commands,
        f"{known_id}.label",
        72,
        926,
        "Selected route · criterion reached",
        size=9,
        weight="bold",
        fill=TOKENS["criterion"],
        container_id="region.query_owner",
        scenario_id="LC6_TRUNCATED",
    )
    _add_text(
        commands,
        f"{beyond_id}.label",
        492,
        926,
        "Root + 2 nodes",
        size=9,
        weight="bold",
        fill=TOKENS["query"],
        container_id="region.query_owner",
        scenario_id="LC6_TRUNCATED",
    )
    _add_polyline(
        commands,
        "threshold.LC6_TRUNCATED.crossing",
        [[340, 950], [480, 950]],
        stroke=TOKENS["query"],
        stroke_width=3,
        dash=[5, 5],
        start_anchor_id=known_id,
        end_anchor_id=beyond_id,
        container_id="region.query_owner",
        semantic_role="threshold_crossing",
        scenario_id="LC6_TRUNCATED",
    )
    _add_polygon(
        commands,
        "threshold.LC6_TRUNCATED.frontier",
        [[410, 934], [423, 950], [410, 966], [397, 950]],
        fill=TOKENS["background"],
        stroke=TOKENS["query"],
        stroke_width=2,
        container_id="region.query_owner",
        semantic_role="frontier_threshold",
        scenario_id="LC6_TRUNCATED",
    )
    for index, suffix in enumerate(
        ("members", "relations", "counts", "budget", "complete", "frontier")
    ):
        _add_fact(
            commands,
            facts,
            f"{base}.{suffix}",
            42,
            978 + index * 20,
            size=8,
            container_id="region.query_owner",
            scenario_id="LC6_TRUNCATED",
        )
    return scene


def build_lc6_scene(
    ledger: Mapping[str, Any], condition_id: str, width: int = 700
) -> dict[str, Any]:
    """Build one deterministic 700px scene without changing LC6 truth."""

    validate_lc6_visual_ledger(ledger)
    _require(width == 700, "first LC6 visual packet is 700px only")
    _require(condition_id in CONDITION_IDS, "unknown LC6 visual condition")
    builders = {
        "LC6_COMPLETE_TEXT": _complete_text_scene,
        "LC6_BOUNDARY_LEDGER": _boundary_ledger_scene,
        "LC6_SPLIT_FRONTIER_ROUTES": _split_frontier_routes_scene,
        "LC6_KNOWN_UNKNOWN_THRESHOLD": _threshold_scene,
    }
    scene = builders[condition_id](ledger, condition_id)
    checks = scene_checks(scene)
    _require(checks["pass"], f"LC6 scene geometry failed: {checks}")
    return scene


def _command_bounds(command: Mapping[str, Any]) -> list[float] | None:
    kind = command.get("kind")
    if kind == "rect":
        values = [
            command.get("x"),
            command.get("y"),
            command.get("width"),
            command.get("height"),
        ]
        if not all(isinstance(value, (int, float)) for value in values):
            return None
        x, y, width, height = values
        return [x, y, x + width, y + height]
    if kind == "text":
        bounds = command.get("bounds")
        return list(bounds) if isinstance(bounds, list) and len(bounds) == 4 else None
    if kind == "circle":
        values = [command.get("cx"), command.get("cy"), command.get("radius")]
        if not all(isinstance(value, (int, float)) for value in values):
            return None
        cx, cy, radius = values
        half = float(command.get("stroke_width") or 0) / 2
        return [cx - radius - half, cy - radius - half, cx + radius + half, cy + radius + half]
    if kind in {"polyline", "polygon"}:
        points = command.get("points")
        if not isinstance(points, list) or len(points) < 2:
            return None
        if not all(
            isinstance(point, list)
            and len(point) == 2
            and all(isinstance(value, (int, float)) for value in point)
            for point in points
        ):
            return None
        half = float(command.get("stroke_width") or 0) / 2
        return [
            min(point[0] for point in points) - half,
            min(point[1] for point in points) - half,
            max(point[0] for point in points) + half,
            max(point[1] for point in points) + half,
        ]
    return None


def _rectangles_overlap(first: list[float], second: list[float]) -> bool:
    return not (
        first[2] <= second[0]
        or second[2] <= first[0]
        or first[3] <= second[1]
        or second[3] <= first[1]
    )


def _segment_intersects_rect(
    start: tuple[float, float],
    end: tuple[float, float],
    rect: list[float],
) -> bool:
    x1, y1 = start
    x2, y2 = end
    left, top, right, bottom = rect
    if x1 == x2:
        return left < x1 < right and max(y1, y2) > top and min(y1, y2) < bottom
    if y1 == y2:
        return top < y1 < bottom and max(x1, x2) > left and min(x1, x2) < right
    steps = max(int(abs(x2 - x1)), int(abs(y2 - y1)), 1)
    return any(
        left < x1 + (x2 - x1) * index / steps < right
        and top < y1 + (y2 - y1) * index / steps < bottom
        for index in range(steps + 1)
    )


def _point_on_rect_boundary(command: Mapping[str, Any], point: list[int]) -> bool:
    bounds = _command_bounds(command)
    if bounds is None:
        return False
    left, top, right, bottom = bounds
    return (
        left <= point[0] <= right
        and top <= point[1] <= bottom
        and (
            point[0] in {left, right}
            or point[1] in {top, bottom}
        )
    )


def scene_checks(scene: Mapping[str, Any]) -> dict[str, Any]:
    """Return strict semantic and geometry checks for one LC6 scene."""

    commands = scene.get("commands", [])
    command_ids = [
        command.get("id")
        for command in commands
        if isinstance(command, Mapping)
    ]
    duplicate_ids = sorted(
        {command_id for command_id in command_ids if command_ids.count(command_id) > 1},
        key=str,
    )
    commands_by_id = {
        command["id"]: command
        for command in commands
        if isinstance(command, Mapping) and isinstance(command.get("id"), str)
    }
    malformed_ids = []
    out_of_bounds_ids = []
    text_measurement_errors = []
    for index, command in enumerate(commands):
        command_id = str(command.get("id", index)) if isinstance(command, Mapping) else str(index)
        bounds = _command_bounds(command) if isinstance(command, Mapping) else None
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
    text_commands = [
        command
        for command in commands
        if isinstance(command, Mapping) and command.get("kind") == "text"
    ]
    text_overlap_pairs = []
    for index, first in enumerate(text_commands):
        first_bounds = _command_bounds(first)
        if first_bounds is None:
            continue
        for second in text_commands[index + 1 :]:
            second_bounds = _command_bounds(second)
            if second_bounds is not None and _rectangles_overlap(
                first_bounds, second_bounds
            ):
                text_overlap_pairs.append([first["id"], second["id"]])
    route_commands = [
        command
        for command in commands
        if isinstance(command, Mapping) and command.get("kind") == "polyline"
    ]
    route_text_collisions = []
    for route in route_commands:
        for text_command in text_commands:
            bounds = _command_bounds(text_command)
            if bounds is None:
                continue
            expanded = [bounds[0] - 2, bounds[1] - 2, bounds[2] + 2, bounds[3] + 2]
            if any(
                _segment_intersects_rect(tuple(start), tuple(end), expanded)
                for start, end in zip(route["points"], route["points"][1:])
            ):
                route_text_collisions.append([route["id"], text_command["id"]])
    containment_errors = []
    for command in commands:
        if not isinstance(command, Mapping) or not command.get("container_id"):
            continue
        container = commands_by_id.get(command["container_id"])
        bounds = _command_bounds(command)
        container_bounds = _command_bounds(container or {})
        if (
            bounds is None
            or container_bounds is None
            or bounds[0] < container_bounds[0]
            or bounds[1] < container_bounds[1]
            or bounds[2] > container_bounds[2]
            or bounds[3] > container_bounds[3]
        ):
            containment_errors.append(command["id"])
    route_attachment_errors = []
    for route in route_commands:
        start_id = route.get("start_anchor_id")
        end_id = route.get("end_anchor_id")
        if start_id is None and end_id is None:
            if route.get("semantic_role") not in {"threshold_axis", None}:
                route_attachment_errors.append(route["id"])
            continue
        start = commands_by_id.get(str(start_id))
        end = commands_by_id.get(str(end_id))
        if (
            start is None
            or end is None
            or not _point_on_rect_boundary(start, route["points"][0])
            or not _point_on_rect_boundary(end, route["points"][-1])
        ):
            route_attachment_errors.append(route["id"])
    information = scene.get("semantic_coverage", {})
    expected_seal = hashlib.sha256(canonical_json_bytes(information)).hexdigest()
    semantic_seal_errors = (
        [] if scene.get("semantic_sha256") == expected_seal else ["semantic_sha256"]
    )
    expected_facts = _display_facts(information)
    fact_commands = [command for command in text_commands if command.get("fact_id")]
    fact_ids = [str(command["fact_id"]) for command in fact_commands]
    missing_fact_ids = sorted(set(expected_facts) - set(fact_ids))
    extra_fact_ids = sorted(set(fact_ids) - set(expected_facts))
    duplicate_fact_ids = sorted(
        {fact_id for fact_id in fact_ids if fact_ids.count(fact_id) > 1}
    )
    fact_value_errors = sorted(
        command["fact_id"]
        for command in fact_commands
        if expected_facts.get(command["fact_id"]) != command.get("fact_value")
        or command.get("text") != command.get("fact_value")
    )
    expected_regions = {
        "core_owner": list(SCENARIO_ORDER[:3]),
        "query_owner": [SCENARIO_ORDER[3]],
    }
    owner_group_errors = []
    for region_id, scenario_ids in expected_regions.items():
        if scene.get("regions", {}).get(region_id, {}).get("scenario_ids") != scenario_ids:
            owner_group_errors.append(region_id)
    criterion_scenarios = {
        command.get("scenario_id")
        for command in text_commands
        if command.get("semantic_role") == "criterion"
    }
    criterion_errors = sorted(set(SCENARIO_ORDER) - criterion_scenarios)
    frontier_fact_id = "scenario.LC6_TRUNCATED.frontier"
    frontier_fact_errors = [] if fact_ids.count(frontier_fact_id) == 1 else [frontier_fact_id]
    frontier_breaks = [
        command
        for command in commands
        if isinstance(command, Mapping)
        and command.get("semantic_role") == "frontier_break"
    ]
    frontier_primitive_errors = []
    if scene.get("condition_id") == "LC6_SPLIT_FRONTIER_ROUTES":
        if len(frontier_breaks) != 1:
            frontier_primitive_errors.append("split_frontier_break")
    elif frontier_breaks:
        frontier_primitive_errors.append("unexpected_frontier_break")
    regions = scene.get("regions", {})
    scenario_region_errors = []
    for fact_command in fact_commands:
        scenario_id = fact_command.get("scenario_id")
        if scenario_id in SCENARIO_ORDER[:3] and fact_command.get("container_id") != "region.core_owner":
            scenario_region_errors.append(fact_command["id"])
        if scenario_id == "LC6_TRUNCATED" and fact_command.get("container_id") != "region.query_owner":
            scenario_region_errors.append(fact_command["id"])
    checks = {
        "canvas": [scene.get("width"), scene.get("height")],
        "condition_errors": (
            [] if scene.get("condition_id") in CONDITION_IDS else [scene.get("condition_id")]
        ),
        "duplicate_command_ids": duplicate_ids,
        "malformed_command_ids": malformed_ids,
        "out_of_bounds_ids": out_of_bounds_ids,
        "text_measurement_errors": text_measurement_errors,
        "text_overlap_pairs": text_overlap_pairs,
        "route_text_collision_pairs": route_text_collisions,
        "containment_errors": containment_errors,
        "route_attachment_errors": route_attachment_errors,
        "semantic_seal_errors": semantic_seal_errors,
        "missing_fact_ids": missing_fact_ids,
        "extra_fact_ids": extra_fact_ids,
        "duplicate_fact_ids": duplicate_fact_ids,
        "fact_value_errors": fact_value_errors,
        "owner_group_errors": owner_group_errors,
        "criterion_errors": criterion_errors,
        "frontier_fact_errors": frontier_fact_errors,
        "frontier_primitive_errors": frontier_primitive_errors,
        "scenario_region_errors": scenario_region_errors,
        "region_count": len(regions),
        "fact_count": len(fact_ids),
        "route_count": len(route_commands),
    }
    checks["pass"] = (
        checks["canvas"] == list(CANVAS)
        and checks["region_count"] == 5
        and checks["fact_count"] == len(expected_facts)
        and not any(
            value
            for key, value in checks.items()
            if key not in {"canvas", "region_count", "fact_count", "route_count", "pass"}
        )
    )
    return checks


def _svg_colour(value: str | None) -> str:
    return value if value is not None else "none"


def svg_for_scene(scene: Mapping[str, Any]) -> str:
    """Render a validated scene to deterministic editable SVG text."""

    checks = scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid LC6 scene: {checks}")
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{scene["width"]}" '
            f'height="{scene["height"]}" viewBox="0 0 {scene["width"]} {scene["height"]}">'
        ),
        "  <title>" + html.escape(str(scene["condition_label"])) + "</title>",
    ]
    for command in scene["commands"]:
        command_id = html.escape(str(command["id"]), quote=True)
        if command["kind"] == "rect":
            lines.append(
                "  <rect "
                f'id="{command_id}" x="{command["x"]}" y="{command["y"]}" '
                f'width="{command["width"]}" height="{command["height"]}" '
                f'rx="{command["radius"]}" fill="{_svg_colour(command["fill"])}" '
                f'stroke="{_svg_colour(command["stroke"])}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "text":
            weight = "700" if command["weight"] == "bold" else "400"
            lines.append(
                "  <text "
                f'id="{command_id}" x="{command["x"]}" y="{command["y"]}" '
                f'fill="{command["fill"]}" font-family="Segoe UI" '
                f'font-size="{command["font_size"]}" font-weight="{weight}" '
                'dominant-baseline="text-before-edge">'
                + html.escape(str(command["text"]))
                + "</text>"
            )
        elif command["kind"] == "polyline":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            dash = (
                f' stroke-dasharray="{",".join(map(str, command["dash"]))}"'
                if command["dash"]
                else ""
            )
            lines.append(
                "  <polyline "
                f'id="{command_id}" points="{points}" fill="none" '
                f'stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"'
                f'{dash} stroke-linecap="round" stroke-linejoin="round"/>'
            )
        elif command["kind"] == "circle":
            lines.append(
                "  <circle "
                f'id="{command_id}" cx="{command["cx"]}" cy="{command["cy"]}" '
                f'r="{command["radius"]}" fill="{command["fill"]}" '
                f'stroke="{_svg_colour(command["stroke"])}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "polygon":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            lines.append(
                "  <polygon "
                f'id="{command_id}" points="{points}" fill="{command["fill"]}" '
                f'stroke="{_svg_colour(command["stroke"])}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _draw_dashed_segment(
    draw: ImageDraw.ImageDraw,
    start: tuple[int, int],
    end: tuple[int, int],
    fill: str,
    width: int,
    dash: list[int],
) -> None:
    from math import hypot

    length = hypot(end[0] - start[0], end[1] - start[1])
    if length == 0:
        return
    dx = (end[0] - start[0]) / length
    dy = (end[1] - start[1]) / length
    position = 0.0
    draw_segment = True
    dash_index = 0
    while position < length:
        next_position = min(length, position + dash[dash_index % len(dash)])
        if draw_segment:
            draw.line(
                [
                    (start[0] + dx * position, start[1] + dy * position),
                    (start[0] + dx * next_position, start[1] + dy * next_position),
                ],
                fill=fill,
                width=width,
            )
        draw_segment = not draw_segment
        dash_index += 1
        position = next_position


def png_for_scene(scene: Mapping[str, Any], path: str | Path) -> None:
    """Render a validated scene to deterministic PNG bytes with Pillow."""

    checks = scene_checks(scene)
    _require(checks["pass"], f"cannot render invalid LC6 scene: {checks}")
    image = Image.new("RGB", (scene["width"], scene["height"]), TOKENS["background"])
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        if command["kind"] == "rect":
            box = [
                command["x"],
                command["y"],
                command["x"] + command["width"],
                command["y"] + command["height"],
            ]
            draw.rounded_rectangle(
                box,
                radius=command["radius"],
                fill=command["fill"],
                outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif command["kind"] == "text":
            draw.text(
                (command["x"], command["y"]),
                command["text"],
                font=_font(command["font_size"], command["weight"]),
                fill=command["fill"],
                anchor="lt",
            )
        elif command["kind"] == "polyline":
            points = [tuple(point) for point in command["points"]]
            if command["dash"]:
                for start, end in zip(points, points[1:]):
                    _draw_dashed_segment(
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
                    joint="curve",
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
                outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif command["kind"] == "polygon":
            draw.polygon(
                [tuple(point) for point in command["points"]],
                fill=command["fill"],
                outline=command["stroke"],
                width=command["stroke_width"],
            )
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target, format="PNG", optimize=False, compress_level=9)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _comparison_board_svg(scenes: list[Mapping[str, Any]]) -> str:
    _require(
        [scene["condition_id"] for scene in scenes] == list(CONDITION_IDS),
        "LC6 comparison board scene order differs",
    )
    for scene in scenes:
        _require(scene_checks(scene)["pass"], "LC6 comparison board scene failed")
    width, height = 1460, 2640
    positions = ((20, 80), (740, 80), (20, 1360), (740, 1360))
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}">'
        ),
        "  <title>LC6 700px information-matched authoring comparison</title>",
        f'  <rect width="{width}" height="{height}" fill="{TOKENS["background"]}"/>',
        (
            '  <text x="20" y="22" fill="#F2F5F8" font-family="Segoe UI" '
            'font-size="22" font-weight="700" dominant-baseline="text-before-edge">'
            "LC6 · 700px information-matched authoring comparison</text>"
        ),
        (
            '  <text x="20" y="51" fill="#A9B3C1" font-family="Segoe UI" '
            'font-size="11" dominant-baseline="text-before-edge">'
            "Recommended: Split Frontier Routes · selected: none · default: none</text>"
        ),
    ]
    for scene, (x, y) in zip(scenes, positions):
        condition_id = str(scene["condition_id"])
        body = svg_for_scene(scene).splitlines()[3:-1]
        prefix = condition_id.lower() + "__"
        lines.append(
            f'  <g id="board__{condition_id.lower()}" transform="translate({x} {y})">'
        )
        lines.extend(
            "  " + line.replace('id="', f'id="{prefix}') for line in body
        )
        lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _comparison_board_png(
    scenes: list[Mapping[str, Any]], effect_paths: Mapping[str, Path], path: Path
) -> None:
    width, height = 1460, 2640
    positions = ((20, 80), (740, 80), (20, 1360), (740, 1360))
    board = Image.new("RGB", (width, height), TOKENS["background"])
    draw = ImageDraw.Draw(board)
    draw.text(
        (20, 22),
        "LC6 · 700px information-matched authoring comparison",
        font=_font(22, "bold"),
        fill=TOKENS["text"],
        anchor="lt",
    )
    draw.text(
        (20, 51),
        "Recommended: Split Frontier Routes · selected: none · default: none",
        font=_font(11, "regular"),
        fill=TOKENS["muted"],
        anchor="lt",
    )
    for scene, position in zip(scenes, positions):
        png_name = CONDITIONS[scene["condition_id"]][2] + ".png"
        with Image.open(effect_paths[png_name]) as effect:
            _require(
                effect.size == CANVAS,
                f"LC6 effect dimensions differ on comparison board: {png_name}",
            )
            board.paste(effect.convert("RGB"), position)
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _packet_hashes(directory: Path, names: list[str]) -> dict[str, Any]:
    entries = []
    for name in sorted(names):
        path = directory / name
        payload = path.read_bytes()
        entries.append(
            {
                "path": name,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    return {
        "format": "blueprint-lens-lc6-self-excluding-hashes",
        "format_version": "1.0.0",
        "algorithm": "sha256",
        "excluded_path": "hashes.json",
        "entry_count": len(entries),
        "entries": entries,
    }


def _validate_packet(directory: Path) -> None:
    paths = sorted(path for path in directory.iterdir() if path.is_file())
    _require(len(paths) == 14, "LC6 visual packet must contain exactly 14 files")
    hashes = _read_json(directory / "hashes.json")
    _require(
        hashes.get("excluded_path") == "hashes.json"
        and hashes.get("entry_count") == 13,
        "LC6 visual hash inventory is not self-excluding 13/14",
    )
    expected_names = {path.name for path in paths} - {"hashes.json"}
    entries = hashes.get("entries")
    _require(isinstance(entries, list), "LC6 visual hash entries are missing")
    _require(
        {entry.get("path") for entry in entries} == expected_names,
        "LC6 visual hash inventory coverage differs",
    )
    for entry in entries:
        path = directory / entry["path"]
        payload = path.read_bytes()
        _require(
            entry.get("bytes") == len(payload)
            and entry.get("sha256") == hashlib.sha256(payload).hexdigest(),
            f"LC6 visual packet hash differs: {entry['path']}",
        )


def _directories_identical(first: Path, second: Path) -> bool:
    first_entries = sorted((path.name, path.is_file()) for path in first.iterdir())
    second_entries = sorted((path.name, path.is_file()) for path in second.iterdir())
    if first_entries != second_entries or not all(is_file for _, is_file in first_entries):
        return False
    first_names = [name for name, _ in first_entries]
    return all(
        (first / name).read_bytes() == (second / name).read_bytes()
        for name in first_names
    )


def build_lc6_visual_artifacts(
    evidence_dir: str | Path, output_dir: str | Path
) -> dict[str, Path]:
    """Atomically publish the exact deterministic 14-file LC6 packet."""

    evidence = Path(evidence_dir).resolve()
    destination = Path(output_dir).resolve()
    _require(
        not destination.exists() or destination.is_dir(),
        "LC6 visual destination exists and is not a directory",
    )
    for path in FONTS.values():
        _require(Path(path).is_file(), f"LC6 visual font is unavailable: {path}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(
            prefix=f".{destination.name}.staging-", dir=destination.parent
        )
    )
    try:
        ledger = load_lc6_visual_ledger(evidence)
        manifest = build_lc6_visual_manifest(ledger)
        scenes = [
            build_lc6_scene(ledger, condition_id, 700)
            for condition_id in CONDITION_IDS
        ]
        checks = {
            scene["condition_id"]: scene_checks(scene) for scene in scenes
        }
        _require(
            all(check["pass"] for check in checks.values()),
            "LC6 visual geometry oracle failed",
        )
        files: dict[str, Path] = {}
        ledger_name = "lc6-visual-ledger.json"
        manifest_name = "lc6-visual-manifest.json"
        oracle_name = "lc6-geometry-oracle.json"
        (staging / ledger_name).write_bytes(_json_bytes(ledger))
        (staging / manifest_name).write_bytes(_json_bytes(manifest))
        oracle = {
            "format": "blueprint-lens-lc6-geometry-oracle",
            "format_version": "1.0.0",
            "status": "PASS",
            "canvas": list(CANVAS),
            "condition_order": list(CONDITION_IDS),
            "checks": checks,
            "semantic_sha256": ledger["semantic_sha256"],
        }
        (staging / oracle_name).write_bytes(_json_bytes(oracle))
        for name in (ledger_name, manifest_name, oracle_name):
            files[name] = staging / name
        for scene in scenes:
            slug = CONDITIONS[scene["condition_id"]][2]
            svg_name, png_name = f"{slug}.svg", f"{slug}.png"
            (staging / svg_name).write_text(
                svg_for_scene(scene), encoding="utf-8", newline="\n"
            )
            png_for_scene(scene, staging / png_name)
            files[svg_name] = staging / svg_name
            files[png_name] = staging / png_name
        board_svg_name = "lc6-comparison-board.svg"
        board_png_name = "lc6-comparison-board.png"
        (staging / board_svg_name).write_text(
            _comparison_board_svg(scenes), encoding="utf-8", newline="\n"
        )
        _comparison_board_png(scenes, files, staging / board_png_name)
        files[board_svg_name] = staging / board_svg_name
        files[board_png_name] = staging / board_png_name
        _require(len(files) == 13, "LC6 packet pre-hash file count differs")
        hashes = _packet_hashes(staging, list(files))
        (staging / "hashes.json").write_bytes(_json_bytes(hashes))
        files["hashes.json"] = staging / "hashes.json"
        _validate_packet(staging)
        if destination.exists():
            _require(
                _directories_identical(staging, destination),
                "LC6 visual destination exists with different bytes",
            )
        else:
            os.replace(staging, destination)
        return {name: destination / name for name in sorted(files)}
    finally:
        if staging.exists():
            shutil.rmtree(staging)
