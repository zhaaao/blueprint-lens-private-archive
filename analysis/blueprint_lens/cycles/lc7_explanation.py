"""Project the bounded LC7 static SCC into the complete Explanation contract."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from ..explanation_model import (
    CompleteDagRelationProjection,
    CompleteDagUnitProjection,
    ExplanationModelError,
    build_complete_cyclic_explanation,
)
from ..raw_probe import Edge, Node, load_blueprint_lens_v1


FIXTURE_SHAPE_INVALID = "LC7_FIXTURE_SHAPE_INVALID"
SCC_GROUP_INVALID = "LC7_SCC_GROUP_INVALID"
SCC_MEMBERSHIP_INVALID = "LC7_SCC_MEMBERSHIP_INVALID"
SCC_EDGE_OWNERSHIP_INVALID = "LC7_SCC_EDGE_OWNERSHIP_INVALID"
SCC_BOUNDARY_INVALID = "LC7_SCC_BOUNDARY_INVALID"
RUNTIME_CLAIM_INVALID = "LC7_RUNTIME_CLAIM_INVALID"

_QUESTION = (
    "Which source-visible control units and relations form the recurrence "
    "upstream of Set LC7Complete?"
)


class LC7ExplanationError(ValueError):
    """Fail-closed LC7 Explanation diagnostic."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


def _error(code: str, message: str) -> LC7ExplanationError:
    return LC7ExplanationError(code, message)


def _read_object(path: Path, label: str) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise _error(FIXTURE_SHAPE_INVALID, f"cannot read {label}") from error
    if not isinstance(value, Mapping):
        raise _error(FIXTURE_SHAPE_INVALID, f"{label} must be an object")
    return value


def _short_class(node: Node) -> str:
    return node.class_path.rsplit(".", 1)[-1]


def _unit_id(node: Node, role: str) -> str:
    return f"unit.{role}.{node.native_guid.lower()}"


def _relation_id(edge_id: str, kind: str) -> str:
    suffix = hashlib.sha256(edge_id.encode("utf-8")).hexdigest()[:16]
    return f"relation.{kind}.{suffix}"


def _unique_strings(value: Any, field: str, code: str) -> tuple[str, ...]:
    if (
        not isinstance(value, list)
        or any(not isinstance(item, str) or not item for item in value)
        or len(value) != len(set(value))
    ):
        raise _error(code, f"{field} must contain unique nonempty strings")
    return tuple(sorted(value))


def _selected_entities(
    ir_path: Path,
    slice_path: Path,
) -> tuple[
    Mapping[str, Any],
    Mapping[str, Node],
    Mapping[str, Edge],
    Mapping[str, Any],
]:
    try:
        document = load_blueprint_lens_v1(ir_path)
    except ValueError as error:
        raise _error(FIXTURE_SHAPE_INVALID, str(error)) from error
    slice_value = _read_object(slice_path, "LC7 slice")
    graph_id = slice_value.get("graph_id")
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        raise _error(FIXTURE_SHAPE_INVALID, "LC7 slice graph must resolve once")
    graph = matches[0]
    node_ids = _unique_strings(
        slice_value.get("node_ids"), "slice.node_ids", FIXTURE_SHAPE_INVALID
    )
    edge_ids = _unique_strings(
        slice_value.get("edge_ids"), "slice.edge_ids", FIXTURE_SHAPE_INVALID
    )
    all_nodes = {node.id: node for node in graph.nodes}
    all_edges = {edge.id: edge for edge in graph.edges}
    if (
        len(node_ids) != 8
        or len(edge_ids) != 8
        or set(node_ids) - set(all_nodes)
        or set(edge_ids) - set(all_edges)
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "LC7 selected closure differs from 8/8")
    return (
        slice_value,
        {node_id: all_nodes[node_id] for node_id in node_ids},
        {edge_id: all_edges[edge_id] for edge_id in edge_ids},
        {
            "blueprint_asset_path": document.blueprint_path,
            "graph_id": graph.id,
        },
    )


def _unit_projection(node: Node, criterion_id: str, members: set[str]) -> CompleteDagUnitProjection:
    node_class = _short_class(node)
    if node.id == criterion_id:
        role = "criterion"
    elif node.id in members:
        role = "control"
    elif node_class in {"K2Node_VariableGet", "K2Node_CallFunction"}:
        role = "predicate"
    elif node_class in {"K2Node_CustomEvent", "K2Node_VariableSet"}:
        role = "control"
    else:
        raise _error(FIXTURE_SHAPE_INVALID, f"unsupported selected node: {node.id}")
    if not node.title:
        raise _error(FIXTURE_SHAPE_INVALID, f"selected node title is empty: {node.id}")
    return CompleteDagUnitProjection(role=role, kind="node", title=node.title)


def _relation_projection(
    edge: Edge,
    nodes: Mapping[str, Node],
) -> CompleteDagRelationProjection:
    source = nodes[edge.source_node_id]
    target = nodes[edge.target_node_id]
    source_pin = next(
        (pin for pin in source.pins if pin.id == edge.source_pin_id), None
    )
    target_pin = next(
        (pin for pin in target.pins if pin.id == edge.target_pin_id), None
    )
    if source_pin is None or target_pin is None:
        raise _error(FIXTURE_SHAPE_INVALID, f"selected edge pin is unresolved: {edge.id}")
    if edge.kind == "execution":
        kind = (
            "controls_execution"
            if _short_class(source) == "K2Node_IfThenElse"
            else "execution_predecessor"
        )
        label = source_pin.name
    elif edge.kind == "data":
        kind = (
            "predicate_for"
            if _short_class(target) == "K2Node_IfThenElse"
            else "provides_value"
        )
        label = target_pin.name
    else:
        raise _error(FIXTURE_SHAPE_INVALID, f"unsupported selected edge: {edge.id}")
    if not label:
        raise _error(FIXTURE_SHAPE_INVALID, f"selected relation label is empty: {edge.id}")
    return CompleteDagRelationProjection(kind=kind, label=label)


def build_lc7_static_scc_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    profile: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the complete 8/8 Explanation and one structural-only SCC group."""

    if profile.get("runtime_iterations") != "NOT_CLAIMED":
        raise _error(RUNTIME_CLAIM_INVALID, "runtime iterations are outside scope")
    scc = profile.get("scc")
    binding = profile.get("source_binding")
    if not isinstance(scc, Mapping) or not isinstance(binding, Mapping):
        raise _error(FIXTURE_SHAPE_INVALID, "LC7 profile binding or SCC is missing")
    members = _unique_strings(
        scc.get("member_node_ids"), "member_node_ids", SCC_MEMBERSHIP_INVALID
    )
    internal = _unique_strings(
        scc.get("internal_edge_ids"),
        "internal_edge_ids",
        SCC_EDGE_OWNERSHIP_INVALID,
    )
    if len(members) != 3 or len(internal) != 3:
        raise _error(SCC_MEMBERSHIP_INVALID, "LC7 profile SCC differs from 3/3")
    criterion_id = profile.get("criterion_node_id")
    if not isinstance(criterion_id, str) or criterion_id in members:
        raise _error(SCC_MEMBERSHIP_INVALID, "criterion must remain outside the SCC")

    slice_value, nodes, edges, source_identity = _selected_entities(
        Path(ir_path), Path(slice_path)
    )
    if (
        source_identity["blueprint_asset_path"] != binding.get("blueprint_asset_path")
        or source_identity["graph_id"] != binding.get("graph_id")
        or slice_value.get("criterion", {}).get("node_id") != criterion_id
        or criterion_id not in nodes
        or set(members) - set(nodes)
        or set(internal) - set(edges)
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "profile does not bind the selected closure")

    unit_projections = {
        node_id: _unit_projection(node, criterion_id, set(members))
        for node_id, node in nodes.items()
    }
    relation_projections = {
        edge_id: _relation_projection(edge, nodes)
        for edge_id, edge in edges.items()
    }
    unit_ids = {
        node_id: _unit_id(nodes[node_id], unit_projections[node_id].role)
        for node_id in nodes
    }
    relation_ids = {
        edge_id: _relation_id(edge_id, relation_projections[edge_id].kind)
        for edge_id in edges
    }
    group_fingerprint = hashlib.sha256(
        ("\n".join((*members, *internal))).encode("utf-8")
    ).hexdigest()[:16]
    group = {
        "id": f"group.scc.{group_fingerprint}",
        "kind": "scc",
        "title": "",
        "ordered_unit_ids": sorted(unit_ids[node_id] for node_id in members),
        "ordered_relation_ids": sorted(relation_ids[edge_id] for edge_id in internal),
        "entry_unit_id": unit_ids[str(scc.get("entry_node_id"))],
        "exit_unit_id": unit_ids[str(scc.get("exit_node_id"))],
        "parent_group_id": None,
        "entered_by": None,
        "member_count": 3,
        "projection_status": "STRUCTURAL_ONLY",
        "diagnostic_code": "",
        "claim_evidence": [],
    }
    try:
        model = build_complete_cyclic_explanation(
            Path(ir_path),
            Path(slice_path),
            Path(asset_file),
            unit_projections,
            relation_projections,
            question=_QUESTION,
            groups=[group],
        )
    except (ExplanationModelError, KeyError) as error:
        raise _error(FIXTURE_SHAPE_INVALID, str(error)) from error
    validate_lc7_static_scc_explanation(model, profile)
    return model


def _unit_by_source(model: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    units = model.get("units")
    if not isinstance(units, list) or len(units) != 8:
        raise _error(FIXTURE_SHAPE_INVALID, "Explanation must contain eight units")
    unit_ids: set[str] = set()
    for unit in units:
        if not isinstance(unit, Mapping) or not isinstance(unit.get("id"), str):
            raise _error(FIXTURE_SHAPE_INVALID, "Explanation unit shape is invalid")
        if unit["id"] in unit_ids:
            raise _error(FIXTURE_SHAPE_INVALID, "canonical unit identity is duplicated")
        unit_ids.add(unit["id"])
        references = unit.get("source_references")
        if not isinstance(references, list) or len(references) != 1:
            raise _error(FIXTURE_SHAPE_INVALID, "unit source action must resolve once")
        reference = references[0]
        if not isinstance(reference, Mapping):
            raise _error(FIXTURE_SHAPE_INVALID, "unit source action is invalid")
        node_id = reference.get("source_node_id")
        pins = reference.get("source_pin_ids")
        if (
            not isinstance(node_id, str)
            or node_id in result
            or not isinstance(pins, list)
            or not pins
            or len(pins) != len(set(pins))
        ):
            raise _error(FIXTURE_SHAPE_INVALID, "unit source action is incomplete")
        result[node_id] = unit
    return result


def _relation_by_source(model: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    relations = model.get("relations")
    if not isinstance(relations, list) or len(relations) != 8:
        raise _error(FIXTURE_SHAPE_INVALID, "Explanation must contain eight relations")
    relation_ids: set[str] = set()
    for relation in relations:
        if not isinstance(relation, Mapping) or not isinstance(relation.get("id"), str):
            raise _error(FIXTURE_SHAPE_INVALID, "Explanation relation shape is invalid")
        if relation["id"] in relation_ids:
            raise _error(FIXTURE_SHAPE_INVALID, "canonical relation identity is duplicated")
        relation_ids.add(relation["id"])
        edge_ids = relation.get("source_edge_ids")
        endpoints = relation.get("source_edge_endpoints")
        if (
            not isinstance(edge_ids, list)
            or len(edge_ids) != 1
            or not isinstance(endpoints, list)
            or len(endpoints) != 1
            or not isinstance(endpoints[0], Mapping)
            or endpoints[0].get("source_edge_id") != edge_ids[0]
            or edge_ids[0] in result
        ):
            raise _error(FIXTURE_SHAPE_INVALID, "relation source action is incomplete")
        result[edge_ids[0]] = relation
    return result


def validate_lc7_static_scc_explanation(
    model: Mapping[str, Any],
    profile: Mapping[str, Any],
) -> None:
    """Require exact SCC ownership and complete source actions in Explanation."""

    if profile.get("runtime_iterations") != "NOT_CLAIMED":
        raise _error(RUNTIME_CLAIM_INVALID, "runtime iterations are outside scope")
    counts = model.get("counts")
    if (
        not isinstance(counts, Mapping)
        or counts.get("units") != 8
        or counts.get("relations") != 8
        or counts.get("source_nodes") != 8
        or counts.get("source_edges") != 8
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "Explanation counts differ from 8/8")
    units = _unit_by_source(model)
    relations = _relation_by_source(model)
    groups = model.get("groups")
    if not isinstance(groups, list) or len(groups) != 1 or not isinstance(groups[0], Mapping):
        raise _error(SCC_GROUP_INVALID, "Explanation requires exactly one SCC group")
    group = groups[0]
    if (
        group.get("kind") != "scc"
        or group.get("projection_status") != "STRUCTURAL_ONLY"
        or group.get("title") != ""
        or group.get("claim_evidence") != []
        or group.get("member_count") != 3
    ):
        raise _error(SCC_GROUP_INVALID, "SCC group status or copy is invalid")

    scc = profile.get("scc")
    if not isinstance(scc, Mapping):
        raise _error(FIXTURE_SHAPE_INVALID, "LC7 profile SCC is missing")
    members = _unique_strings(
        scc.get("member_node_ids"), "member_node_ids", SCC_MEMBERSHIP_INVALID
    )
    group_units = _unique_strings(
        group.get("ordered_unit_ids"), "ordered_unit_ids", SCC_MEMBERSHIP_INVALID
    )
    if len(group_units) != 3 or group_units != tuple(sorted(units[node]["id"] for node in members)):
        raise _error(SCC_MEMBERSHIP_INVALID, "SCC group membership differs from profile")
    criterion_id = profile.get("criterion_node_id")
    if not isinstance(criterion_id, str) or criterion_id not in units:
        raise _error(FIXTURE_SHAPE_INVALID, "criterion unit does not resolve")
    if units[criterion_id]["id"] in group_units:
        raise _error(SCC_MEMBERSHIP_INVALID, "criterion appears inside SCC group")

    internal = _unique_strings(
        scc.get("internal_edge_ids"),
        "internal_edge_ids",
        SCC_EDGE_OWNERSHIP_INVALID,
    )
    group_relations = _unique_strings(
        group.get("ordered_relation_ids"),
        "ordered_relation_ids",
        SCC_EDGE_OWNERSHIP_INVALID,
    )
    expected_relation_ids = tuple(sorted(relations[edge]["id"] for edge in internal))
    if len(group_relations) != 3 or group_relations != expected_relation_ids:
        raise _error(SCC_EDGE_OWNERSHIP_INVALID, "SCC internal relation ownership differs")
    if any(
        relations[edge].get("kind")
        not in {"execution_predecessor", "controls_execution"}
        for edge in internal
    ):
        raise _error(SCC_EDGE_OWNERSHIP_INVALID, "SCC owns a non-execution relation")

    entry_id = scc.get("entry_node_id")
    exit_id = scc.get("exit_node_id")
    if (
        not isinstance(entry_id, str)
        or not isinstance(exit_id, str)
        or entry_id not in units
        or exit_id not in units
        or group.get("entry_unit_id") != units[entry_id]["id"]
        or group.get("exit_unit_id") != units[exit_id]["id"]
        or entry_id != exit_id
    ):
        raise _error(SCC_BOUNDARY_INVALID, "SCC entry or exit differs from profile")


def build_lc7_information_inventory(
    model: Mapping[str, Any],
    profile: Mapping[str, Any],
) -> dict[str, Any]:
    """Return the complete structural inventory consumed by later LC7 surfaces."""

    validate_lc7_static_scc_explanation(model, profile)
    units = _unit_by_source(model)
    scc = profile["scc"]
    return {
        "question": model["query"]["question"],
        "criterion_node_id": profile["criterion_node_id"],
        "member_node_ids": list(scc["member_node_ids"]),
        "internal_edge_ids": list(scc["internal_edge_ids"]),
        "incoming_edge_ids": list(scc["incoming_edge_ids"]),
        "outgoing_edge_ids": list(scc["outgoing_edge_ids"]),
        "returning_edge_ids": list(scc["returning_edge_ids"]),
        "entry_node_id": scc["entry_node_id"],
        "exit_node_id": scc["exit_node_id"],
        "runtime_iterations": "Runtime iterations: NOT_CLAIMED",
        "source_actions": [
            {
                "unit_id": units[node_id]["id"],
                "source_node_id": node_id,
                "source_pin_ids": list(
                    units[node_id]["source_references"][0]["source_pin_ids"]
                ),
            }
            for node_id in sorted(units)
        ],
    }
