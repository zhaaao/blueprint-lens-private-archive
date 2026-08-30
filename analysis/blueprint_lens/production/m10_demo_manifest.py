"""Validation for the retained M10 composition-demonstration evidence.

The case matrix is a declaration.  This module deliberately checks the current
repository asset, typed IR, Explanation packets, screenshots, and LC5 scroll
ledger so that the declaration cannot validate itself in their absence.
"""

from __future__ import annotations

import hashlib
import json
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

from ..schema_validation import SchemaValidationError, validate_instance


ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = ROOT / "artifacts/m10/ue/m10-composition-demo-case-matrix.v1.json"
SCHEMA_PATH = ROOT / "schemas/blueprint-lens-m10-demo-case-matrix-v1.schema.json"
MEASUREMENTS_PATH = (
    ROOT
    / "artifacts/m10/ue/m10-composition-demo/scenario-measurements.v1.json"
)
VISUAL_MANIFEST_PATH = (
    ROOT / "artifacts/m10/ue/m10-composition-demo/visual-manifest.v1.json"
)
POST_REPAIR_EVIDENCE_ROLES = frozenset(
    {
        "cold_build_log",
        "focused_ue_log",
        "full_ue_log",
        "full_python_log",
        "editor_log",
        "verification_script",
        "visible_png",
        "widget_tree",
        "review_record",
    }
)


def _records(value: Any, field: str, errors: list[str]) -> list[Mapping[str, Any]]:
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        errors.append(f"{field} is not an object array")
        return []
    return value


def _strong_components(
    unit_ids: Iterable[str], relations: Sequence[Mapping[str, Any]]
) -> tuple[frozenset[str], ...]:
    """Return non-trivial SCCs for the same two execution families as live LC7."""

    adjacency = {unit_id: set() for unit_id in unit_ids}
    reverse = {unit_id: set() for unit_id in unit_ids}
    for relation in relations:
        if relation.get("kind") not in {
            "execution_predecessor",
            "controls_execution",
        }:
            continue
        source = str(relation.get("source_unit_id", ""))
        target = str(relation.get("target_unit_id", ""))
        if source in adjacency and target in adjacency:
            adjacency[source].add(target)
            reverse[target].add(source)

    visited: set[str] = set()
    order: list[str] = []

    def visit(unit_id: str) -> None:
        if unit_id in visited:
            return
        visited.add(unit_id)
        for target in sorted(adjacency[unit_id]):
            visit(target)
        order.append(unit_id)

    for unit_id in sorted(adjacency):
        visit(unit_id)

    visited.clear()
    components: list[frozenset[str]] = []

    def collect(unit_id: str, members: set[str]) -> None:
        if unit_id in visited:
            return
        visited.add(unit_id)
        members.add(unit_id)
        for source in sorted(reverse[unit_id]):
            collect(source, members)

    for unit_id in reversed(order):
        members: set[str] = set()
        collect(unit_id, members)
        if len(members) > 1:
            components.append(frozenset(members))
    return tuple(components)


def validate_composed_execution_case(
    explanation: Mapping[str, Any],
    typed_ir: Mapping[str, Any],
    measurement: Mapping[str, Any],
) -> tuple[str, ...]:
    """Independently derive every mechanism promised by M10-DEMO-01."""

    errors: list[str] = []
    units = _records(explanation.get("units"), "M10-DEMO-01 units", errors)
    relations = _records(
        explanation.get("relations"), "M10-DEMO-01 relations", errors
    )
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    if len(unit_by_id) != len(units) or "" in unit_by_id:
        errors.append("M10-DEMO-01 unit ids are empty or duplicated")
        return tuple(errors)
    criterion_id = str(explanation.get("criterion_unit_id", ""))
    criterion = unit_by_id.get(criterion_id)
    if criterion is None or criterion.get("title") != "Set ComposedExecutionComplete":
        errors.append("M10-DEMO-01 criterion binding is wrong")

    value_relations = [
        relation for relation in relations if relation.get("kind") == "provides_value"
    ]
    cone = {criterion_id}
    pending = [criterion_id]
    cone_relations: list[Mapping[str, Any]] = []
    while pending:
        consumer = pending.pop(0)
        for relation in value_relations:
            if relation.get("target_unit_id") != consumer:
                continue
            source = str(relation.get("source_unit_id", ""))
            cone_relations.append(relation)
            if source not in cone:
                cone.add(source)
                pending.append(source)
    if len(cone) != 4 or len(cone_relations) != 3:
        errors.append(
            "M10-DEMO-01 LC3 cone is "
            f"{len(cone)} units/{len(cone_relations)} relations, expected 4/3"
        )

    predicate_relations = [
        relation
        for relation in relations
        if relation.get("kind") == "predicate_for"
        and unit_by_id.get(str(relation.get("source_unit_id", "")), {}).get("role")
        == "predicate"
    ]
    predicates_by_port = {
        str(relation.get("port_label", "")): relation
        for relation in predicate_relations
    }
    if set(predicates_by_port) != {"OuterEnabled", "InnerEnabled"}:
        errors.append("M10-DEMO-01 LC2 guard predicates are not exactly outer and inner")
    else:
        outer_id = str(predicates_by_port["OuterEnabled"].get("target_unit_id", ""))
        inner_id = str(predicates_by_port["InnerEnabled"].get("target_unit_id", ""))
        exits = {
            (str(relation.get("source_unit_id", "")), relation.get("semantic_label")):
            relation
            for relation in relations
            if relation.get("kind") == "controls_execution"
        }
        outer_true = exits.get((outer_id, "condition_true"))
        outer_false = exits.get((outer_id, "condition_false"))
        inner_true = exits.get((inner_id, "condition_true"))
        inner_false = exits.get((inner_id, "condition_false"))
        if outer_true is None or outer_true.get("target_unit_id") != inner_id:
            errors.append("M10-DEMO-01 LC2 inner guard is not entered by outer true")
        outcome_relations = [outer_false, inner_false, inner_true]
        if any(relation is None for relation in outcome_relations):
            errors.append("M10-DEMO-01 LC2 guard exits are incomplete")
        else:
            outcome_ids = {
                str(relation.get("target_unit_id", ""))
                for relation in outcome_relations
                if relation is not None
            }
            outcome_titles = {
                str(unit_by_id.get(unit_id, {}).get("title", ""))
                for unit_id in outcome_ids
            }
            if outcome_titles != {
                "Set ComposedAccepted",
                "Set ComposedInnerRejected",
                "Set ComposedOuterRejected",
            }:
                errors.append("M10-DEMO-01 LC2 does not expose the three outcomes")
            for outcome_id in outcome_ids:
                reconvergences = [
                    relation
                    for relation in relations
                    if relation.get("kind") == "execution_predecessor"
                    and relation.get("source_unit_id") == outcome_id
                    and relation.get("target_unit_id") == criterion_id
                ]
                if len(reconvergences) != 1:
                    errors.append(
                        "M10-DEMO-01 LC2 outcome does not directly reconverge once"
                    )
            entries = [
                relation
                for relation in relations
                if relation.get("kind") == "execution_predecessor"
                and relation.get("target_unit_id") == outer_id
            ]
            if len(entries) != 1:
                errors.append("M10-DEMO-01 LC2 outer guard lacks a unique entry")
            else:
                core_units = {
                    criterion_id,
                    outer_id,
                    inner_id,
                    str(predicates_by_port["OuterEnabled"].get("source_unit_id", "")),
                    str(predicates_by_port["InnerEnabled"].get("source_unit_id", "")),
                    str(entries[0].get("source_unit_id", "")),
                    *outcome_ids,
                }
                if len(core_units) != 9:
                    errors.append("M10-DEMO-01 LC2 reconstructed core is not 9 units")

    statuses = {
        (str(unit.get("title", "")), str(unit.get("semantic_status", "")))
        for unit in units
        if unit.get("role") == "boundary"
    }
    if ("DemoImpureBody", "opaque") not in statuses:
        errors.append("M10-DEMO-01 lacks the genuine opaque LC5 boundary")
    if ("Delay", "unsupported") not in statuses:
        errors.append("M10-DEMO-01 lacks the genuine unsupported LC6 boundary")

    components = _strong_components(unit_by_id, relations)
    if len(components) != 1:
        errors.append(f"M10-DEMO-01 has {len(components)} non-trivial execution SCCs")
    else:
        component = components[0]
        incoming = [
            relation
            for relation in relations
            if relation.get("kind") in {"execution_predecessor", "controls_execution"}
            and relation.get("source_unit_id") not in component
            and relation.get("target_unit_id") in component
        ]
        outgoing = [
            relation
            for relation in relations
            if relation.get("kind") in {"execution_predecessor", "controls_execution"}
            and relation.get("source_unit_id") in component
            and relation.get("target_unit_id") not in component
        ]
        if not incoming or not outgoing:
            errors.append("M10-DEMO-01 SCC lacks a retained entry or exit")

    blueprint = typed_ir.get("blueprint")
    graphs = (
        _records(blueprint.get("graphs"), "demo typed-IR graphs", errors)
        if isinstance(blueprint, dict)
        else []
    )
    source = explanation.get("source")
    graph_id = source.get("graph_id") if isinstance(source, dict) else None
    event_graph = next((graph for graph in graphs if graph.get("id") == graph_id), None)
    if event_graph is None:
        errors.append("M10-DEMO-01 typed IR lacks the live source graph")
    else:
        graph_nodes = _records(event_graph.get("nodes"), "demo EventGraph nodes", errors)
        graph_edges = _records(event_graph.get("edges"), "demo EventGraph edges", errors)
        sequences = [node for node in graph_nodes if node.get("title") == "Sequence"]
        if len(sequences) != 1:
            errors.append("M10-DEMO-01 must contain exactly one Sequence node")
        else:
            sequence = sequences[0]
            output_pins = {
                str(pin.get("id", "")): str(pin.get("name", ""))
                for pin in _records(sequence.get("pins"), "Sequence pins", errors)
                if pin.get("direction") == "output" and pin.get("kind") == "execution"
            }
            connected = {
                output_pins[str(edge.get("source_pin_id", ""))]
                for edge in graph_edges
                if str(edge.get("source_pin_id", "")) in output_pins
            }
            sequence_unit_ids = {
                unit_id
                for unit_id, unit in unit_by_id.items()
                if unit.get("title") == "Sequence"
            }
            included = {
                str(relation.get("port_label", ""))
                for relation in relations
                if relation.get("kind") == "execution_predecessor"
                and relation.get("source_unit_id") in sequence_unit_ids
            }
            declared = set(output_pins.values())
            connected_outside = connected - included
            unconnected = declared - connected
            if not included or not connected_outside or not unconnected:
                errors.append(
                    "M10-DEMO-01 LC4-SEQ lacks an included, connected-outside, "
                    "or unconnected sibling"
                )
            if len(declared) > 4:
                errors.append("M10-DEMO-01 LC4-SEQ declares more than four outputs")

        composed_impure_units = [
            unit
            for unit in units
            if unit.get("title") == "DemoImpureBody" and unit.get("role") == "boundary"
        ]
        composed_impure_source_ids = {
            str(reference.get("source_node_id", ""))
            for unit in composed_impure_units
            for reference in _records(
                unit.get("source_references"),
                "M10-DEMO-01 impure-call source references",
                errors,
            )
            if reference.get("primary") is True
        }
        impure_calls = [
            node
            for node in graph_nodes
            if node.get("id") in composed_impure_source_ids
            and node.get("title") == "DemoImpureBody"
            and isinstance(node.get("symbol"), dict)
            and node["symbol"].get("is_self_context") is True
            and node["symbol"].get("is_pure") is False
        ]
        if len(impure_calls) != 1:
            errors.append("M10-DEMO-01 lacks one impure self-context body call")
    impure_graph = next(
        (graph for graph in graphs if str(graph.get("id", "")).endswith(":DemoImpureBody")),
        None,
    )
    if impure_graph is None:
        errors.append("M10-DEMO-01 typed IR lacks the exported impure body")
    else:
        body_nodes = _records(impure_graph.get("nodes"), "DemoImpureBody nodes", errors)
        body_edges = _records(impure_graph.get("edges"), "DemoImpureBody edges", errors)
        if len(body_nodes) != 16 or len(body_edges) != 15:
            errors.append(
                "M10-DEMO-01 impure body is "
                f"{len(body_nodes)} nodes/{len(body_edges)} relations, expected 16/15"
            )

    if measurement.get("station_count") != len(units):
        errors.append("M10-DEMO-01 measured station count is stale")
    if measurement.get("relation_count") != len(relations):
        errors.append("M10-DEMO-01 measured relation count is stale")
    if measurement.get("radius_limit") != 13 or measurement.get("max_hop", 14) > 13:
        errors.append("M10-DEMO-01 does not satisfy radius 13")
    if measurement.get("request_budget") != 24 or len(units) > 24:
        errors.append("M10-DEMO-01 does not satisfy budget 24")
    return tuple(errors)


def validate_data_with_producers_case(
    explanation: Mapping[str, Any], measurement: Mapping[str, Any]
) -> tuple[str, ...]:
    """Require five complete, locally disclosed data producers for M10-DEMO-02."""

    errors: list[str] = []
    units = _records(explanation.get("units"), "M10-DEMO-02 units", errors)
    relations = _records(
        explanation.get("relations"), "M10-DEMO-02 relations", errors
    )
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    criterion_id = str(explanation.get("criterion_unit_id", ""))
    criterion = unit_by_id.get(criterion_id)
    if criterion is None or criterion.get("title") != "Set DataWithProducersAnswer":
        errors.append("M10-DEMO-02 criterion binding is wrong")

    declared_producers = {
        unit_id
        for unit_id, unit in unit_by_id.items()
        if "required_data_producer" in unit.get("inclusion_reasons", [])
    }
    value_relations = [
        relation for relation in relations if relation.get("kind") == "provides_value"
    ]
    reached = {criterion_id}
    pending = [criterion_id]
    while pending:
        consumer = pending.pop(0)
        for relation in value_relations:
            if relation.get("target_unit_id") != consumer:
                continue
            source = str(relation.get("source_unit_id", ""))
            if source not in reached:
                reached.add(source)
                pending.append(source)
    reached_producers = reached - {criterion_id}
    if len(declared_producers) != 5:
        errors.append(
            "M10-DEMO-02 declares "
            f"{len(declared_producers)} required producers, expected 5"
        )
    if reached_producers != declared_producers:
        errors.append("M10-DEMO-02 producer disclosure omits or invents a source")
    if len(value_relations) != 5:
        errors.append(
            f"M10-DEMO-02 has {len(value_relations)} value relations, expected 5"
        )
    if measurement.get("station_count") != len(units):
        errors.append("M10-DEMO-02 measured station count is stale")
    if measurement.get("max_hop", 14) > 13 or len(units) > 24:
        errors.append("M10-DEMO-02 exceeds the radius or request budget")
    return tuple(errors)


def validate_data_without_producer_case(
    explanation: Mapping[str, Any], measurement: Mapping[str, Any]
) -> tuple[str, ...]:
    """Require the explicit no-separate-source data state for M10-DEMO-03."""

    errors: list[str] = []
    units = _records(explanation.get("units"), "M10-DEMO-03 units", errors)
    relations = _records(
        explanation.get("relations"), "M10-DEMO-03 relations", errors
    )
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    criterion = unit_by_id.get(str(explanation.get("criterion_unit_id", "")))
    if criterion is None or criterion.get("title") != "Set DataWithoutProducerAnswer":
        errors.append("M10-DEMO-03 criterion binding is wrong")
    producers = [
        unit
        for unit in units
        if "required_data_producer" in unit.get("inclusion_reasons", [])
    ]
    value_relations = [
        relation for relation in relations if relation.get("kind") == "provides_value"
    ]
    if producers or value_relations:
        errors.append("M10-DEMO-03 invents a separate data producer")
    if len(units) != 2 or len(relations) != 1:
        errors.append(
            "M10-DEMO-03 is not the measured two-unit/one-relation no-source case"
        )
    if measurement.get("station_count") != 2 or measurement.get("relation_count") != 1:
        errors.append("M10-DEMO-03 measurements are stale")
    return tuple(errors)


def validate_data_with_multiple_sets_case(
    explanation: Mapping[str, Any], measurement: Mapping[str, Any]
) -> tuple[str, ...]:
    """Require two source-owned writes and their real relations for DEMO-10."""

    errors: list[str] = []
    units = _records(explanation.get("units"), "M10-DEMO-10 units", errors)
    relations = _records(
        explanation.get("relations"), "M10-DEMO-10 relations", errors
    )
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    writes = [
        unit
        for unit in units
        if unit.get("role") == "criterion"
        and "member_set" in unit.get("inclusion_reasons", [])
    ]
    write_ids = {str(unit.get("id", "")) for unit in writes}
    if len(writes) != 2 or any(
        unit.get("title") != "Set DataWithMultipleSetsAnswer" for unit in writes
    ):
        errors.append("M10-DEMO-10 does not retain exactly two member Set writes")
    anchor_id = str(explanation.get("criterion_unit_id", ""))
    if anchor_id not in write_ids:
        errors.append("M10-DEMO-10 render anchor is not one of the member writes")
    source_node_ids = {
        str(reference.get("source_node_id", ""))
        for unit in writes
        for reference in _records(
            unit.get("source_references"),
            "M10-DEMO-10 write source references",
            errors,
        )
        if reference.get("primary") is True
    }
    if len(source_node_ids) != 2:
        errors.append("M10-DEMO-10 does not preserve two primary Set source identities")
    measured_source_ids = measurement.get("criterion_node_ids")
    if not isinstance(measured_source_ids, list) or set(measured_source_ids) != source_node_ids:
        errors.append("M10-DEMO-10 measured Set source identities are stale")
    write_control_relations = [
        relation
        for relation in relations
        if relation.get("kind") in {
            "execution_predecessor",
            "controls_execution",
        }
        and relation.get("target_unit_id") in write_ids
    ]
    if not write_control_relations or not all(
        relation.get("source_edge_ids") for relation in write_control_relations
    ):
        errors.append("M10-DEMO-10 lacks source-bound control for its writes")
    value_relations = [
        relation
        for relation in relations
        if relation.get("kind") == "provides_value"
        and relation.get("target_unit_id") in write_ids
    ]
    if not value_relations or not all(
        relation.get("source_edge_ids") for relation in value_relations
    ):
        errors.append("M10-DEMO-10 lacks a source-bound value producer")
    if measurement.get("write_count") != 2:
        errors.append("M10-DEMO-10 measured write count is not two")
    if measurement.get("station_count") != len(units):
        errors.append("M10-DEMO-10 measured station count is stale")
    if measurement.get("relation_count") != len(relations):
        errors.append("M10-DEMO-10 measured relation count is stale")
    if measurement.get("max_hop", 14) > 13 or len(units) > 24:
        errors.append("M10-DEMO-10 exceeds the radius or request budget")
    return tuple(errors)


def validate_lc5_case(
    explanation: Mapping[str, Any],
    typed_ir: Mapping[str, Any],
    measurement: Mapping[str, Any],
    *,
    call_title: str,
    criterion_title: str,
    expected_pure: bool,
    expected_body_units: int | None,
) -> tuple[str, ...]:
    """Derive one LC5 live claim state from Explanation plus the loaded sidecar."""

    errors: list[str] = []
    units = _records(explanation.get("units"), f"{call_title} units", errors)
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    criterion = unit_by_id.get(str(explanation.get("criterion_unit_id", "")))
    if criterion is None or criterion.get("title") != criterion_title:
        errors.append(f"{call_title} criterion binding is wrong")
    call_units = [unit for unit in units if unit.get("title") == call_title]
    if len(call_units) != 1:
        errors.append(f"{call_title} is not one live Explanation call unit")
        return tuple(errors)
    call_unit = call_units[0]
    source_references = _records(
        call_unit.get("source_references"), f"{call_title} source references", errors
    )
    primary = next(
        (reference for reference in source_references if reference.get("primary") is True),
        source_references[0] if source_references else None,
    )
    source_node_id = str(primary.get("source_node_id", "")) if primary else ""

    blueprint = typed_ir.get("blueprint")
    graphs = (
        _records(blueprint.get("graphs"), "demo typed-IR graphs", errors)
        if isinstance(blueprint, dict)
        else []
    )
    nodes = [
        node
        for graph in graphs
        for node in _records(graph.get("nodes"), "demo typed-IR graph nodes", errors)
    ]
    matching_nodes = [node for node in nodes if node.get("id") == source_node_id]
    if len(matching_nodes) != 1:
        errors.append(f"{call_title} typed-IR source node is absent or duplicated")
        return tuple(errors)
    call_node = matching_nodes[0]
    symbol = call_node.get("symbol")
    if (
        call_node.get("class") != "/Script/BlueprintGraph.K2Node_CallFunction"
        or not isinstance(symbol, dict)
        or symbol.get("is_self_context") is not True
        or symbol.get("is_latent") is not False
        or symbol.get("is_pure") is not expected_pure
    ):
        errors.append(f"{call_title} typed-IR call state is wrong")
        return tuple(errors)
    callee_name = str(symbol.get("name", ""))
    callee_graphs = [
        graph
        for graph in graphs
        if str(graph.get("id", "")).rsplit(":", 1)[-1] == callee_name
    ]
    if expected_body_units is None:
        if callee_graphs:
            errors.append(f"{call_title} unexpectedly resolves an exported body")
    elif len(callee_graphs) != 1:
        errors.append(f"{call_title} exported body is absent or ambiguous")
    else:
        body_nodes = _records(
            callee_graphs[0].get("nodes"), f"{call_title} body nodes", errors
        )
        body_edges = _records(
            callee_graphs[0].get("edges"), f"{call_title} body edges", errors
        )
        if len(body_nodes) != expected_body_units:
            errors.append(
                f"{call_title} body has {len(body_nodes)} units, "
                f"expected {expected_body_units}"
            )
        if expected_body_units > 16:
            errors.append(f"{call_title} body exceeds the live upper bound of 16")
        if expected_body_units in {3, 16} and len(body_edges) != expected_body_units - 1:
            errors.append(f"{call_title} body relation ledger is incomplete")
    if measurement.get("station_count") != len(units):
        errors.append(f"{call_title} measured station count is stale")
    if measurement.get("max_hop", 14) > 13 or len(units) > 24:
        errors.append(f"{call_title} exceeds the radius or request budget")
    return tuple(errors)


def validate_lc6_uncertain_case(
    explanation: Mapping[str, Any], measurement: Mapping[str, Any]
) -> tuple[str, ...]:
    """Require a genuine Select-owned uncertain boundary for M10-DEMO-07."""

    errors: list[str] = []
    units = _records(explanation.get("units"), "M10-DEMO-07 units", errors)
    relations = _records(
        explanation.get("relations"), "M10-DEMO-07 relations", errors
    )
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    criterion_id = str(explanation.get("criterion_unit_id", ""))
    criterion = unit_by_id.get(criterion_id)
    if criterion is None or criterion.get("title") != "Set BoundaryComplete":
        errors.append("M10-DEMO-07 criterion binding is wrong")

    boundaries = [unit for unit in units if unit.get("role") == "boundary"]
    if len(boundaries) != 1:
        errors.append(
            f"M10-DEMO-07 has {len(boundaries)} boundary units, expected one"
        )
    else:
        boundary = boundaries[0]
        if (
            boundary.get("title") != "Select"
            or boundary.get("semantic_status") != "uncertain"
            or "uncertain_boundary" not in boundary.get("inclusion_reasons", [])
        ):
            errors.append("M10-DEMO-07 is not a genuine uncertain Select boundary")
        value_edges = [
            relation
            for relation in relations
            if relation.get("kind") == "provides_value"
            and relation.get("source_unit_id") == boundary.get("id")
            and relation.get("target_unit_id") == criterion_id
        ]
        if len(value_edges) != 1:
            errors.append(
                "M10-DEMO-07 uncertain Select does not feed the criterion once"
            )

    if measurement.get("station_count") != len(units):
        errors.append("M10-DEMO-07 measured station count is stale")
    if measurement.get("relation_count") != len(relations):
        errors.append("M10-DEMO-07 measured relation count is stale")
    if measurement.get("boundary_count") != 1:
        errors.append("M10-DEMO-07 measured boundary count is not one")
    if measurement.get("max_hop", 14) > 13 or len(units) > 24:
        errors.append("M10-DEMO-07 exceeds the radius or request budget")
    return tuple(errors)


def validate_lc7_case(
    explanation: Mapping[str, Any],
    typed_ir: Mapping[str, Any],
    measurement: Mapping[str, Any],
    *,
    case_id: str,
    criterion_title: str,
    exit_outside_slice: bool,
) -> tuple[str, ...]:
    """Derive one live static-slice SCC state over LC7's declared relation family."""

    errors: list[str] = []
    units = _records(explanation.get("units"), f"{case_id} units", errors)
    relations = _records(explanation.get("relations"), f"{case_id} relations", errors)
    unit_by_id = {str(unit.get("id", "")): unit for unit in units}
    criterion_id = str(explanation.get("criterion_unit_id", ""))
    criterion = unit_by_id.get(criterion_id)
    if criterion is None or criterion.get("title") != criterion_title:
        errors.append(f"{case_id} criterion binding is wrong")

    components = _strong_components(unit_by_id, relations)
    if len(components) != 1:
        errors.append(f"{case_id} has {len(components)} non-trivial execution SCCs")
        return tuple(errors)
    component = components[0]
    family = {"execution_predecessor", "controls_execution"}
    incoming = [
        relation
        for relation in relations
        if relation.get("kind") in family
        and relation.get("source_unit_id") not in component
        and relation.get("target_unit_id") in component
    ]
    outgoing = [
        relation
        for relation in relations
        if relation.get("kind") in family
        and relation.get("source_unit_id") in component
        and relation.get("target_unit_id") not in component
    ]
    if len(incoming) != 1:
        errors.append(f"{case_id} SCC has {len(incoming)} retained entries, expected one")

    if exit_outside_slice:
        if outgoing:
            errors.append(f"{case_id} invents an SCC exit inside the static slice")
        if criterion_id not in component:
            errors.append(f"{case_id} criterion is not part of the exit-outside SCC")
    else:
        retained_to_criterion = [
            relation
            for relation in outgoing
            if relation.get("target_unit_id") == criterion_id
        ]
        if len(outgoing) != 1 or len(retained_to_criterion) != 1:
            errors.append(f"{case_id} lacks one retained SCC exit to the criterion")
        if criterion_id in component:
            errors.append(f"{case_id} criterion is incorrectly inside the exit-present SCC")

    if exit_outside_slice:
        source_node_ids_by_unit: dict[str, set[str]] = {}
        for unit_id, unit in unit_by_id.items():
            source_node_ids_by_unit[unit_id] = {
                str(reference.get("source_node_id", ""))
                for reference in _records(
                    unit.get("source_references"), f"{case_id} source references", errors
                )
                if reference.get("primary") is True
            }
        component_node_ids = set().union(
            *(source_node_ids_by_unit[unit_id] for unit_id in component)
        )
        included_node_ids = set().union(*source_node_ids_by_unit.values())
        source = explanation.get("source")
        graph_id = source.get("graph_id") if isinstance(source, dict) else None
        blueprint = typed_ir.get("blueprint")
        graphs = (
            _records(blueprint.get("graphs"), "demo typed-IR graphs", errors)
            if isinstance(blueprint, dict)
            else []
        )
        graph = next((value for value in graphs if value.get("id") == graph_id), None)
        if graph is None:
            errors.append(f"{case_id} typed IR lacks the live source graph")
        else:
            graph_edges = _records(
                graph.get("edges"), f"{case_id} typed-IR graph edges", errors
            )
            outside_edges = [
                edge
                for edge in graph_edges
                if edge.get("kind") == "execution"
                and edge.get("source_node_id") in component_node_ids
                and edge.get("target_node_id") not in included_node_ids
            ]
            if not outside_edges:
                errors.append(
                    f"{case_id} has no typed-IR exit beyond the selected static slice"
                )

    if measurement.get("station_count") != len(units):
        errors.append(f"{case_id} measured station count is stale")
    if measurement.get("relation_count") != len(relations):
        errors.append(f"{case_id} measured relation count is stale")
    if measurement.get("max_hop", 14) > 13 or len(units) > 24:
        errors.append(f"{case_id} exceeds the radius or request budget")
    return tuple(errors)


def _load_json(path: Path, purpose: str, errors: list[str]) -> Mapping[str, Any] | None:
    if not path.is_file():
        errors.append(f"{purpose} is absent: {path.relative_to(ROOT).as_posix()}")
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        errors.append(f"{purpose} is unreadable: {error}")
        return None
    if not isinstance(value, dict):
        errors.append(f"{purpose} root is not an object")
        return None
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def _validate_evidence_hash(
    entry: Mapping[str, Any],
    *,
    purpose: str,
    root: Path,
    errors: list[str],
) -> None:
    path_value = entry.get("path")
    expected = entry.get("sha256")
    if not isinstance(path_value, str) or not path_value:
        errors.append(f"{purpose} path is absent")
        return
    if not isinstance(expected, str) or len(expected) != 64:
        errors.append(f"{purpose} SHA-256 is absent or malformed")
        return
    root_resolved = root.resolve()
    candidate = (root / path_value).resolve()
    try:
        candidate.relative_to(root_resolved)
    except ValueError:
        errors.append(f"{purpose} path escapes the repository: {path_value}")
        return
    if not candidate.is_file():
        errors.append(f"{purpose} file is absent: {path_value}")
        return
    if _sha256(candidate) != expected.upper():
        errors.append(f"{purpose} SHA-256 mismatch: {path_value}")


def validate_visual_evidence_manifest(
    document: Mapping[str, Any], *, root: Path = ROOT
) -> tuple[str, ...]:
    """Bind retained visual files and the bounded post-repair visible verdict."""

    errors: list[str] = []
    if document.get("schema_name") != "blueprint-lens-m10-demo-visual-manifest":
        errors.append("M10 visual manifest schema_name is wrong")

    frames = _records(document.get("frames"), "M10 visual frames", errors)
    for index, frame in enumerate(frames):
        for kind in ("png", "tree"):
            _validate_evidence_hash(
                {
                    "path": frame.get(f"{kind}_path"),
                    "sha256": frame.get(f"{kind}_sha256"),
                },
                purpose=f"M10 visual frame {index} {kind}",
                root=root,
                errors=errors,
            )

    geometry = _records(
        document.get("lc5_runencounter_geometry_evidence"),
        "LC5 geometry evidence",
        errors,
    )
    for index, entry in enumerate(geometry):
        _validate_evidence_hash(
            entry,
            purpose=f"LC5 geometry evidence {index}",
            root=root,
            errors=errors,
        )
    _validate_evidence_hash(
        {
            "path": document.get("matrix_path"),
            "sha256": document.get("matrix_sha256"),
        },
        purpose="M10 visual case matrix",
        root=root,
        errors=errors,
    )

    post_repair = document.get("post_repair_verification")
    if not isinstance(post_repair, dict):
        errors.append("M10 visual manifest lacks post-repair verification")
        return tuple(errors)
    if post_repair.get("verdict") != "FIXED_TARGET_TITLE_CLIPPING_ONLY":
        errors.append("M10 post-repair verdict exceeds or omits the bounded fix")
    claim_boundary = post_repair.get("claim_boundary")
    if not isinstance(claim_boundary, str) or not claim_boundary.strip():
        errors.append("M10 post-repair claim boundary is absent")
    evidence = _records(post_repair.get("evidence"), "post-repair evidence", errors)
    roles = [str(entry.get("role", "")) for entry in evidence]
    if len(roles) != len(set(roles)):
        errors.append("M10 post-repair evidence roles are duplicated")
    missing_roles = sorted(POST_REPAIR_EVIDENCE_ROLES - set(roles))
    extra_roles = sorted(set(roles) - POST_REPAIR_EVIDENCE_ROLES)
    if missing_roles:
        errors.append("M10 post-repair evidence roles are missing: " + ", ".join(missing_roles))
    if extra_roles:
        errors.append("M10 post-repair evidence roles are unknown: " + ", ".join(extra_roles))
    for entry, role in zip(evidence, roles, strict=True):
        _validate_evidence_hash(
            entry,
            purpose=f"post-repair {role or '<unset>'}",
            root=root,
            errors=errors,
        )
    return tuple(errors)


def validate_lc5_scroll_coverage(
    document: Mapping[str, Any],
    *,
    required_node_ids: Sequence[str],
    required_relation_ids: Sequence[str],
) -> tuple[str, ...]:
    """Require a three-position 700px ledger with complete union and overlap."""

    errors: list[str] = []
    frames_value = document.get("frames")
    frames = frames_value if isinstance(frames_value, list) else []
    if document.get("viewport_width") != 700:
        errors.append("LC5 scroll coverage must be measured at viewport_width 700")
    if len(frames) < 3:
        errors.append("LC5 scroll coverage requires at least three scroll frames")

    visible_nodes: set[str] = set()
    visible_relations: set[str] = set()
    frame_nodes: list[set[str]] = []
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            errors.append(f"LC5 scroll frame {index} is not an object")
            frame_nodes.append(set())
            continue
        nodes = {str(value) for value in frame.get("visible_node_ids", [])}
        relations = {str(value) for value in frame.get("visible_relation_ids", [])}
        frame_nodes.append(nodes)
        visible_nodes.update(nodes)
        visible_relations.update(relations)

    expected_nodes = set(required_node_ids)
    expected_relations = set(required_relation_ids)
    covered_nodes = visible_nodes & expected_nodes
    covered_relations = visible_relations & expected_relations
    if covered_nodes != expected_nodes:
        errors.append(
            f"LC5 scroll union covers {len(covered_nodes)}/{len(expected_nodes)} nodes"
        )
    if covered_relations != expected_relations:
        errors.append(
            "LC5 scroll union covers "
            f"{len(covered_relations)}/{len(expected_relations)} relations"
        )

    for index in range(len(frame_nodes) - 1):
        overlap = frame_nodes[index] & frame_nodes[index + 1]
        if len(overlap) < 2:
            errors.append(
                "LC5 adjacent scroll frames "
                f"{index}/{index + 1} overlap on {len(overlap)} nodes; at least 2 required"
            )
    return tuple(errors)

def _strings(values: Iterable[Any]) -> tuple[str, ...]:
    return tuple(str(value) for value in values)


def validate_composition_demo(root: Path = ROOT) -> tuple[str, ...]:
    """Validate the case matrix against retained, current repository evidence."""

    errors: list[str] = []
    manifest_path = root / MANIFEST_PATH.relative_to(ROOT)
    schema_path = root / SCHEMA_PATH.relative_to(ROOT)
    manifest = _load_json(manifest_path, "M10 demo case matrix", errors)
    schema = _load_json(schema_path, "M10 demo case-matrix schema", errors)
    if manifest is None or schema is None:
        return tuple(errors)
    try:
        validate_instance(manifest, schema)
    except SchemaValidationError as error:
        errors.append(f"M10 demo case matrix violates its schema: {error}")
        return tuple(errors)

    visual_manifest_path = root / VISUAL_MANIFEST_PATH.relative_to(ROOT)
    visual_manifest = _load_json(
        visual_manifest_path, "M10 demo visual manifest", errors
    )
    if visual_manifest is not None:
        errors.extend(validate_visual_evidence_manifest(visual_manifest, root=root))

    asset = manifest["asset"]
    asset_path = root / asset["content_path"]
    if not asset_path.is_file():
        errors.append(f"demo Blueprint asset is absent: {asset['content_path']}")

    typed_ir_path = root / asset["typed_ir_path"]
    typed_ir = _load_json(typed_ir_path, "demo Blueprint typed IR", errors)
    measurements_path = root / MEASUREMENTS_PATH.relative_to(ROOT)
    measurements = _load_json(
        measurements_path, "demo scenario measurements", errors
    )
    measurement_rows = {
        str(row.get("case_id", "")): row
        for row in (
            _records(measurements.get("rows"), "demo measurement rows", errors)
            if measurements is not None
            else []
        )
    }
    expected_graph_ids = {case["graph_id"] for case in manifest["cases"]}
    if typed_ir is not None:
        blueprint = typed_ir.get("blueprint")
        if not isinstance(blueprint, dict) or blueprint.get("id") != asset["object_path"]:
            errors.append("demo typed IR blueprint id does not match the case matrix")
        graphs = blueprint.get("graphs", []) if isinstance(blueprint, dict) else []
        actual_graph_ids = {
            graph.get("id") for graph in graphs if isinstance(graph, dict)
        }
        missing_graphs = sorted(expected_graph_ids - actual_graph_ids)
        if missing_graphs:
            errors.append("demo typed IR lacks case graphs: " + ", ".join(missing_graphs))

    for case in manifest["cases"]:
        explanation_path = root / case["explanation_path"]
        explanation = _load_json(
            explanation_path, f"{case['case_id']} Explanation", errors
        )
        if explanation is not None:
            source = explanation.get("source")
            query = explanation.get("query")
            if not isinstance(source, dict) or source.get("graph_id") != case["graph_id"]:
                errors.append(f"{case['case_id']} Explanation source graph is stale")
            if not isinstance(query, dict) or query.get("direction") != case["query_direction"]:
                errors.append(f"{case['case_id']} Explanation query direction is wrong")
            if (
                case["case_id"] == "M10-DEMO-01"
                and typed_ir is not None
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_composed_execution_case(
                        explanation,
                        typed_ir,
                        measurement_rows[case["case_id"]],
                    )
                )
            if (
                case["case_id"] == "M10-DEMO-02"
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_data_with_producers_case(
                        explanation,
                        measurement_rows[case["case_id"]],
                    )
                )
            if (
                case["case_id"] == "M10-DEMO-03"
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_data_without_producer_case(
                        explanation,
                        measurement_rows[case["case_id"]],
                    )
                )
            if (
                case["case_id"] == "M10-DEMO-10"
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_data_with_multiple_sets_case(
                        explanation,
                        measurement_rows[case["case_id"]],
                    )
                )
            lc5_contracts = {
                "M10-DEMO-04": {
                    "call_title": "DemoPureBody",
                    "criterion_title": "Set PureBodyAnswer",
                    "expected_pure": True,
                    "expected_body_units": 3,
                },
                "M10-DEMO-05": {
                    "call_title": "DemoImpureBody",
                    "criterion_title": "Set ImpureBodyComplete",
                    "expected_pure": False,
                    "expected_body_units": 16,
                },
                "M10-DEMO-06": {
                    "call_title": "DemoBodyUnavailable",
                    "criterion_title": "Set BodyUnavailableComplete",
                    "expected_pure": False,
                    "expected_body_units": None,
                },
            }
            if (
                case["case_id"] in lc5_contracts
                and typed_ir is not None
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_lc5_case(
                        explanation,
                        typed_ir,
                        measurement_rows[case["case_id"]],
                        **lc5_contracts[case["case_id"]],
                    )
                )
            if (
                case["case_id"] == "M10-DEMO-07"
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_lc6_uncertain_case(
                        explanation,
                        measurement_rows[case["case_id"]],
                    )
                )
            lc7_contracts = {
                "M10-DEMO-08": {
                    "criterion_title": "Set ExitPresentComplete",
                    "exit_outside_slice": False,
                },
                "M10-DEMO-09": {
                    "criterion_title": "Set ExitOutsideComplete",
                    "exit_outside_slice": True,
                },
            }
            if (
                case["case_id"] in lc7_contracts
                and typed_ir is not None
                and case["case_id"] in measurement_rows
            ):
                errors.extend(
                    validate_lc7_case(
                        explanation,
                        typed_ir,
                        measurement_rows[case["case_id"]],
                        case_id=case["case_id"],
                        **lc7_contracts[case["case_id"]],
                    )
                )
        for screenshot_path in case["screenshot_paths"]:
            if not (root / screenshot_path).is_file():
                errors.append(
                    f"{case['case_id']} retained screenshot is absent: {screenshot_path}"
                )

    coverage = manifest["lc5_scroll_coverage"]
    coverage_path = root / coverage["ledger_path"]
    coverage_document = _load_json(coverage_path, "LC5 scroll coverage ledger", errors)
    if coverage_document is not None:
        errors.extend(
            validate_lc5_scroll_coverage(
                coverage_document,
                required_node_ids=_strings(coverage["required_node_ids"]),
                required_relation_ids=_strings(coverage["required_relation_ids"]),
            )
        )
        for frame in coverage_document.get("frames", []):
            if isinstance(frame, dict):
                screenshot_path = frame.get("screenshot_path")
                if not isinstance(screenshot_path, str) or not (root / screenshot_path).is_file():
                    errors.append(
                        "LC5 scroll frame screenshot is absent: "
                        f"{screenshot_path or '<unset>'}"
                    )
    return tuple(errors)
