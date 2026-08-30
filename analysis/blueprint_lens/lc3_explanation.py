"""Project and validate the complete LC3 value-provenance Explanation."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .contract_validation import ContractValidationError, validate_contract_file
from .explanation_model import (
    CompleteDagRelationProjection,
    CompleteDagUnitProjection,
    ExplanationModelError,
    build_complete_dag_explanation,
    canonical_explanation_bytes,
    validate_explanation_model,
)
from .lc3_artifacts import (
    LC3_BLUEPRINT_PATH,
    LC3_EXPECTED_DATA_EDGES,
    LC3_EXPECTED_EDGES,
    LC3_EXPECTED_EXECUTION_EDGES,
    LC3_EXPECTED_NODES,
    LC3_FROZEN_LEDGER_FIELDS,
    LC3_MEMBER_GUID,
    LC3_MEMBER_NAME,
    LC3_QUESTION,
    LC3ArtifactError,
    _build_inventory_expected_sets,
    _build_lc3_truth,
    _compare_inventory,
    _load_object as _load_artifact_object,
    _parse_inventory,
    _precision_recall,
    _raw_inventory,
    validate_lc3_truth_ledger,
)
from .raw_probe import Edge, Node, Pin, ReconstructionError, load_blueprint_lens_v1
from .typed_ir import TypedIRBuildError, build_typed_ir


class LC3ExplanationError(ValueError):
    """Raised when LC3 cannot be projected without guessing source facts."""


@dataclass(slots=True)
class _PublishTarget:
    path: Path
    existed: bool = False
    original_payload: bytes | None = None
    staged_path: Path | None = None
    backup_path: Path | None = None
    restore_path: Path | None = None


@dataclass(frozen=True, slots=True)
class _RollbackTargetResult:
    target_path: Path
    restored: bool
    retain_transaction_files: bool
    backup_path: Path | None
    original_payload: bytes | None
    retained_paths: tuple[Path, ...]
    error: str | None


class _LC3Error(ValueError):
    pass


LC3_VERIFIED_AT_PUBLICATION_CHECKS = frozenset(
    {
        "ground_truth_contract_valid",
        "inventory_matches_raw_export",
        "inventory_runs_byte_identical",
        "precision_recall_1_0",
        "raw_runs_byte_identical",
        "slice_contract_valid",
        "typed_ir_contract_valid",
    }
)

LC3_DECLARED_BY_BUILDER_CHECKS = frozenset(
    {
        "asset_hash_stable",
        "data_endpoint_ledger_exact",
        "execution_controller_exact",
        "inventory_and_raw_independently_extracted_from_asset",
        "producer_closure_exact",
        "selected_membership_7_6",
        "status_and_boundary_coverage",
    }
)

LC3_READINESS_CHECKS = (
    LC3_VERIFIED_AT_PUBLICATION_CHECKS | LC3_DECLARED_BY_BUILDER_CHECKS
)


def _load_object(path: Path, label: str) -> Mapping[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise _LC3Error(f"cannot read {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise _LC3Error(f"{label} must be an object")
    return value


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()
    except OSError as error:
        raise _LC3Error(f"cannot hash {path}: {error}") from error


def _require_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise _LC3Error(f"{label} must be an array of strings")
    return value


def _validate_source_contracts(
    ir_path: Path,
    slice_path: Path,
    ground_truth_path: Path,
) -> None:
    repository_root = Path(__file__).resolve().parents[2]
    documents = (
        (
            "typed IR",
            ir_path,
            repository_root / "schemas" / "blueprint-lens-v1.schema.json",
        ),
        (
            "slice",
            slice_path,
            repository_root / "schemas" / "blueprint-lens-slice-v1.schema.json",
        ),
        (
            "ground truth",
            ground_truth_path,
            repository_root / "schemas" / "blueprint-lens-ground-truth-v1.schema.json",
        ),
    )
    for document_name, instance_path, schema_path in documents:
        try:
            validate_contract_file(instance_path, schema_path)
        except ContractValidationError as error:
            raise _LC3Error(
                f"LC3 {document_name} contract validation failed: {error}"
            ) from error


def _validate_slice_contract(slice_value: Mapping[str, Any]) -> tuple[list[str], list[str]]:
    node_ids = _require_string_list(slice_value.get("node_ids"), "slice.node_ids")
    edge_ids = _require_string_list(slice_value.get("edge_ids"), "slice.edge_ids")
    if len(node_ids) != len(set(node_ids)):
        raise _LC3Error("LC3 slice.node_ids contains duplicate entries")
    if len(edge_ids) != len(set(edge_ids)):
        raise _LC3Error("LC3 slice.edge_ids contains duplicate entries")
    expected_lengths = {"nodes": len(node_ids), "edges": len(edge_ids)}
    if slice_value.get("counts") != expected_lengths:
        raise _LC3Error("LC3 slice.counts must equal node_ids/edge_ids lengths")
    if expected_lengths != {"nodes": LC3_EXPECTED_NODES, "edges": LC3_EXPECTED_EDGES}:
        raise _LC3Error("LC3 slice.counts must be exactly 7 nodes/6 edges")
    return node_ids, edge_ids


def _resolve_run_siblings(canonical_path: Path, kind: str) -> tuple[Path, Path]:
    suffix = ".raw-0.2.json" if kind == "raw" else ".inventory.tsv"
    if not canonical_path.name.endswith(suffix):
        raise _LC3Error(f"LC3 canonical {kind} filename cannot resolve run siblings")
    stem = canonical_path.name[: -len(suffix)]
    run1 = canonical_path.with_name(f"{stem}.run1{suffix}")
    run2 = canonical_path.with_name(f"{stem}.run2{suffix}")
    missing = [str(path) for path in (run1, run2) if not path.is_file()]
    if missing:
        raise _LC3Error(f"LC3 {kind} run files are required: missing={missing}")
    return run1, run2


def _one(values: Iterable[Any], label: str) -> Any:
    matches = list(values)
    if len(matches) != 1:
        raise _LC3Error(f"{label} must resolve exactly once; found {len(matches)}")
    return matches[0]


def _short_class(node: Node) -> str:
    return node.class_path.rsplit(".", 1)[-1]


def _symbol(node: Node, access: str | None = None) -> Mapping[str, Any]:
    symbol = node.symbol or {}
    if symbol.get("kind") != "variable":
        return {}
    if access is not None and symbol.get("access") != access:
        return {}
    return symbol


def _criterion_node(nodes: Mapping[str, Node]) -> Node:
    return _one(
        (
            node
            for node in nodes.values()
            if _symbol(node, "set").get("guid") == LC3_MEMBER_GUID
        ),
        "LC3Score criterion by stable member GUID",
    )


def _selected_entities(
    ir_path: Path,
    slice_path: Path,
) -> tuple[Mapping[str, Any], Mapping[str, Node], Mapping[str, Edge], Mapping[str, Pin]]:
    try:
        document = load_blueprint_lens_v1(ir_path)
    except ReconstructionError as error:
        raise _LC3Error(str(error)) from error
    slice_value = _load_object(slice_path, "LC3 slice")
    node_ids, edge_ids = _validate_slice_contract(slice_value)
    graph_id = slice_value.get("graph_id")
    graph = _one(
        (candidate for candidate in document.graphs if candidate.id == graph_id),
        f"LC3 slice graph {graph_id}",
    )
    graph_nodes = {node.id: node for node in graph.nodes}
    graph_edges = {edge.id: edge for edge in graph.edges}
    missing_nodes = sorted(set(node_ids) - set(graph_nodes))
    missing_edges = sorted(set(edge_ids) - set(graph_edges))
    if missing_nodes or missing_edges:
        raise _LC3Error(
            "LC3 slice identities do not resolve in IR: "
            f"nodes={missing_nodes} edges={missing_edges}"
        )
    nodes = {node_id: graph_nodes[node_id] for node_id in node_ids}
    edges = {edge_id: graph_edges[edge_id] for edge_id in edge_ids}
    pins = {pin.id: pin for node in nodes.values() for pin in node.pins}
    return slice_value, nodes, edges, pins


def _validate_frozen_equal(declared: Any, derived: Any, path: str) -> None:
    if isinstance(derived, Mapping):
        if not isinstance(declared, Mapping) or set(declared) != set(derived):
            raise _LC3Error(
                f"LC3 value truth drift at {path}: declared {declared!r}, derived {derived!r}"
            )
        for key in sorted(derived):
            _validate_frozen_equal(declared[key], derived[key], f"{path}.{key}")
        return
    if isinstance(derived, list):
        if not isinstance(declared, list) or len(declared) != len(derived):
            raise _LC3Error(
                f"LC3 value truth drift at {path}: declared {declared!r}, derived {derived!r}"
            )
        for index, value in enumerate(derived):
            _validate_frozen_equal(declared[index], value, f"{path}[{index}]")
        return
    if declared != derived:
        raise _LC3Error(
            f"LC3 value truth drift at {path}: declared {declared!r}, derived {derived!r}"
        )


def _validate_freshness(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    value_truth_path: Path,
    readiness_path: Path | None,
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, list[str]]]:
    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_file = Path(asset_file).resolve()
    value_truth_path = Path(value_truth_path).resolve()
    slice_value = _load_object(slice_path, "LC3 slice")
    _validate_slice_contract(slice_value)
    try:
        ledger = validate_lc3_truth_ledger(
            value_truth_path,
            ir_path,
            slice_path,
            asset_file,
        )
    except (LC3ArtifactError, OSError, ReconstructionError) as error:
        raise _LC3Error(str(error)) from error
    source = ledger.get("source")
    if not isinstance(source, Mapping):
        raise _LC3Error("LC3 value truth source must be an object")
    ground_truth_name = source.get("ground_truth_file")
    if not isinstance(ground_truth_name, str) or not ground_truth_name:
        raise _LC3Error("LC3 value truth ground-truth source is missing")
    ground_truth_path = value_truth_path.parent / ground_truth_name
    if not ground_truth_path.is_file():
        raise _LC3Error(f"LC3 ground truth does not exist: {ground_truth_path}")
    if source.get("ground_truth_sha256") != _sha256(ground_truth_path):
        raise _LC3Error("LC3 value truth ground-truth SHA-256 mismatch")
    source_sha256 = source.get("ir_sha256")
    if source_sha256 != _sha256(ir_path):
        raise _LC3Error("LC3 ground truth source IR SHA-256 mismatch")
    criterion = slice_value.get("criterion")
    if (
        not isinstance(criterion, Mapping)
        or criterion.get("member_guid") != LC3_MEMBER_GUID
        or criterion.get("member_name") != LC3_MEMBER_NAME
    ):
        raise _LC3Error("LC3 slice criterion is not the stable LC3Score member identity")
    if criterion.get("question") != LC3_QUESTION:
        raise _LC3Error(
            "LC3 slice criterion.question disagrees with LC3_QUESTION"
        )
    if slice_value.get("source_sha256") != _sha256(ir_path):
        raise _LC3Error("LC3 slice source SHA-256 mismatch")
    if ledger.get("counts") != {
        "data_edges": LC3_EXPECTED_DATA_EDGES,
        "edges": LC3_EXPECTED_EDGES,
        "excluded_regions": len(ledger.get("exclusions", [])),
        "execution_edges": LC3_EXPECTED_EXECUTION_EDGES,
        "nodes": LC3_EXPECTED_NODES,
        "operations": 2,
        "producer_gets": 3,
        "producer_nodes": 5,
    }:
        raise _LC3Error("LC3 value truth frozen counts are not exact")

    def source_artifact(field: str, label: str) -> Path:
        filename = source.get(field)
        if not isinstance(filename, str) or not filename:
            raise _LC3Error(f"LC3 value truth {label} source is missing")
        for parent in (value_truth_path.parent, ir_path.parent):
            candidate = parent / filename
            if candidate.is_file():
                return candidate
        raise _LC3Error(f"LC3 {label} does not exist: {filename}")

    raw_path = source_artifact("raw_file", "canonical raw")
    inventory_path = source_artifact("inventory_file", "canonical inventory")
    if source.get("raw_sha256") != _sha256(raw_path):
        raise _LC3Error("LC3 value truth canonical raw SHA-256 mismatch")
    if source.get("inventory_sha256") != _sha256(inventory_path):
        raise _LC3Error("LC3 value truth canonical inventory SHA-256 mismatch")

    try:
        raw = _load_artifact_object(raw_path, "LC3 canonical raw")
        inventory = _parse_inventory(inventory_path)
        _compare_inventory(inventory, _raw_inventory(raw))
    except (LC3ArtifactError, OSError, ValueError, KeyError, TypeError) as error:
        raise _LC3Error(
            f"LC3 independent inventory/raw comparison failed: {error}"
        ) from error
    try:
        independent_node_ids, independent_edge_ids, edge_kinds = (
            _build_inventory_expected_sets(inventory, raw, str(slice_value.get("graph_id", "")))
        )
    except (LC3ArtifactError, OSError, ValueError, KeyError, TypeError) as error:
        raise _LC3Error(
            f"LC3 independent inventory/raw closure validation failed: {error}"
        ) from error

    ground_truth = _load_object(ground_truth_path, "LC3 ground truth")
    if ground_truth.get("format") != "blueprint-lens-ground-truth":
        raise _LC3Error("LC3 independent ground truth format is not supported")
    if ground_truth.get("source_sha256") != _sha256(ir_path):
        raise _LC3Error("LC3 independent ground truth source IR SHA-256 mismatch")
    ground_truth_criterion = ground_truth.get("criterion")
    if (
        not isinstance(ground_truth_criterion, Mapping)
        or ground_truth_criterion.get("graph_id") != slice_value.get("graph_id")
        or ground_truth_criterion.get("member_guid") != LC3_MEMBER_GUID
        or ground_truth_criterion.get("member_name") != LC3_MEMBER_NAME
    ):
        raise _LC3Error("LC3 independent ground truth criterion is not the stable LC3Score identity")
    if ground_truth_criterion.get("question") != LC3_QUESTION:
        raise _LC3Error(
            "LC3 ground truth criterion.question disagrees with LC3_QUESTION"
        )
    expected = ground_truth.get("expected")
    if not isinstance(expected, Mapping):
        raise _LC3Error("LC3 independent ground truth expected closure is missing")
    expected_node_ids = _require_string_list(
        expected.get("node_ids"), "LC3 ground truth expected.node_ids"
    )
    expected_edge_ids = _require_string_list(
        expected.get("edge_ids"), "LC3 ground truth expected.edge_ids"
    )
    expected_counts = expected.get("counts")
    if expected_counts != {
        "nodes": LC3_EXPECTED_NODES,
        "edges": LC3_EXPECTED_EDGES,
        "execution_edges": LC3_EXPECTED_EXECUTION_EDGES,
        "data_edges": LC3_EXPECTED_DATA_EDGES,
    }:
        raise _LC3Error("LC3 independent ground truth counts are not exact")
    node_precision, node_recall = _precision_recall(
        slice_value["node_ids"], expected_node_ids
    )
    edge_precision, edge_recall = _precision_recall(
        slice_value["edge_ids"], expected_edge_ids
    )
    if any(
        value != 1.0
        for value in (node_precision, node_recall, edge_precision, edge_recall)
    ):
        raise _LC3Error(
            "LC3 publication precision/recall recomputation failed: "
            f"node precision={node_precision} recall={node_recall} "
            f"edge precision={edge_precision} recall={edge_recall}"
        )
    if (
        len(expected_node_ids) != len(set(expected_node_ids))
        or set(expected_node_ids) != set(independent_node_ids)
    ):
        raise _LC3Error(
            "LC3 independent ground truth nodes disagree with inventory/raw closure: "
            f"missing={sorted(set(independent_node_ids) - set(expected_node_ids))} "
            f"extra={sorted(set(expected_node_ids) - set(independent_node_ids))}"
        )
    if (
        len(expected_edge_ids) != len(set(expected_edge_ids))
        or set(expected_edge_ids) != set(independent_edge_ids)
    ):
        raise _LC3Error(
            "LC3 independent ground truth edges disagree with inventory/raw closure: "
            f"missing={sorted(set(independent_edge_ids) - set(expected_edge_ids))} "
            f"extra={sorted(set(expected_edge_ids) - set(independent_edge_ids))}"
        )

    raw_blueprint = raw.get("blueprint")
    if not isinstance(raw_blueprint, Mapping):
        raise _LC3Error("LC3 canonical raw blueprint is not an object")
    raw_graph = _one(
        (
            candidate
            for candidate in raw_blueprint.get("graphs", [])
            if isinstance(candidate, Mapping)
            and str(candidate.get("id")) == str(slice_value.get("graph_id", ""))
        ),
        "LC3 independent raw graph",
    )
    raw_nodes = {
        str(node.get("native_guid")): node
        for node in raw_graph.get("nodes", [])
        if isinstance(node, Mapping)
    }
    criterion_native_guids = [
        native_guid
        for native_guid, node in raw_nodes.items()
        if str(node.get("class", "")).rsplit(".", 1)[-1] == "K2Node_VariableSet"
        and isinstance(node.get("symbol"), Mapping)
        and node["symbol"].get("access") == "set"
        and node["symbol"].get("guid") == LC3_MEMBER_GUID
        and node["symbol"].get("name") == LC3_MEMBER_NAME
    ]
    if len(criterion_native_guids) != 1:
        raise _LC3Error("LC3 independent raw closure must resolve one LC3Score Set")
    graph_id = str(slice_value.get("graph_id", ""))
    criterion_node_id = f"{graph_id}::node::{criterion_native_guids[0]}"
    execution_edge_ids = [
        edge_id for edge_id, kind in edge_kinds.items() if kind == "execution"
    ]
    if len(execution_edge_ids) != LC3_EXPECTED_EXECUTION_EDGES:
        raise _LC3Error("LC3 independent raw closure must resolve one execution edge")
    inventory_edges = {
        f"{graph_id}::edge::{graph_id}::node::{edge[1]}::pin::locator-"
        f"{edge[2]}-{edge[3]}-{edge[4]}->{graph_id}::node::{edge[5]}::pin::locator-"
        f"{edge[6]}-{edge[7]}-{edge[8]}": edge
        for edge in inventory.edges
        if edge[0] == graph_id
    }
    execution_edge = inventory_edges.get(execution_edge_ids[0])
    if execution_edge is None:
        raise _LC3Error("LC3 independent execution edge identity is missing")
    controller_node_id = f"{graph_id}::node::{execution_edge[1]}"
    if criterion_node_id not in independent_node_ids or controller_node_id not in independent_node_ids:
        raise _LC3Error("LC3 independent role closure does not cover criterion and controller")
    inclusion_reasons: dict[str, list[str]] = {
        criterion_node_id: ["member_set"],
        controller_node_id: ["direct_write_controller"],
    }
    producer_counts = {"K2Node_VariableGet": 0, "K2Node_PromotableOperator": 0}
    for node_id in independent_node_ids:
        if node_id in inclusion_reasons:
            continue
        native_guid = node_id.rsplit("::node::", 1)[-1]
        node = raw_nodes.get(native_guid)
        short_class = str(node.get("class", "")).rsplit(".", 1)[-1] if node else ""
        if short_class not in producer_counts:
            raise _LC3Error(
                f"LC3 independent raw closure has an unsupported producer role: {node_id}"
            )
        producer_counts[short_class] += 1
        inclusion_reasons[node_id] = ["required_data_producer"]
    if producer_counts != {"K2Node_VariableGet": 3, "K2Node_PromotableOperator": 2}:
        raise _LC3Error("LC3 independent producer roles are not three Gets and two operations")

    if readiness_path is None:
        readiness_path = value_truth_path.parent / "readiness.v1.json"
    readiness_path = Path(readiness_path)
    if not readiness_path.is_file():
        raise _LC3Error(f"LC3 readiness file is required: {readiness_path}")
    readiness = _load_object(readiness_path, "LC3 readiness")
    if (
        readiness.get("format") != "blueprint-lens-lc3-readiness"
        or readiness.get("status") != "TRUTH_FROZEN"
    ):
        raise _LC3Error("LC3 readiness is not frozen")

    review = ground_truth.get("review")
    if not isinstance(review, Mapping) or review.get("status") != "frozen":
        raise _LC3Error("LC3 frozen ground truth review.status must be 'frozen'")
    annotators = review.get("annotators")
    if (
        not isinstance(annotators, list)
        or not annotators
        or any(not isinstance(annotator, str) or not annotator.strip() for annotator in annotators)
    ):
        raise _LC3Error(
            "LC3 frozen ground truth review.annotators must be a non-empty list of non-empty strings"
        )
    reviewed_at = review.get("reviewed_at")
    if not isinstance(reviewed_at, str) or not reviewed_at.strip():
        raise _LC3Error(
            "LC3 frozen ground truth review.reviewed_at must be a non-empty string"
        )

    for block_name, expected_keys in (
        ("verified_at_publication", LC3_VERIFIED_AT_PUBLICATION_CHECKS),
        ("declared_by_builder", LC3_DECLARED_BY_BUILDER_CHECKS),
    ):
        checks = readiness.get(block_name)
        if (
            not isinstance(checks, Mapping)
            or set(checks) != expected_keys
            or any(type(value) is not bool or not value for value in checks.values())
        ):
            raise _LC3Error("LC3 readiness checks are not all true")
    metrics = readiness.get("metrics")
    expected_metrics = {
        "node_precision": node_precision,
        "node_recall": node_recall,
        "edge_precision": edge_precision,
        "edge_recall": edge_recall,
    }
    if (
        not isinstance(metrics, Mapping)
        or set(metrics) != set(expected_metrics)
        or any(
            type(metrics[key]) not in (int, float) or metrics[key] != 1.0
            for key in expected_metrics
        )
    ):
        raise _LC3Error("LC3 readiness metrics are not all 1.0")
    readiness_counts = readiness.get("counts")
    expected_readiness_counts = {
        "nodes": LC3_EXPECTED_NODES,
        "edges": LC3_EXPECTED_EDGES,
        "execution_edges": LC3_EXPECTED_EXECUTION_EDGES,
        "data_edges": LC3_EXPECTED_DATA_EDGES,
        "producer_nodes": 5,
        "producer_gets": 3,
        "operations": 2,
        "excluded_regions": len(ledger["exclusions"]),
    }
    if readiness_counts != expected_readiness_counts:
        raise _LC3Error(
            "LC3 readiness counts are not reviewed 7 nodes/6 edges with 5 data and 1 execution edge"
        )
    hashes = readiness.get("hashes")
    if not isinstance(hashes, Mapping):
        raise _LC3Error("LC3 readiness value truth hash does not match")

    raw_run1_path, raw_run2_path = _resolve_run_siblings(raw_path, "raw")
    inventory_run1_path, inventory_run2_path = _resolve_run_siblings(
        inventory_path, "inventory"
    )
    raw_run1_sha256 = _sha256(raw_run1_path)
    raw_run2_sha256 = _sha256(raw_run2_path)
    if raw_run1_sha256 != raw_run2_sha256:
        raise _LC3Error("LC3 raw run files are not byte-identical")
    if hashes.get("raw_run1_sha256") != raw_run1_sha256:
        raise _LC3Error(
            "LC3 readiness raw_run1_sha256 does not match observed raw run 1"
        )
    if hashes.get("raw_run2_sha256") != raw_run2_sha256:
        raise _LC3Error(
            "LC3 readiness raw_run2_sha256 does not match observed raw run 2"
        )
    if _sha256(raw_path) != raw_run1_sha256:
        raise _LC3Error("LC3 canonical raw does not match observed raw run 1")

    inventory_run1_sha256 = _sha256(inventory_run1_path)
    inventory_run2_sha256 = _sha256(inventory_run2_path)
    if inventory_run1_sha256 != inventory_run2_sha256:
        raise _LC3Error("LC3 inventory run files are not byte-identical")
    if hashes.get("inventory_run1_sha256") != inventory_run1_sha256:
        raise _LC3Error(
            "LC3 readiness inventory_run1_sha256 does not match observed inventory run 1"
        )
    if hashes.get("inventory_run2_sha256") != inventory_run2_sha256:
        raise _LC3Error(
            "LC3 readiness inventory_run2_sha256 does not match observed inventory run 2"
        )
    if _sha256(inventory_path) != inventory_run1_sha256:
        raise _LC3Error("LC3 canonical inventory does not match observed inventory run 1")

    for field, path in (
        ("asset_sha256_before", asset_file),
        ("asset_sha256_after", asset_file),
        ("ir_sha256", ir_path),
        ("slice_sha256", slice_path),
        ("ground_truth_sha256", ground_truth_path),
        ("value_truth_sha256", value_truth_path),
    ):
        if hashes.get(field) != _sha256(path):
            raise _LC3Error(f"LC3 readiness {field} does not match")
    layout_readiness = readiness.get("layout_readiness")
    if (
        not isinstance(layout_readiness, Mapping)
        or layout_readiness.get("status") != "COMPLETE"
    ):
        raise _LC3Error("LC3 readiness is not complete for port layout")
    if layout_readiness.get("endpoint_count") != LC3_EXPECTED_DATA_EDGES:
        raise _LC3Error("LC3 readiness endpoint count is not 5")
    _validate_source_contracts(ir_path, slice_path, ground_truth_path)
    try:
        rederived_ir = build_typed_ir(raw, expected_blueprint_path=LC3_BLUEPRINT_PATH)
    except TypedIRBuildError as error:
        raise _LC3Error(f"LC3 typed IR re-derivation failed: {error}") from error
    ir_document = _load_artifact_object(ir_path, "LC3 typed IR")
    if rederived_ir != ir_document:
        raise _LC3Error(
            "LC3 typed IR does not match the IR re-derived from the canonical raw export"
        )
    try:
        document = load_blueprint_lens_v1(ir_path)
    except ReconstructionError as error:
        raise _LC3Error(str(error)) from error
    graph = _one(
        (candidate for candidate in document.graphs if candidate.id == slice_value.get("graph_id")),
        "LC3 value truth graph",
    )
    derived = _build_lc3_truth(document, graph)
    for field in LC3_FROZEN_LEDGER_FIELDS:
        _validate_frozen_equal(ledger[field], derived[field], field)
    return slice_value, ledger, inclusion_reasons


def _projection_specs(
    ir_path: Path,
    slice_path: Path,
) -> tuple[
    Mapping[str, CompleteDagUnitProjection],
    Mapping[str, CompleteDagRelationProjection],
]:
    slice_value, nodes, edges, pins = _selected_entities(ir_path, slice_path)
    criterion = _criterion_node(nodes)
    unit_specs: dict[str, CompleteDagUnitProjection] = {}
    event_nodes: list[Node] = []
    for node_id in sorted(nodes):
        node = nodes[node_id]
        if node.semantic_status != "supported":
            raise _LC3Error(f"selected node is unknown or unsupported: {node_id}")
        short_class = _short_class(node)
        if short_class == "K2Node_Event":
            event_nodes.append(node)
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="control", kind="node", title="BeginPlay"
            )
        elif short_class == "K2Node_VariableSet":
            symbol = _symbol(node, "set")
            if node.id != criterion.id or symbol.get("guid") != LC3_MEMBER_GUID:
                raise _LC3Error(f"selected Set is not the LC3Score criterion: {node_id}")
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="criterion", kind="node", title="Set LC3Score"
            )
        elif short_class == "K2Node_VariableGet":
            symbol = _symbol(node, "get")
            name = str(symbol.get("name", ""))
            if not name or not symbol.get("guid"):
                raise _LC3Error(f"selected Get has no stable member symbol: {node_id}")
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="value", kind="node", title=f"Get {name}"
            )
        elif short_class == "K2Node_PromotableOperator":
            name = str((node.symbol or {}).get("name", ""))
            if name not in {"Add_IntInt", "Subtract_IntInt"}:
                raise _LC3Error(f"selected operator is outside LC3 support: {node_id}")
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="value", kind="node", title=name
            )
        else:
            raise _LC3Error(f"selected node is unknown or unsupported: {node_id}")
    if len(event_nodes) != 1:
        raise _LC3Error("LC3 direct execution controller must resolve to one Event")
    relation_specs: dict[str, CompleteDagRelationProjection] = {}
    for edge_id in sorted(edges):
        edge = edges[edge_id]
        source_node = nodes[edge.source_node_id]
        target_node = nodes[edge.target_node_id]
        source_pin = pins[edge.source_pin_id]
        target_pin = pins[edge.target_pin_id]
        if edge.kind == "execution":
            if (
                _short_class(source_node) != "K2Node_Event"
                or target_node.id != criterion.id
                or source_pin.name != "then"
                or target_pin.name != "execute"
            ):
                raise _LC3Error(f"LC3 execution relation endpoint is not BeginPlay -> Set execute: {edge_id}")
            relation_specs[edge_id] = CompleteDagRelationProjection(
                kind="execution_predecessor", label=source_pin.name
            )
        elif edge.kind == "data":
            if source_pin.kind != "data" or target_pin.kind != "data":
                raise _LC3Error(f"LC3 value relation is not data-to-data: {edge_id}")
            if _short_class(source_node) not in {
                "K2Node_VariableGet",
                "K2Node_PromotableOperator",
            }:
                raise _LC3Error(f"LC3 value relation source is not a producer: {edge_id}")
            if _short_class(target_node) not in {
                "K2Node_PromotableOperator",
                "K2Node_VariableSet",
            }:
                raise _LC3Error(f"LC3 value relation target is not a consumer: {edge_id}")
            relation_specs[edge_id] = CompleteDagRelationProjection(
                kind="provides_value", label=target_pin.name
            )
        else:
            raise _LC3Error(f"LC3 selected edge has unsupported kind: {edge_id}")
    if len(unit_specs) != LC3_EXPECTED_NODES or len(relation_specs) != LC3_EXPECTED_EDGES:
        raise _LC3Error("LC3 complete projection must contain exactly 7/6")
    if slice_value.get("criterion", {}).get("member_guid") != LC3_MEMBER_GUID:
        raise _LC3Error("LC3 projection criterion is not bound to the member GUID")
    return unit_specs, relation_specs


def _relation_by_edge(model: Mapping[str, Any]) -> Mapping[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    for relation in model.get("relations", []):
        edge_ids = relation.get("source_edge_ids")
        endpoints = relation.get("source_edge_endpoints")
        if (
            not isinstance(edge_ids, list)
            or len(edge_ids) != 1
            or not isinstance(endpoints, list)
            or len(endpoints) != 1
        ):
            raise _LC3Error(
                "AMBIGUOUS_PORT_ENDPOINT: every LC3 relation requires one source edge and endpoint"
            )
        edge_id = edge_ids[0]
        if edge_id in result:
            raise _LC3Error(f"LC3 relation edge ownership overlaps: {edge_id}")
        result[edge_id] = relation
    return result


def _assert_endpoint(
    relation: Mapping[str, Any],
    edge: Edge,
    pins: Mapping[str, Pin],
) -> Mapping[str, Any]:
    endpoints = relation.get("source_edge_endpoints")
    if not isinstance(endpoints, list) or len(endpoints) != 1:
        raise _LC3Error(
            f"AMBIGUOUS_PORT_ENDPOINT: relation endpoint is absent or non-unique: {relation.get('id')}"
        )
    endpoint = endpoints[0]
    if not isinstance(endpoint, Mapping):
        raise _LC3Error(
            f"AMBIGUOUS_PORT_ENDPOINT: relation endpoint is not an object: {relation.get('id')}"
        )
    expected = {
        "source_edge_id": edge.id,
        "source_node_id": edge.source_node_id,
        "source_pin_id": edge.source_pin_id,
        "source_port_label": pins[edge.source_pin_id].name,
        "target_node_id": edge.target_node_id,
        "target_pin_id": edge.target_pin_id,
        "target_port_label": pins[edge.target_pin_id].name,
    }
    for field, value in expected.items():
        if endpoint.get(field) != value:
            raise _LC3Error(
                "AMBIGUOUS_PORT_ENDPOINT: relation endpoint does not match exported pin identity: "
                f"{relation.get('id')}.{field}"
            )
    return endpoint


def _unit_ids_by_node(model: Mapping[str, Any]) -> Mapping[str, str]:
    result: dict[str, str] = {}
    for unit in model.get("units", []):
        references = unit.get("source_references")
        if not isinstance(references, list) or len(references) != 1:
            raise _LC3Error("LC3 units must own one disjoint source node")
        node_id = references[0].get("source_node_id")
        if not isinstance(node_id, str) or node_id in result:
            raise _LC3Error("LC3 source node ownership must be disjoint")
        result[node_id] = str(unit["id"])
    return result


def _expected_group(
    ledger: Mapping[str, Any],
    unit_ids: Mapping[str, str],
    relations_by_edge: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    truth_group = ledger["value_cone"]
    try:
        expected = {
            "id": truth_group["group_id"],
            "kind": truth_group["kind"],
            "title": truth_group["title"],
            "ordered_unit_ids": [unit_ids[node_id] for node_id in truth_group["ordered_node_ids"]],
            "ordered_relation_ids": [
                relations_by_edge[edge_id]["id"]
                for edge_id in truth_group["ordered_edge_ids"]
            ],
            "entry_unit_id": unit_ids[truth_group["entry_node_id"]],
            "parent_group_id": truth_group["parent_group_id"],
            "entered_by": truth_group["entered_by"],
            "member_count": truth_group["member_count"],
            "projection_status": truth_group["projection_status"],
            "diagnostic_code": truth_group["diagnostic_code"],
            "claim_evidence": truth_group["claim_evidence"],
        }
    except (KeyError, TypeError) as error:
        raise _LC3Error(f"LC3 value_cone ledger cannot resolve into the model: {error}") from error
    return expected


def validate_lc3_value_explanation(
    model: Mapping[str, Any],
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    value_truth_path: Path,
    *,
    readiness_path: Path | None = None,
) -> None:
    """Cross-check every LC3 Explanation fact against the frozen value ledger."""

    try:
        slice_value, ledger, inclusion_reasons = _validate_freshness(
            Path(ir_path),
            Path(slice_path),
            Path(asset_file),
            Path(value_truth_path),
            readiness_path,
        )
        model_query = model.get("query")
        if not isinstance(model_query, Mapping) or model_query.get("question") != LC3_QUESTION:
            raise _LC3Error(
                "LC3 Explanation question disagrees with LC3_QUESTION"
            )
        _, nodes, edges, pins = _selected_entities(Path(ir_path), Path(slice_path))
        if model.get("counts") != {
            "lanes": 6,
            "units": LC3_EXPECTED_NODES,
            "relations": LC3_EXPECTED_EDGES,
            "source_nodes": LC3_EXPECTED_NODES,
            "source_edges": LC3_EXPECTED_EDGES,
        }:
            raise _LC3Error("LC3 Explanation declared counts do not match 7/6")
        model_source = model.get("source")
        truth_source = ledger.get("source")
        if not isinstance(model_source, Mapping) or not isinstance(truth_source, Mapping):
            raise _LC3Error("LC3 Explanation source provenance must be an object")
        if (
            model_source.get("ir_sha256") != _sha256(Path(ir_path))
            or model_source.get("slice_sha256") != _sha256(Path(slice_path))
            or model_source.get("blueprint_package_sha256") != _sha256(Path(asset_file))
            or model_source.get("blueprint_asset_path") != LC3_BLUEPRINT_PATH
            or model_source.get("graph_id") != truth_source.get("graph_id")
        ):
            raise _LC3Error("LC3 Explanation source provenance disagrees with frozen inputs")
        units_by_node = _unit_ids_by_node(model)
        expected_node_ids = {
            str(reference["node_id"]) for reference in ledger["source_references"]
        }
        if set(units_by_node) != expected_node_ids or set(units_by_node) != set(nodes):
            raise _LC3Error("LC3 source node ownership disagrees with value truth")
        criterion_node = _criterion_node(nodes)
        if model.get("criterion_unit_id") != units_by_node[criterion_node.id]:
            raise _LC3Error("LC3 criterion unit does not resolve from the stable member GUID")
        status_by_node = {
            str(item["node_id"]): item
            for item in ledger["status_coverage"]
        }
        for unit in model["units"]:
            reference = unit["source_references"][0]
            node_id = reference["source_node_id"]
            status_fact = status_by_node.get(node_id)
            if status_fact is None:
                raise _LC3Error(f"LC3 status coverage is missing: {node_id}")
            if (
                unit["semantic_status"] != status_fact["semantic_status"]
                or status_fact["semantic_reason"]
            ):
                raise _LC3Error(f"LC3 unit status/boundary coverage disagrees: {node_id}")
            truth_reference = next(
                item for item in ledger["source_references"] if item["node_id"] == node_id
            )
            source_pin_ids = _require_string_list(
                reference.get("source_pin_ids"),
                f"LC3 Explanation source pin IDs for {node_id}",
            )
            truth_pin_ids = _require_string_list(
                truth_reference.get("source_pin_ids"),
                f"LC3 value truth source pin IDs for {node_id}",
            )
            if len(source_pin_ids) != len(set(source_pin_ids)):
                raise _LC3Error("LC3 source pin IDs contain duplicate entries")
            if len(truth_pin_ids) != len(set(truth_pin_ids)):
                raise _LC3Error("LC3 value truth source pin IDs contain duplicate entries")
            if set(source_pin_ids) != set(truth_pin_ids):
                raise _LC3Error(f"LC3 source pin coverage disagrees: {node_id}")
            expected_reasons = inclusion_reasons.get(node_id)
            if expected_reasons is None or unit.get("inclusion_reasons") != expected_reasons:
                raise _LC3Error(
                    "LC3 inclusion reasons disagree with independent truth: "
                    f"{node_id} expected={expected_reasons!r} "
                    f"actual={unit.get('inclusion_reasons')!r}"
                )
        if ledger["boundaries"]:
            raise _LC3Error("LC3 frozen value truth unexpectedly contains a boundary")

        relations_by_edge = _relation_by_edge(model)
        expected_edges = set(edges)
        if set(relations_by_edge) != expected_edges:
            missing = sorted(expected_edges - set(relations_by_edge))
            extra = sorted(set(relations_by_edge) - expected_edges)
            raise _LC3Error(
                f"LC3 relation coverage is not exact: missing={missing} extra={extra}"
            )
        graph_data_edges = {
            edge_id for edge_id, edge in edges.items() if edge.kind == "data"
        }
        ledger_data_edges = {
            str(item["edge_id"]) for item in ledger["data_relations"]
        }
        if ledger_data_edges != graph_data_edges:
            raise _LC3Error("LC3 data relation coverage disagrees with value truth")
        for edge_id, relation in relations_by_edge.items():
            endpoint = _assert_endpoint(relation, edges[edge_id], pins)
            edge = edges[edge_id]
            if relation["source_unit_id"] != units_by_node[edge.source_node_id] or relation["target_unit_id"] != units_by_node[edge.target_node_id]:
                raise _LC3Error(f"LC3 relation direction disagrees with IR edge: {edge_id}")
            if relation["port_label"] != endpoint["source_port_label"]:
                raise _LC3Error(f"LC3 port_label disagrees with endpoint provenance: {edge_id}")
            if edge.kind == "execution":
                if (
                    relation["kind"] != "execution_predecessor"
                    or relation["label"] != endpoint["source_port_label"]
                    or relation["semantic_label"] != "next_execution"
                ):
                    raise _LC3Error(f"LC3 execution relation semantics are not exact: {edge_id}")
            else:
                if (
                    relation["kind"] != "provides_value"
                    or relation["label"] != endpoint["target_port_label"]
                    or relation["semantic_label"] != "value_input"
                ):
                    raise _LC3Error(f"LC3 value relation semantics are not exact: {edge_id}")
        expected_controller_edge = str(ledger["execution_controller"]["edge"]["edge_id"])
        if expected_controller_edge not in relations_by_edge:
            raise _LC3Error("LC3 direct execution controller relation is missing")
        if relations_by_edge[expected_controller_edge]["kind"] != "execution_predecessor":
            raise _LC3Error("LC3 direct execution controller was conflated with value provenance")
        group_values = model.get("groups")
        if not isinstance(group_values, list) or len(group_values) != 1:
            raise _LC3Error("LC3 Explanation must contain exactly one value_cone group")
        expected_group = _expected_group(ledger, units_by_node, relations_by_edge)
        group = group_values[0]
        if set(group) != set(expected_group):
            raise _LC3Error("LC3 value_cone emitted fields are not ledger-bound")
        for field, expected_value in expected_group.items():
            if group.get(field) != expected_value:
                raise _LC3Error(f"LC3 value_cone field is not ledger-bound: {field}")
        if expected_controller_edge in {
            edge_id
            for edge_id, relation in relations_by_edge.items()
            if relation["id"] in group["ordered_relation_ids"]
        }:
            raise _LC3Error("LC3 value_cone must not contain the execution controller")
        if set(group["ordered_relation_ids"]) != {
            relation["id"]
            for edge_id, relation in relations_by_edge.items()
            if edge_id in graph_data_edges
        }:
            raise _LC3Error("LC3 value_cone must contain exactly the five data relations")
        expected_group_units = {
            units_by_node[node_id] for node_id in ledger["value_cone"]["ordered_node_ids"]
        }
        if set(group["ordered_unit_ids"]) != expected_group_units:
            raise _LC3Error("LC3 value_cone membership disagrees with producer closure")
        if set(group["ordered_unit_ids"]) == {units_by_node[ledger["execution_controller"]["node_id"]]}:
            raise _LC3Error("LC3 value_cone cannot collapse to the execution controller")
        if slice_value.get("slice_kind") != "member_variable_data_dependency":
            raise _LC3Error("LC3 Explanation is not bound to the member-variable slice")
    except _LC3Error as error:
        raise LC3ExplanationError(str(error)) from error


def _populate_semantics_and_group(
    model: dict[str, Any],
    ledger: Mapping[str, Any],
) -> None:
    for relation in model["relations"]:
        endpoints = relation.get("source_edge_endpoints") or []
        if len(endpoints) != 1:
            raise _LC3Error(
                f"AMBIGUOUS_PORT_ENDPOINT: relation has no unique endpoint: {relation['id']}"
            )
        relation["port_label"] = endpoints[0]["source_port_label"]
        relation["semantic_label"] = (
            "next_execution"
            if relation["kind"] == "execution_predecessor"
            else "value_input"
        )
    units_by_node = _unit_ids_by_node(model)
    relations_by_edge = _relation_by_edge(model)
    model["groups"] = [
        _expected_group(ledger, units_by_node, relations_by_edge)
    ]


def build_lc3_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    value_truth_path: Path,
    *,
    readiness_path: Path | None = None,
) -> dict[str, Any]:
    """Build the complete LC3 Explanation from validated source truth."""

    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_file = Path(asset_file).resolve()
    value_truth_path = Path(value_truth_path).resolve()
    _, ledger, _ = _validate_freshness(
        ir_path,
        slice_path,
        asset_file,
        value_truth_path,
        readiness_path,
    )
    unit_specs, relation_specs = _projection_specs(ir_path, slice_path)
    try:
        model = build_complete_dag_explanation(
            ir_path,
            slice_path,
            asset_file,
            unit_specs,
            relation_specs,
            question=LC3_QUESTION,
        )
    except ExplanationModelError as error:
        raise LC3ExplanationError(str(error)) from error
    _populate_semantics_and_group(model, ledger)
    validate_lc3_value_explanation(
        model,
        ir_path,
        slice_path,
        asset_file,
        value_truth_path,
        readiness_path=readiness_path,
    )
    return model


def _ensure_publish_parent(target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)


def _write_publish_temporary(target: Path, payload: bytes, marker: str) -> Path:
    descriptor, temporary_name = tempfile.mkstemp(
        dir=target.parent,
        prefix=f".{target.name}.{marker}-",
        suffix=".tmp",
    )
    temporary_path = Path(temporary_name)
    stream_opened = False
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream_opened = True
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        if temporary_path.read_bytes() != payload:
            raise _LC3Error(f"LC3 {marker} verification failed for {target}")
        return temporary_path
    except Exception:
        if not stream_opened:
            os.close(descriptor)
        temporary_path.unlink(missing_ok=True)
        raise


def _rollback_publication(states: Sequence[_PublishTarget]) -> list[_RollbackTargetResult]:
    results: list[_RollbackTargetResult] = []
    for state in reversed(states):
        restored = False
        error_text: str | None = None
        try:
            if state.existed:
                target_is_original = (
                    state.path.exists()
                    and state.path.read_bytes() == state.original_payload
                )
                if not target_is_original:
                    if state.backup_path is None or not state.backup_path.exists():
                        raise OSError("verified backup is unavailable")
                    backup_payload = state.backup_path.read_bytes()
                    if backup_payload != state.original_payload:
                        raise OSError("verified backup does not match original target")
                    state.restore_path = _write_publish_temporary(
                        state.path, backup_payload, "stage-restore"
                    )
                    os.replace(state.restore_path, state.path)
                if state.path.read_bytes() != state.original_payload:
                    raise OSError("restored bytes do not match original target")
                restored = True
            else:
                state.path.unlink(missing_ok=True)
                if state.path.exists():
                    raise OSError("new target could not be removed")
                restored = True
        except (OSError, _LC3Error) as error:
            error_text = str(error)
            try:
                restored = (
                    state.path.exists() and state.path.read_bytes() == state.original_payload
                    if state.existed
                    else not state.path.exists()
                )
            except OSError:
                restored = False
        transaction_paths = tuple(
            path
            for path in (state.staged_path, state.backup_path, state.restore_path)
            if path is not None and path.exists()
        )
        results.append(
            _RollbackTargetResult(
                state.path,
                restored,
                not restored,
                state.backup_path,
                state.original_payload,
                transaction_paths if not restored else (),
                error_text,
            )
        )
    return results


def _cleanup_publication(
    states: Sequence[_PublishTarget],
    *,
    cleanup_targets: set[Path] | None = None,
) -> list[str]:
    errors: list[str] = []
    for state in states:
        if cleanup_targets is not None and state.path not in cleanup_targets:
            continue
        for temporary_path in (state.staged_path, state.backup_path, state.restore_path):
            if temporary_path is None:
                continue
            try:
                temporary_path.unlink(missing_ok=True)
            except OSError as error:
                errors.append(f"{temporary_path}: {error}")
    return errors


def _publish_validated_payload(payload: bytes, targets: Sequence[Path]) -> None:
    resolved_targets = [Path(target).resolve() for target in targets]
    if len(resolved_targets) != len(set(resolved_targets)):
        raise _LC3Error("LC3 publication targets must be distinct")
    states = [_PublishTarget(path=target) for target in resolved_targets]
    try:
        for state in states:
            state.existed = state.path.exists()
            if state.existed:
                state.original_payload = state.path.read_bytes()
        for state in states:
            _ensure_publish_parent(state.path)
        for state in states:
            state.staged_path = _write_publish_temporary(state.path, payload, "stage")
        for state in states:
            if state.existed:
                assert state.original_payload is not None
                state.backup_path = _write_publish_temporary(
                    state.path, state.original_payload, "backup"
                )
        for state in states:
            if state.existed and state.path.read_bytes() != state.original_payload:
                raise OSError(f"publication target changed before replace: {state.path}")
        for state in states:
            assert state.staged_path is not None
            os.replace(state.staged_path, state.path)
        for state in states:
            if state.path.read_bytes() != payload:
                raise OSError(f"published bytes failed verification: {state.path}")
    except (OSError, _LC3Error) as error:
        rollback_results = _rollback_publication(states)
        restored_targets = {
            result.target_path
            for result in rollback_results
            if not result.retain_transaction_files
        }
        cleanup_errors = _cleanup_publication(states, cleanup_targets=restored_targets)
        detail = f"LC3 publication failed: {error}"
        failed = [result for result in rollback_results if not result.restored]
        if failed:
            detail += "; rollback incomplete"
        if cleanup_errors:
            detail += "; cleanup residue: " + "; ".join(cleanup_errors)
        raise _LC3Error(detail) from error
    cleanup_errors = _cleanup_publication(states)
    if cleanup_errors:
        raise _LC3Error(
            "LC3 publication committed with verified byte-identical targets; cleanup residue: "
            + "; ".join(cleanup_errors)
        )


def build_lc3_explanation_artifacts(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    value_truth_path: Path,
    explanation_schema_path: Path,
    output_path: Path,
    *,
    renderer_copy_path: Path | None = None,
    readiness_path: Path | None = None,
) -> dict[str, Path]:
    """Validate LC3 truth and publish both canonical Explanation copies transactionally."""

    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_file = Path(asset_file).resolve()
    value_truth_path = Path(value_truth_path).resolve()
    output_path = Path(output_path).resolve()
    try:
        model = build_lc3_explanation(
            ir_path,
            slice_path,
            asset_file,
            value_truth_path,
            readiness_path=readiness_path,
        )
        validate_explanation_model(
            model,
            ir_path,
            slice_path,
            asset_file,
            Path(explanation_schema_path),
        )
        validate_lc3_value_explanation(
            model,
            ir_path,
            slice_path,
            asset_file,
            value_truth_path,
            readiness_path=readiness_path,
        )
    except (ExplanationModelError, _LC3Error, LC3ExplanationError) as error:
        raise LC3ExplanationError(str(error)) from error
    payload = canonical_explanation_bytes(model)
    targets = [output_path]
    outputs = {"explanation": output_path}
    if renderer_copy_path is not None:
        renderer_copy = Path(renderer_copy_path).resolve()
        targets.append(renderer_copy)
        outputs["renderer_copy"] = renderer_copy
    try:
        _publish_validated_payload(payload, targets)
    except _LC3Error as error:
        raise LC3ExplanationError(str(error)) from error
    return outputs
