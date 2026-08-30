"""Frozen slice-v1 serialization and atomic single-file publication for M4."""

from __future__ import annotations

from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Mapping, NoReturn

from ..execution_slice import NODE_REASON_VOCABULARY, ExecutionSlice
from ..m4_errors import M4ExecutionError
from ..raw_probe import BlueprintDocument
from ..schema_validation import validate_instance


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-slice-v1.schema.json"
_BOUNDARY_STATUSES = frozenset({"opaque", "uncertain", "unsupported"})


def _fail(
    code: str, message: str, *, cause: Exception | None = None
) -> NoReturn:
    raise M4ExecutionError(code, message, cause=cause)


def build_execution_slice_value(
    document: BlueprintDocument,
    result: ExecutionSlice,
    *,
    source_fixture: str,
    source_sha256: str,
    description: str,
) -> dict[str, Any]:
    """Map one accountable internal result onto only the frozen slice-v1 fields."""

    del document
    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": source_fixture,
        "source_sha256": source_sha256.upper(),
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": result.graph_id,
            "node_id": result.criterion_node_id,
            "description": description,
        },
        "graph_id": result.graph_id,
        "node_ids": list(result.node_ids),
        "edge_ids": list(result.edge_ids),
        "inclusion_reasons": {
            node_id: list(result.inclusion_reasons[node_id])
            for node_id in result.node_ids
        },
        "boundaries": [asdict(boundary) for boundary in result.boundaries],
        "counts": {"nodes": len(result.node_ids), "edges": len(result.edge_ids)},
    }


def canonical_execution_json_bytes(value: Mapping[str, Any]) -> bytes:
    """Return the canonical UTF-8, LF, sorted-key encoding for one slice value."""

    try:
        return (
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        _fail(
            "M4_SLICE_SCHEMA_INVALID",
            f"execution slice cannot be canonically serialized: {error}",
            cause=error,
        )


def _load_schema() -> Mapping[str, Any]:
    try:
        value = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            "M4_SLICE_SCHEMA_INVALID",
            f"cannot load frozen slice schema {_SCHEMA_PATH}: {error}",
            cause=error,
        )
    if not isinstance(value, dict):
        _fail("M4_SLICE_SCHEMA_INVALID", "frozen slice schema root is not an object")
    return value


def _validate_schema(value: Mapping[str, Any]) -> None:
    try:
        validate_instance(value, _load_schema())
    except M4ExecutionError:
        raise
    except Exception as error:
        _fail(
            "M4_SLICE_SCHEMA_INVALID",
            f"frozen slice schema rejected value: {error}",
            cause=error,
        )


def _validate_source(value: Mapping[str, Any], source_path: Path) -> None:
    try:
        actual = hashlib.sha256(source_path.read_bytes()).hexdigest().upper()
    except OSError as error:
        _fail(
            "M4_SOURCE_INVALID",
            f"cannot hash slice source {source_path}: {error}",
            cause=error,
        )
    if value.get("source_sha256") != actual:
        _fail(
            "M4_SOURCE_INVALID",
            "slice source SHA-256 mismatch: "
            f"declared {value.get('source_sha256')}, actual {actual}",
        )


def _validate_semantics(
    document: BlueprintDocument,
    value: Mapping[str, Any],
) -> None:
    try:
        criterion = value["criterion"]
        if not isinstance(criterion, Mapping):
            raise ValueError("criterion must be an object")
        graph_id = criterion.get("graph_id")
        graphs = [graph for graph in document.graphs if graph.id == graph_id]
        if len(graphs) != 1:
            raise ValueError(f"criterion graph must resolve exactly once: {graph_id!r}")
        graph = graphs[0]
        if value.get("graph_id") != graph.id:
            raise ValueError("slice graph_id differs from criterion graph_id")

        graph_nodes = {node.id: node for node in graph.nodes}
        graph_edges = {edge.id: edge for edge in graph.edges}
        if len(graph_nodes) != len(graph.nodes) or len(graph_edges) != len(graph.edges):
            raise ValueError("source graph contains duplicate node or edge identities")
        criterion_node_id = criterion.get("node_id")
        criterion_node = graph_nodes.get(str(criterion_node_id))
        if criterion_node is None:
            raise ValueError("criterion node is not in its declared graph")
        if not any(pin.kind == "execution" for pin in criterion_node.pins):
            raise ValueError("execution criterion has no execution pin")

        node_ids = value["node_ids"]
        edge_ids = value["edge_ids"]
        if not isinstance(node_ids, list) or not isinstance(edge_ids, list):
            raise ValueError("node_ids and edge_ids must be arrays")
        if node_ids != sorted(node_ids) or edge_ids != sorted(edge_ids):
            raise ValueError("node_ids and edge_ids must be deterministically sorted")
        selected = set(node_ids)
        missing_nodes = selected - set(graph_nodes)
        missing_edges = set(edge_ids) - set(graph_edges)
        if missing_nodes or missing_edges:
            raise ValueError(
                f"slice references unknown entities: nodes={sorted(missing_nodes)}, "
                f"edges={sorted(missing_edges)}"
            )
        induced = {
            edge.id
            for edge in graph.edges
            if edge.source_node_id in selected and edge.target_node_id in selected
        }
        if set(edge_ids) != induced:
            raise ValueError("edge_ids are not the exact selected-node induced edges")

        counts = value["counts"]
        if not isinstance(counts, Mapping) or counts.get("nodes") != len(node_ids) or counts.get(
            "edges"
        ) != len(edge_ids):
            raise ValueError("declared node/edge counts disagree with membership")

        reasons = value["inclusion_reasons"]
        if not isinstance(reasons, Mapping) or set(reasons) != selected:
            raise ValueError("inclusion reason keys must exactly equal node_ids")
        for node_id in node_ids:
            node_reasons = reasons[node_id]
            if (
                not isinstance(node_reasons, list)
                or not node_reasons
                or node_reasons != sorted(node_reasons)
                or not set(node_reasons) <= NODE_REASON_VOCABULARY
            ):
                raise ValueError(f"node {node_id} has invalid inclusion reasons")
        criterion_markers = {
            node_id for node_id, reasons_for_node in reasons.items()
            if "criterion" in reasons_for_node
        }
        if criterion_markers != {criterion_node_id}:
            raise ValueError("exactly the criterion node must carry criterion reason")

        expected_boundaries = [
            {
                "node_id": node_id,
                "status": graph_nodes[node_id].semantic_status,
                "reason": graph_nodes[node_id].semantic_reason,
            }
            for node_id in node_ids
            if graph_nodes[node_id].semantic_status in _BOUNDARY_STATUSES
        ]
        if value["boundaries"] != expected_boundaries:
            raise ValueError("boundaries disagree with selected non-supported nodes")
    except M4ExecutionError:
        raise
    except Exception as error:
        _fail(
            "M4_SLICE_INVARIANT_FAILED",
            f"execution slice disagrees with source document: {error}",
            cause=error,
        )


def validate_execution_slice_value(
    document: BlueprintDocument,
    value: Mapping[str, Any],
    *,
    source_path: str | Path,
) -> None:
    """Validate frozen shape, source hash and graph-local semantic agreement."""

    if not isinstance(value, Mapping):
        _fail("M4_SLICE_SCHEMA_INVALID", "execution slice root must be an object")
    _validate_schema(value)
    _validate_source(value, Path(source_path))
    _validate_semantics(document, value)


def _temporary_sibling(destination: Path) -> Path:
    try:
        handle, raw_path = tempfile.mkstemp(
            prefix=f".{destination.name}.",
            suffix=".tmp",
            dir=destination.parent,
        )
    except OSError as error:
        _fail(
            "M4_PUBLISH_FAILED",
            f"cannot create temporary slice beside {destination}: {error}",
            cause=error,
        )
    temporary = Path(raw_path)
    try:
        os.close(handle)
    except OSError as error:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        _fail(
            "M4_PUBLISH_FAILED",
            f"cannot close temporary slice beside {destination}: {error}",
            cause=error,
        )
    return temporary


def _remove_owned_temporary(path: Path) -> None:
    try:
        path.unlink(missing_ok=True)
    except OSError:
        pass


def publish_execution_slice(
    destination: str | Path,
    document: BlueprintDocument,
    value: Mapping[str, Any],
    *,
    source_path: str | Path,
) -> str:
    """Publish one valid slice atomically and return its lowercase SHA-256."""

    target = Path(destination)
    payload = canonical_execution_json_bytes(value)
    temporary = _temporary_sibling(target)
    try:
        try:
            temporary.write_bytes(payload)
            reopened = json.loads(temporary.read_text(encoding="utf-8"))
            if not isinstance(reopened, dict):
                raise TypeError("persisted slice root must be an object")
        except (OSError, UnicodeError, json.JSONDecodeError, TypeError) as error:
            _fail(
                "M4_PUBLISH_FAILED",
                f"cannot persist and reopen temporary slice {temporary}: {error}",
                cause=error,
            )

        validate_execution_slice_value(
            document,
            reopened,
            source_path=source_path,
        )
        try:
            os.replace(temporary, target)
        except OSError as error:
            _fail(
                "M4_PUBLISH_FAILED",
                f"cannot atomically replace slice destination {target}: {error}",
                cause=error,
            )
    finally:
        _remove_owned_temporary(temporary)
    return hashlib.sha256(payload).hexdigest()
