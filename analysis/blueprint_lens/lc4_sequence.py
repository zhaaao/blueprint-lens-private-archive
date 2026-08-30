"""Build and semantically validate the LC4 source-owned Sequence profile."""

from __future__ import annotations

from collections import defaultdict
from copy import deepcopy
from dataclasses import dataclass
from typing import Any, Mapping, Sequence


PROFILE_ID = "LC4_SEQUENCE_FANOUT_TO_BOUNDARY_V1"
RULES_VERSION = "sequence_fanout_to_first_boundary_v1"
QUERY_MODE = "sequence_fanout_overview"
SEQUENCE_CLASS = "K2Node_ExecutionSequence"

ROOT_INVALID = "LC4_SEQ_ROOT_INVALID"
BINDING_MISMATCH = "LC4_SEQ_BINDING_MISMATCH"
OUTPUT_COVERAGE_MISMATCH = "LC4_SEQ_OUTPUT_COVERAGE_MISMATCH"
ORDINAL_INVALID = "LC4_SEQ_ORDINAL_INVALID"
SOURCE_PIN_MISMATCH = "LC4_SEQ_SOURCE_PIN_MISMATCH"
SOURCE_COMPILER_ORDER_MISMATCH = "LC4_SEQ_SOURCE_COMPILER_ORDER_MISMATCH"
CRITERION_MEMBERSHIP_MISMATCH = "LC4_SEQ_CRITERION_MEMBERSHIP_MISMATCH"
PATH_MEMBERSHIP_MISMATCH = "LC4_SEQ_PATH_MEMBERSHIP_MISMATCH"
RECONVERGENCE_KIND_INVALID = "LC4_SEQ_RECONVERGENCE_KIND_INVALID"
COUNT_MISMATCH = "LC4_SEQ_COUNT_MISMATCH"
CANONICAL_IDENTITY_DUPLICATED = "LC4_SEQ_CANONICAL_IDENTITY_DUPLICATED"
UNSUPPORTED_BOUNDARY_UNDECLARED = "LC4_SEQ_UNSUPPORTED_BOUNDARY_UNDECLARED"

_BOUNDARY_TERMINATIONS = {
    "opaque_boundary",
    "uncertain_boundary",
    "unsupported_boundary",
}


class LC4SequenceError(ValueError):
    """A fail-closed LC4 Sequence diagnostic with a stable code."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


def _fail(code: str, message: str) -> None:
    raise LC4SequenceError(code, message)


def _mapping(value: Any, code: str, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        _fail(code, f"{context} must be an object")
    return value


def _array(value: Any, code: str, context: str) -> list[Any]:
    if not isinstance(value, list):
        _fail(code, f"{context} must be an array")
    return value


def _string(value: Any, code: str, context: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(code, f"{context} must be a non-empty string")
    return value


def _short_class(node: Mapping[str, Any]) -> str:
    return str(node.get("class", "")).rsplit(".", 1)[-1]


def _unique(values: Sequence[str]) -> bool:
    return len(values) == len(set(values))


def _append_unique(values: list[str], value: str) -> None:
    if value not in values:
        values.append(value)


@dataclass(frozen=True, slots=True)
class CompilerAuditOutput:
    rank: int
    source_pin_id: str
    source_pin_name: str
    linked_count: int


@dataclass(frozen=True, slots=True)
class CompilerAudit:
    blueprint_asset_path: str
    graph_id: str
    sequence_node_id: str
    outputs: tuple[CompilerAuditOutput, ...]
    connected_output_count: int


def parse_compiler_audit(text: str) -> CompilerAudit:
    """Parse the independent compiler-equivalent TSV without inferring order."""

    format_rows: list[tuple[str, str]] = []
    blueprints: list[str] = []
    graphs: list[str] = []
    roots: list[str] = []
    counts: list[int] = []
    outputs: list[CompilerAuditOutput] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        fields = line.split("\t")
        record = fields[0] if fields else ""
        try:
            if record == "FORMAT" and len(fields) == 3:
                format_rows.append((fields[1], fields[2]))
            elif record == "BLUEPRINT" and len(fields) == 2:
                blueprints.append(fields[1])
            elif record == "GRAPH" and len(fields) == 2:
                graphs.append(fields[1])
            elif record == "SEQUENCE" and len(fields) == 2:
                roots.append(fields[1])
            elif record == "OUTPUT" and len(fields) == 5:
                outputs.append(
                    CompilerAuditOutput(
                        rank=int(fields[1]),
                        source_pin_id=fields[2],
                        source_pin_name=fields[3],
                        linked_count=int(fields[4]),
                    )
                )
            elif record == "COUNTS" and len(fields) == 2:
                counts.append(int(fields[1]))
            else:
                raise ValueError(f"unexpected {record!r} record shape")
        except ValueError as error:
            _fail(
                SOURCE_COMPILER_ORDER_MISMATCH,
                f"malformed compiler audit row {line_number}: {error}",
            )
    if format_rows != [("blueprint-lens-sequence-compiler-order", "1.0.0")]:
        _fail(SOURCE_COMPILER_ORDER_MISMATCH, "compiler audit format is not v1")
    if len(blueprints) != 1 or len(graphs) != 1 or len(roots) != 1:
        _fail(BINDING_MISMATCH, "compiler audit must bind one asset, graph and root")
    if len(counts) != 1 or counts[0] != len(outputs):
        _fail(COUNT_MISMATCH, "compiler audit count does not match OUTPUT rows")
    if [item.rank for item in outputs] != list(range(len(outputs))):
        _fail(SOURCE_COMPILER_ORDER_MISMATCH, "compiler ranks are not contiguous")
    if not _unique([item.source_pin_id for item in outputs]):
        _fail(SOURCE_COMPILER_ORDER_MISMATCH, "compiler output pin is duplicated")
    return CompilerAudit(
        blueprint_asset_path=blueprints[0],
        graph_id=graphs[0],
        sequence_node_id=roots[0],
        outputs=tuple(outputs),
        connected_output_count=counts[0],
    )


def _graph_and_root(
    ir: Mapping[str, Any], sequence_node_id: str
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    blueprint = _mapping(ir.get("blueprint"), BINDING_MISMATCH, "IR blueprint")
    graphs = _array(blueprint.get("graphs"), BINDING_MISMATCH, "IR graphs")
    root_matches: list[tuple[Mapping[str, Any], Mapping[str, Any]]] = []
    for graph_value in graphs:
        graph = _mapping(graph_value, BINDING_MISMATCH, "IR graph")
        for node_value in _array(graph.get("nodes"), BINDING_MISMATCH, "IR nodes"):
            node = _mapping(node_value, BINDING_MISMATCH, "IR node")
            if node.get("id") == sequence_node_id:
                root_matches.append((graph, node))
    if len(root_matches) != 1:
        _fail(
            ROOT_INVALID,
            f"Sequence root must resolve exactly once; found {len(root_matches)}",
        )
    graph, root = root_matches[0]
    if _short_class(root) != SEQUENCE_CLASS:
        _fail(ROOT_INVALID, f"root is not {SEQUENCE_CLASS}: {root.get('class')!r}")
    return graph, root


def _validate_source_facts(
    ir: Mapping[str, Any],
    sequence_source: Mapping[str, Any],
    compiler_audit: CompilerAudit,
) -> tuple[
    Mapping[str, Any],
    Mapping[str, Any],
    list[Mapping[str, Any]],
]:
    if sequence_source.get("format") != "blueprint-lens-sequence-source" or sequence_source.get(
        "format_version"
    ) != "1.0.0":
        _fail(BINDING_MISMATCH, "Sequence source facts are not v1")
    sequence_node_id = _string(
        sequence_source.get("sequence_node_id"), BINDING_MISMATCH, "sequence_node_id"
    )
    graph, root = _graph_and_root(ir, sequence_node_id)
    blueprint = _mapping(ir.get("blueprint"), BINDING_MISMATCH, "IR blueprint")
    expected_binding = (
        str(blueprint.get("path", "")),
        str(graph.get("id", "")),
        sequence_node_id,
    )
    source_binding = (
        sequence_source.get("blueprint_asset_path"),
        sequence_source.get("graph_id"),
        sequence_node_id,
    )
    audit_binding = (
        compiler_audit.blueprint_asset_path,
        compiler_audit.graph_id,
        compiler_audit.sequence_node_id,
    )
    if source_binding != expected_binding or audit_binding != expected_binding:
        _fail(BINDING_MISMATCH, "IR, source facts and compiler audit bindings differ")

    outputs_value = _array(
        sequence_source.get("outputs"), OUTPUT_COVERAGE_MISMATCH, "Sequence outputs"
    )
    outputs = [
        _mapping(item, OUTPUT_COVERAGE_MISMATCH, "Sequence output")
        for item in outputs_value
    ]
    counts = _mapping(sequence_source.get("counts"), COUNT_MISMATCH, "source counts")
    declared_count = counts.get("declared_output_count")
    if not isinstance(declared_count, int) or isinstance(declared_count, bool):
        _fail(COUNT_MISMATCH, "declared output count must be an integer")
    if declared_count != len(outputs):
        _fail(OUTPUT_COVERAGE_MISMATCH, "declared output count differs from records")

    ordinal_values = [item.get("ordinal") for item in outputs]
    if any(not isinstance(value, int) or isinstance(value, bool) for value in ordinal_values):
        _fail(ORDINAL_INVALID, "every ordinal must be an integer")
    if sorted(ordinal_values) != list(range(len(outputs))):
        _fail(ORDINAL_INVALID, "ordinals must be unique and contiguous from zero")
    outputs.sort(key=lambda item: int(item["ordinal"]))

    root_pins = [
        _mapping(pin, SOURCE_PIN_MISMATCH, "root pin")
        for pin in _array(root.get("pins"), SOURCE_PIN_MISMATCH, "root pins")
        if isinstance(pin, Mapping)
        and pin.get("kind") == "execution"
        and pin.get("direction") == "output"
    ]
    root_pin_by_id = {str(pin.get("id", "")): pin for pin in root_pins}
    source_pin_ids = [str(item.get("source_pin_id", "")) for item in outputs]
    if not _unique(source_pin_ids):
        _fail(OUTPUT_COVERAGE_MISMATCH, "source output pin is repeated")
    if set(source_pin_ids) != set(root_pin_by_id):
        _fail(OUTPUT_COVERAGE_MISMATCH, "source output coverage differs from root exec pins")

    graph_edges = [
        _mapping(edge, SOURCE_PIN_MISMATCH, "graph edge")
        for edge in _array(graph.get("edges"), SOURCE_PIN_MISMATCH, "graph edges")
    ]
    edge_ids_by_pin: dict[str, list[str]] = defaultdict(list)
    for edge in graph_edges:
        if edge.get("kind") == "execution":
            edge_ids_by_pin[str(edge.get("source_pin_id", ""))].append(
                str(edge.get("id", ""))
            )
    for edge_ids in edge_ids_by_pin.values():
        edge_ids.sort()

    connected_records: list[tuple[str, str, int]] = []
    connected_count = 0
    for output in outputs:
        pin_id = str(output.get("source_pin_id", ""))
        pin = root_pin_by_id[pin_id]
        if output.get("source_pin_name") != pin.get("name"):
            _fail(SOURCE_PIN_MISMATCH, f"source pin name differs for {pin_id}")
        declared_edges = output.get("connected_edge_ids")
        if not isinstance(declared_edges, list) or not all(
            isinstance(edge_id, str) and edge_id for edge_id in declared_edges
        ):
            _fail(SOURCE_PIN_MISMATCH, f"connected edges are invalid for {pin_id}")
        if not _unique(declared_edges) or sorted(declared_edges) != edge_ids_by_pin.get(
            pin_id, []
        ):
            _fail(SOURCE_PIN_MISMATCH, f"connected edges differ from IR for {pin_id}")
        expected_state = "connected" if declared_edges else "unconnected"
        if output.get("connection_state") != expected_state:
            _fail(SOURCE_PIN_MISMATCH, f"connection state differs for {pin_id}")
        if declared_edges:
            connected_count += 1
            connected_records.append(
                (pin_id, str(output.get("source_pin_name", "")), len(declared_edges))
            )

    if counts.get("connected_output_count") != connected_count or counts.get(
        "unconnected_output_count"
    ) != len(outputs) - connected_count:
        _fail(COUNT_MISMATCH, "Sequence source counts do not reconcile")
    audit_records = [
        (item.source_pin_id, item.source_pin_name, item.linked_count)
        for item in compiler_audit.outputs
    ]
    if connected_records != audit_records:
        _fail(
            SOURCE_COMPILER_ORDER_MISMATCH,
            "connected API ordinal projection differs from compiler-equivalent order",
        )
    return graph, root, outputs


def _walk_output(
    root_id: str,
    connected_edge_ids: Sequence[str],
    criterion_node_id: str,
    nodes_by_id: Mapping[str, Mapping[str, Any]],
    edges_by_id: Mapping[str, Mapping[str, Any]],
    outgoing: Mapping[str, tuple[Mapping[str, Any], ...]],
    incoming: Mapping[str, tuple[Mapping[str, Any], ...]],
) -> tuple[list[str], list[str], dict[str, str]]:
    if not connected_edge_ids:
        return [], [], {"kind": "unconnected", "node_id": root_id}
    if len(connected_edge_ids) > 1:
        reachable_nodes: list[str] = []
        for edge_id in connected_edge_ids:
            edge = edges_by_id[edge_id]
            _append_unique(reachable_nodes, str(edge.get("target_node_id", "")))
        return (
            reachable_nodes,
            list(connected_edge_ids),
            {"kind": "nested_fanout", "node_id": root_id},
        )

    reachable_nodes = []
    reachable_edges: list[str] = []
    visited = {root_id}
    current_edge = edges_by_id[connected_edge_ids[0]]
    while True:
        edge_id = str(current_edge.get("id", ""))
        target_id = str(current_edge.get("target_node_id", ""))
        _append_unique(reachable_edges, edge_id)
        _append_unique(reachable_nodes, target_id)
        if target_id not in nodes_by_id:
            return reachable_nodes, reachable_edges, {
                "kind": "graph_boundary",
                "node_id": target_id,
            }
        if target_id == criterion_node_id:
            return reachable_nodes, reachable_edges, {
                "kind": "criterion",
                "node_id": target_id,
            }
        node = nodes_by_id[target_id]
        status = str(node.get("semantic_status", "uncertain"))
        if status in {"opaque", "uncertain", "unsupported"}:
            return reachable_nodes, reachable_edges, {
                "kind": f"{status}_boundary",
                "node_id": target_id,
            }
        if target_id in visited:
            return reachable_nodes, reachable_edges, {
                "kind": "cycle_revisit",
                "node_id": target_id,
            }
        visited.add(target_id)
        if len(incoming.get(target_id, ())) > 1:
            return reachable_nodes, reachable_edges, {
                "kind": "ordinary_reconvergence",
                "node_id": target_id,
            }
        next_edges = outgoing.get(target_id, ())
        if not next_edges:
            return reachable_nodes, reachable_edges, {
                "kind": "terminal",
                "node_id": target_id,
            }
        if len(next_edges) > 1:
            return reachable_nodes, reachable_edges, {
                "kind": "nested_fanout",
                "node_id": target_id,
            }
        current_edge = next_edges[0]


def _walk_shared_suffix(
    start_node_id: str,
    criterion_node_id: str,
    nodes_by_id: Mapping[str, Mapping[str, Any]],
    outgoing: Mapping[str, tuple[Mapping[str, Any], ...]],
    incoming: Mapping[str, tuple[Mapping[str, Any], ...]],
) -> tuple[list[str], list[str], dict[str, str]]:
    reachable_nodes = [start_node_id]
    reachable_edges: list[str] = []
    current_id = start_node_id
    visited = {start_node_id}
    while True:
        if current_id == criterion_node_id:
            return reachable_nodes, reachable_edges, {
                "kind": "criterion",
                "node_id": current_id,
            }
        node = nodes_by_id.get(current_id)
        if node is None:
            return reachable_nodes, reachable_edges, {
                "kind": "graph_boundary",
                "node_id": current_id,
            }
        status = str(node.get("semantic_status", "uncertain"))
        if status in {"opaque", "uncertain", "unsupported"}:
            return reachable_nodes, reachable_edges, {
                "kind": f"{status}_boundary",
                "node_id": current_id,
            }
        next_edges = outgoing.get(current_id, ())
        if not next_edges:
            return reachable_nodes, reachable_edges, {
                "kind": "terminal",
                "node_id": current_id,
            }
        if len(next_edges) > 1:
            return reachable_nodes, reachable_edges, {
                "kind": "nested_fanout",
                "node_id": current_id,
            }
        edge = next_edges[0]
        edge_id = str(edge.get("id", ""))
        target_id = str(edge.get("target_node_id", ""))
        _append_unique(reachable_edges, edge_id)
        _append_unique(reachable_nodes, target_id)
        if target_id in visited:
            return reachable_nodes, reachable_edges, {
                "kind": "cycle_revisit",
                "node_id": target_id,
            }
        visited.add(target_id)
        if target_id != criterion_node_id and len(incoming.get(target_id, ())) > 1:
            return reachable_nodes, reachable_edges, {
                "kind": "ordinary_reconvergence",
                "node_id": target_id,
            }
        current_id = target_id


def build_sequence_profile(
    ir: Mapping[str, Any],
    slice_value: Mapping[str, Any],
    sequence_source: Mapping[str, Any],
    compiler_audit: CompilerAudit | str,
    source_binding: Mapping[str, Any],
) -> dict[str, Any]:
    """Build a deterministic profile from independently owned source facts."""

    audit = (
        parse_compiler_audit(compiler_audit)
        if isinstance(compiler_audit, str)
        else compiler_audit
    )
    graph, root, source_outputs = _validate_source_facts(ir, sequence_source, audit)
    root_id = str(root.get("id", ""))
    graph_id = str(graph.get("id", ""))
    criterion = _mapping(
        slice_value.get("criterion"), BINDING_MISMATCH, "slice criterion"
    )
    criterion_node_id = _string(
        criterion.get("node_id"), BINDING_MISMATCH, "criterion node id"
    )
    if slice_value.get("format") != "blueprint-lens-slice" or slice_value.get(
        "slice_kind"
    ) != "execution_context":
        _fail(BINDING_MISMATCH, "LC4 Sequence requires an execution-context slice")
    if slice_value.get("graph_id") != graph_id or criterion.get("graph_id") != graph_id:
        _fail(BINDING_MISMATCH, "slice graph differs from Sequence graph")

    binding = deepcopy(dict(source_binding))
    required_binding = {
        "blueprint_asset_path": sequence_source.get("blueprint_asset_path"),
        "graph_id": graph_id,
        "sequence_node_id": root_id,
        "criterion_node_id": criterion_node_id,
    }
    if any(binding.get(key) != value for key, value in required_binding.items()):
        _fail(BINDING_MISMATCH, "profile source binding differs from frozen query")

    nodes = [
        _mapping(node, PATH_MEMBERSHIP_MISMATCH, "graph node")
        for node in _array(graph.get("nodes"), PATH_MEMBERSHIP_MISMATCH, "graph nodes")
    ]
    edges = [
        _mapping(edge, PATH_MEMBERSHIP_MISMATCH, "graph edge")
        for edge in _array(graph.get("edges"), PATH_MEMBERSHIP_MISMATCH, "graph edges")
        if isinstance(edge, Mapping) and edge.get("kind") == "execution"
    ]
    nodes_by_id = {str(node.get("id", "")): node for node in nodes}
    edges_by_id = {str(edge.get("id", "")): edge for edge in edges}
    if criterion_node_id not in nodes_by_id:
        _fail(BINDING_MISMATCH, "criterion node is absent from the Sequence graph")
    outgoing_lists: dict[str, list[Mapping[str, Any]]] = defaultdict(list)
    incoming_lists: dict[str, list[Mapping[str, Any]]] = defaultdict(list)
    for edge in edges:
        outgoing_lists[str(edge.get("source_node_id", ""))].append(edge)
        incoming_lists[str(edge.get("target_node_id", ""))].append(edge)
    outgoing = {
        node_id: tuple(sorted(items, key=lambda item: str(item.get("id", ""))))
        for node_id, items in outgoing_lists.items()
    }
    incoming = {
        node_id: tuple(sorted(items, key=lambda item: str(item.get("id", ""))))
        for node_id, items in incoming_lists.items()
    }
    selected_edges = set(
        str(edge_id)
        for edge_id in _array(
            slice_value.get("edge_ids"), BINDING_MISMATCH, "slice edge ids"
        )
    )

    outputs: list[dict[str, Any]] = []
    reconvergence_ordinals: dict[str, list[int]] = defaultdict(list)
    for source_output in source_outputs:
        connected_edge_ids = sorted(
            str(edge_id) for edge_id in source_output["connected_edge_ids"]
        )
        if any(edge_id not in edges_by_id for edge_id in connected_edge_ids):
            _fail(SOURCE_PIN_MISMATCH, "source output references a missing edge")
        reachable_nodes, reachable_edges, termination = _walk_output(
            root_id,
            connected_edge_ids,
            criterion_node_id,
            nodes_by_id,
            edges_by_id,
            outgoing,
            incoming,
        )
        if any(edge_id in selected_edges for edge_id in connected_edge_ids):
            criterion_relation = "included"
            criterion_reason = "selected_execution_edge"
        elif not connected_edge_ids:
            criterion_relation = "outside"
            criterion_reason = "unconnected_output"
        elif termination["kind"] in _BOUNDARY_TERMINATIONS:
            criterion_relation = "indeterminate"
            criterion_reason = "unsupported_or_uncertain_boundary"
        else:
            criterion_relation = "outside"
            criterion_reason = "no_selected_execution_edge"
        ordinal = int(source_output["ordinal"])
        if termination["kind"] == "ordinary_reconvergence":
            reconvergence_ordinals[termination["node_id"]].append(ordinal)
        outputs.append(
            {
                "source_pin_id": str(source_output["source_pin_id"]),
                "source_pin_name": str(source_output["source_pin_name"]),
                "ordinal": ordinal,
                "connection_state": str(source_output["connection_state"]),
                "connected_edge_ids": connected_edge_ids,
                "criterion_relation": criterion_relation,
                "criterion_reason": criterion_reason,
                "reachable_node_ids": reachable_nodes,
                "reachable_edge_ids": reachable_edges,
                "termination": termination,
            }
        )
    outputs.sort(key=lambda item: int(item["ordinal"]))

    reconvergences: list[dict[str, Any]] = []
    for node_id, ordinals in sorted(reconvergence_ordinals.items()):
        shared_nodes, shared_edges, termination = _walk_shared_suffix(
            node_id,
            criterion_node_id,
            nodes_by_id,
            outgoing,
            incoming,
        )
        reconvergences.append(
            {
                "node_id": node_id,
                "kind": "ordinary_multi_predecessor",
                "incoming_output_ordinals": sorted(ordinals),
                "shared_reachable_node_ids": shared_nodes,
                "shared_reachable_edge_ids": shared_edges,
                "termination": termination,
            }
        )

    connected = sum(item["connection_state"] == "connected" for item in outputs)
    counts = {
        "declared_output_count": len(outputs),
        "connected_output_count": connected,
        "unconnected_output_count": len(outputs) - connected,
        "criterion_included_output_count": sum(
            item["criterion_relation"] == "included" for item in outputs
        ),
        "outside_criterion_connected_output_count": sum(
            item["criterion_relation"] == "outside"
            and item["connection_state"] == "connected"
            for item in outputs
        ),
        "indeterminate_output_count": sum(
            item["criterion_relation"] == "indeterminate" for item in outputs
        ),
    }
    return {
        "format": "blueprint-lens-sequence-profile",
        "schema_version": "1.0.0",
        "profile_id": PROFILE_ID,
        "rules_version": RULES_VERSION,
        "query_mode": QUERY_MODE,
        "source": binding,
        "outputs": outputs,
        "reconvergences": reconvergences,
        "counts": counts,
    }


def validate_sequence_profile(
    profile: Mapping[str, Any],
    ir: Mapping[str, Any],
    slice_value: Mapping[str, Any],
    sequence_source: Mapping[str, Any],
    compiler_audit: CompilerAudit | str,
    source_binding: Mapping[str, Any],
) -> None:
    """Cross-check a profile against independently rederived expected facts."""

    expected = build_sequence_profile(
        ir,
        slice_value,
        sequence_source,
        compiler_audit,
        source_binding,
    )
    for field in ("format", "schema_version", "profile_id", "rules_version", "query_mode", "source"):
        if profile.get(field) != expected[field]:
            _fail(BINDING_MISMATCH, f"profile {field} differs from frozen binding")

    actual_outputs_value = _array(
        profile.get("outputs"), OUTPUT_COVERAGE_MISMATCH, "profile outputs"
    )
    if len(actual_outputs_value) != len(expected["outputs"]):
        _fail(OUTPUT_COVERAGE_MISMATCH, "profile output record count differs")
    actual_outputs = [
        _mapping(item, OUTPUT_COVERAGE_MISMATCH, "profile output")
        for item in actual_outputs_value
    ]
    actual_ordinals = [item.get("ordinal") for item in actual_outputs]
    if any(not isinstance(value, int) or isinstance(value, bool) for value in actual_ordinals):
        _fail(ORDINAL_INVALID, "profile ordinals must be integers")
    if sorted(actual_ordinals) != list(range(len(expected["outputs"]))):
        _fail(ORDINAL_INVALID, "profile ordinals are not contiguous and unique")
    actual_by_ordinal = {int(item["ordinal"]): item for item in actual_outputs}

    for expected_output in expected["outputs"]:
        ordinal = int(expected_output["ordinal"])
        actual = actual_by_ordinal[ordinal]
        for field in (
            "source_pin_id",
            "source_pin_name",
            "connection_state",
            "connected_edge_ids",
        ):
            if actual.get(field) != expected_output[field]:
                _fail(SOURCE_PIN_MISMATCH, f"profile output {ordinal}.{field} differs")
        for field in ("reachable_node_ids", "reachable_edge_ids"):
            values = actual.get(field)
            if not isinstance(values, list) or not _unique(
                [str(value) for value in values]
            ):
                _fail(
                    CANONICAL_IDENTITY_DUPLICATED,
                    f"profile output {ordinal}.{field} duplicates an identity",
                )
        expected_termination = expected_output["termination"]
        if expected_output["criterion_relation"] == "indeterminate" and (
            actual.get("criterion_relation") != "indeterminate"
            or actual.get("criterion_reason")
            != "unsupported_or_uncertain_boundary"
            or actual.get("termination") != expected_termination
        ):
            _fail(
                UNSUPPORTED_BOUNDARY_UNDECLARED,
                f"profile output {ordinal} hides an uncertain boundary",
            )
        if actual.get("criterion_relation") != expected_output["criterion_relation"] or actual.get(
            "criterion_reason"
        ) != expected_output["criterion_reason"]:
            _fail(
                CRITERION_MEMBERSHIP_MISMATCH,
                f"profile output {ordinal} criterion relation differs",
            )
        for field in ("reachable_node_ids", "reachable_edge_ids", "termination"):
            if actual.get(field) != expected_output[field]:
                _fail(PATH_MEMBERSHIP_MISMATCH, f"profile output {ordinal}.{field} differs")

    actual_reconvergences = _array(
        profile.get("reconvergences"),
        RECONVERGENCE_KIND_INVALID,
        "profile reconvergences",
    )
    for item_value in actual_reconvergences:
        item = _mapping(
            item_value, RECONVERGENCE_KIND_INVALID, "profile reconvergence"
        )
        if item.get("kind") != "ordinary_multi_predecessor":
            _fail(RECONVERGENCE_KIND_INVALID, "ordinary merge is labelled as a barrier")
        for field in ("shared_reachable_node_ids", "shared_reachable_edge_ids"):
            values = item.get(field)
            if not isinstance(values, list) or not _unique(
                [str(value) for value in values]
            ):
                _fail(
                    CANONICAL_IDENTITY_DUPLICATED,
                    f"reconvergence {item.get('node_id')}.{field} duplicates an identity",
                )
    if actual_reconvergences != expected["reconvergences"]:
        _fail(RECONVERGENCE_KIND_INVALID, "reconvergence facts differ from traversal")
    if profile.get("counts") != expected["counts"]:
        _fail(COUNT_MISMATCH, "profile counts do not reconcile")
