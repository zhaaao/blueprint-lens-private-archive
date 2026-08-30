"""Cross-file semantic validation for Blueprint Lens v1 contracts."""

from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .raw_probe import BlueprintDocument, Graph, ReconstructionError, load_blueprint_lens_v1
from .schema_validation import SchemaValidationError, validate_json_file


class ContractValidationError(ValueError):
    """Raised when schema-valid files violate a Blueprint Lens contract."""


def _load_object(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ContractValidationError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise ContractValidationError(f"contract root must be an object: {path}")
    return value


def _resolve_source(instance_path: Path, value: Mapping[str, Any]) -> tuple[Path, str]:
    source_value = value.get("source_fixture")
    if not isinstance(source_value, str) or not source_value:
        raise ContractValidationError("source_fixture must be a non-empty string")
    source_path = (instance_path.parent / source_value).resolve()
    if not source_path.is_file():
        raise ContractValidationError(f"source_fixture does not exist: {source_path}")
    actual_hash = hashlib.sha256(source_path.read_bytes()).hexdigest().upper()
    if value.get("source_sha256") != actual_hash:
        raise ContractValidationError(
            f"source_sha256 mismatch: declared {value.get('source_sha256')}, "
            f"actual {actual_hash}"
        )
    return source_path, actual_hash


def _criterion_graph(
    document: BlueprintDocument,
    value: Mapping[str, Any],
) -> tuple[Graph, Mapping[str, Any]]:
    criterion = value.get("criterion")
    if not isinstance(criterion, dict):
        raise ContractValidationError("criterion must be an object")
    graph_id = criterion.get("graph_id")
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        raise ContractValidationError(
            f"criterion graph must resolve exactly once: {graph_id!r}"
        )
    graph = matches[0]
    kind = value.get("slice_kind")
    if kind == "execution_context":
        expected_fields = {"graph_id", "node_id", "description"}
        if set(criterion) != expected_fields:
            raise ContractValidationError(
                f"execution criterion fields must be {sorted(expected_fields)}"
            )
        nodes = {node.id: node for node in graph.nodes}
        node_id = criterion.get("node_id")
        if node_id not in nodes:
            raise ContractValidationError(
                f"execution criterion node is not in graph: {node_id}"
            )
        if not any(pin.kind == "execution" for pin in nodes[str(node_id)].pins):
            raise ContractValidationError(
                f"execution criterion has no execution pin: {node_id}"
            )
    elif kind == "member_variable_data_dependency":
        expected_fields = {"graph_id", "member_guid", "member_name", "question"}
        if set(criterion) != expected_fields:
            raise ContractValidationError(
                f"member criterion fields must be {sorted(expected_fields)}"
            )
        guid = criterion.get("member_guid")
        name = criterion.get("member_name")
        accesses = [
            node
            for node in graph.nodes
            if node.symbol
            and node.symbol.get("kind") == "variable"
            and node.symbol.get("guid") == guid
            and node.symbol.get("access") in {"get", "set"}
        ]
        if not accesses:
            raise ContractValidationError(
                f"member criterion has no graph access: {guid}"
            )
        names = {str(node.symbol.get("name")) for node in accesses if node.symbol}
        if names != {name}:
            raise ContractValidationError(
                f"member criterion name does not match GUID: {name!r} vs {sorted(names)}"
            )
    else:
        raise ContractValidationError(f"unsupported slice_kind: {kind!r}")
    return graph, criterion


def _selected_contract(
    value: Mapping[str, Any],
) -> tuple[list[str], list[str], Mapping[str, Any] | None]:
    expected = value.get("expected")
    if expected is not None:
        if not isinstance(expected, dict):
            raise ContractValidationError("expected must be an object")
        node_ids = expected.get("node_ids")
        edge_ids = expected.get("edge_ids")
        counts = expected.get("counts")
    else:
        node_ids = value.get("node_ids")
        edge_ids = value.get("edge_ids")
        counts = value.get("counts")
    if not isinstance(node_ids, list) or not isinstance(edge_ids, list):
        raise ContractValidationError("node_ids and edge_ids must be arrays")
    if not isinstance(counts, dict):
        raise ContractValidationError("counts must be an object")
    return [str(item) for item in node_ids], [str(item) for item in edge_ids], counts


def _validate_membership(
    graph: Graph,
    node_ids: list[str],
    edge_ids: list[str],
    counts: Mapping[str, Any],
    *,
    ground_truth: bool,
) -> None:
    graph_nodes = {node.id: node for node in graph.nodes}
    graph_edges = {edge.id: edge for edge in graph.edges}
    missing_nodes = sorted(set(node_ids) - set(graph_nodes))
    missing_edges = sorted(set(edge_ids) - set(graph_edges))
    if missing_nodes:
        raise ContractValidationError(f"unknown selected node IDs: {missing_nodes}")
    if missing_edges:
        raise ContractValidationError(f"unknown selected edge IDs: {missing_edges}")
    selected = set(node_ids)
    induced = {
        edge.id
        for edge in graph.edges
        if edge.source_node_id in selected and edge.target_node_id in selected
    }
    if set(edge_ids) != induced:
        raise ContractValidationError(
            "edge_ids must equal the selected-node induced pin-level edges"
        )
    if counts.get("nodes") != len(node_ids) or counts.get("edges") != len(edge_ids):
        raise ContractValidationError("declared node/edge counts do not match arrays")
    if ground_truth:
        edge_kinds = Counter(graph_edges[edge_id].kind for edge_id in edge_ids)
        if counts.get("execution_edges") != edge_kinds["execution"]:
            raise ContractValidationError("execution_edges count mismatch")
        if counts.get("data_edges") != edge_kinds["data"]:
            raise ContractValidationError("data_edges count mismatch")


def _validate_review(value: Mapping[str, Any]) -> None:
    review = value.get("review")
    if not isinstance(review, dict):
        raise ContractValidationError("ground truth review must be an object")
    status = review.get("status")
    annotators = review.get("annotators")
    if not isinstance(annotators, list) or not annotators:
        raise ContractValidationError("ground truth requires at least one annotator")
    if len(set(annotators)) != len(annotators):
        raise ContractValidationError("ground-truth annotators must be unique")
    if status in {"independent_reviewed", "adjudicated", "frozen"} and len(annotators) < 2:
        raise ContractValidationError(
            f"{status} ground truth requires at least two annotators"
        )


def _validate_slice_metadata(
    value: Mapping[str, Any],
    graph: Graph,
    criterion: Mapping[str, Any],
    node_ids: list[str],
) -> None:
    if value.get("graph_id") != graph.id:
        raise ContractValidationError("slice graph_id differs from criterion graph_id")
    reasons = value.get("inclusion_reasons")
    if not isinstance(reasons, dict) or set(reasons) != set(node_ids):
        raise ContractValidationError(
            "inclusion_reasons keys must exactly equal selected node_ids"
        )
    for node_id, node_reasons in reasons.items():
        if not isinstance(node_reasons, list) or not node_reasons:
            raise ContractValidationError(
                f"selected node requires at least one inclusion reason: {node_id}"
            )
    if value.get("slice_kind") == "execution_context":
        criterion_nodes = {
            node_id for node_id, node_reasons in reasons.items()
            if "criterion" in node_reasons
        }
        if criterion_nodes != {criterion.get("node_id")}:
            raise ContractValidationError(
                "execution slice must mark exactly its criterion node"
            )
    elif any("criterion" in node_reasons for node_reasons in reasons.values()):
        raise ContractValidationError(
            "member-variable slice must not assign a node-level criterion reason"
        )

    graph_nodes = {node.id: node for node in graph.nodes}
    for node_id, node_reasons in reasons.items():
        node = graph_nodes[node_id]
        for access in ("get", "set"):
            reason = f"member_{access}"
            if reason in node_reasons and not (
                node.symbol
                and node.symbol.get("kind") == "variable"
                and node.symbol.get("access") == access
            ):
                raise ContractValidationError(
                    f"{reason} does not match source symbol: {node_id}"
                )

    boundaries = value.get("boundaries")
    if not isinstance(boundaries, list):
        raise ContractValidationError("boundaries must be an array")
    actual = {
        (
            item.get("node_id"),
            item.get("status"),
            item.get("reason"),
        )
        for item in boundaries
        if isinstance(item, dict)
    }
    expected = {
        (node.id, node.semantic_status, node.semantic_reason)
        for node in graph.nodes
        if node.id in set(node_ids) and node.semantic_status != "supported"
    }
    if actual != expected or len(actual) != len(boundaries):
        raise ContractValidationError(
            "boundaries must exactly match selected non-supported source nodes"
        )


def validate_contract_file(
    instance_path: str | Path,
    schema_path: str | Path,
) -> None:
    """Validate shape plus all cross-file Blueprint Lens v1 invariants."""

    instance = Path(instance_path).resolve()
    try:
        validate_json_file(instance, schema_path)
    except (SchemaValidationError, OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ContractValidationError(str(error)) from error
    value = _load_object(instance)
    format_name = value.get("format")
    if format_name == "blueprint-lens":
        try:
            load_blueprint_lens_v1(instance)
        except ReconstructionError as error:
            raise ContractValidationError(str(error)) from error
        return
    if format_name not in {"blueprint-lens-ground-truth", "blueprint-lens-slice"}:
        raise ContractValidationError(f"unsupported contract format: {format_name!r}")

    source_path, _ = _resolve_source(instance, value)
    try:
        document = load_blueprint_lens_v1(source_path)
    except ReconstructionError as error:
        raise ContractValidationError(str(error)) from error
    graph, criterion = _criterion_graph(document, value)
    node_ids, edge_ids, counts = _selected_contract(value)
    is_ground_truth = format_name == "blueprint-lens-ground-truth"
    _validate_membership(
        graph,
        node_ids,
        edge_ids,
        counts,
        ground_truth=is_ground_truth,
    )
    if is_ground_truth:
        _validate_review(value)
    else:
        _validate_slice_metadata(value, graph, criterion, node_ids)
