"""Frozen slice-v1 serialization and atomic single-file publication for M5."""

from __future__ import annotations

from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import tempfile
from typing import Any, Mapping, NoReturn

from ..data_slice import MemberVariableDataSlice, compute_member_variable_data_slice
from ..m5_errors import M5DataError
from ..raw_probe import BlueprintDocument
from ..schema_validation import validate_instance


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-slice-v1.schema.json"
_ORIGINAL_OS_CLOSE = os.close


def _fail(
    code: str,
    message: str,
    *,
    cause: Exception | None = None,
) -> NoReturn:
    raise M5DataError(code, message, cause=cause)


def build_member_data_slice_value(
    document: BlueprintDocument,
    result: MemberVariableDataSlice,
    *,
    source_fixture: str,
    source_sha256: str,
    question: str,
) -> dict[str, Any]:
    """Map one accountable internal result onto only frozen slice-v1 fields."""

    del document
    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": source_fixture,
        "source_sha256": source_sha256.upper(),
        "slice_kind": "member_variable_data_dependency",
        "criterion": {
            "graph_id": result.graph_id,
            "member_guid": result.member_guid,
            "member_name": result.member_name,
            "question": question,
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


def canonical_data_slice_json_bytes(value: Mapping[str, Any]) -> bytes:
    """Return the canonical UTF-8, LF, sorted-key encoding for one slice value."""

    try:
        return (
            json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        _fail(
            "M5_SLICE_SCHEMA_INVALID",
            f"member data slice cannot be canonically serialized: {error}",
            cause=error,
        )


def _load_schema() -> Mapping[str, Any]:
    try:
        value = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            "M5_SLICE_SCHEMA_INVALID",
            f"cannot load frozen slice schema {_SCHEMA_PATH}: {error}",
            cause=error,
        )
    if not isinstance(value, dict):
        _fail(
            "M5_SLICE_SCHEMA_INVALID",
            "frozen slice schema root is not an object",
        )
    return value


def _validate_schema(value: Mapping[str, Any]) -> None:
    try:
        validate_instance(value, _load_schema())
    except M5DataError:
        raise
    except Exception as error:
        _fail(
            "M5_SLICE_SCHEMA_INVALID",
            f"frozen slice schema rejected value: {error}",
            cause=error,
        )


def _validate_source(value: Mapping[str, Any], source_path: Path) -> None:
    try:
        payload = source_path.read_bytes()
        source_value = json.loads(payload.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            "M5_SOURCE_INVALID",
            f"cannot read slice source {source_path}: {error}",
            cause=error,
        )
    actual = hashlib.sha256(payload).hexdigest().upper()
    if value.get("source_sha256") != actual:
        _fail(
            "M5_SOURCE_INVALID",
            "slice source SHA-256 mismatch: "
            f"declared {value.get('source_sha256')}, actual {actual}",
        )
    if not isinstance(source_value, dict):
        _fail("M5_SOURCE_INVALID", "slice source root must be an object")


def _relative_source_fixture(value: Any) -> bool:
    if not isinstance(value, str) or not value.strip():
        return False
    posix = PurePosixPath(value)
    windows = PureWindowsPath(value)
    return not (
        posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
    )


def _validate_semantics(
    document: BlueprintDocument,
    value: Mapping[str, Any],
) -> None:
    try:
        if value.get("format") != "blueprint-lens-slice":
            raise ValueError("slice format is invalid")
        if value.get("schema_version") != "1.0.0":
            raise ValueError("slice schema version is invalid")
        if value.get("rules_version") != "1.0.0":
            raise ValueError("slice rules version is invalid")
        if value.get("slice_kind") != "member_variable_data_dependency":
            raise ValueError("slice kind is not member_variable_data_dependency")
        if not _relative_source_fixture(value.get("source_fixture")):
            raise ValueError("source_fixture must be non-empty and relative")

        criterion = value["criterion"]
        if not isinstance(criterion, Mapping):
            raise ValueError("criterion must be an object")
        graph_id = criterion.get("graph_id")
        member_guid = criterion.get("member_guid")
        member_name = criterion.get("member_name")
        question = criterion.get("question")
        if not isinstance(question, str) or not question.strip():
            raise ValueError("criterion question must be non-empty")

        try:
            result = compute_member_variable_data_slice(
                document,
                str(graph_id),
                str(member_guid),
            )
        except M5DataError as error:
            raise ValueError(f"slice recomputation failed: {error}") from error
        if result.member_name != member_name:
            raise ValueError("criterion member name disagrees with recomputation")
        if value.get("graph_id") != result.graph_id or graph_id != result.graph_id:
            raise ValueError("slice graph disagrees with recomputation")

        expected_node_ids = list(result.node_ids)
        expected_edge_ids = list(result.edge_ids)
        if value.get("node_ids") != expected_node_ids:
            raise ValueError("node membership differs from recomputation")
        if value.get("edge_ids") != expected_edge_ids:
            raise ValueError("edge membership differs from recomputation")
        expected_reasons = {
            node_id: list(result.inclusion_reasons[node_id])
            for node_id in result.node_ids
        }
        if value.get("inclusion_reasons") != expected_reasons:
            raise ValueError("node reasons differ from recomputation")
        reasons = value.get("inclusion_reasons")
        if not isinstance(reasons, Mapping) or list(reasons) != sorted(reasons):
            raise ValueError("node reason keys must be deterministically sorted")
        expected_boundaries = [asdict(boundary) for boundary in result.boundaries]
        if value.get("boundaries") != expected_boundaries:
            raise ValueError("boundaries differ from recomputation")
        if value.get("counts") != {
            "nodes": len(result.node_ids),
            "edges": len(result.edge_ids),
        }:
            raise ValueError("declared counts differ from recomputation")

        graphs = [graph for graph in document.graphs if graph.id == result.graph_id]
        if len(graphs) != 1:
            raise ValueError("result graph must resolve exactly once")
        graph = graphs[0]
        graph_edges = {edge.id: edge for edge in graph.edges}
        if len(graph_edges) != len(graph.edges):
            raise ValueError("source graph repeats edge identities")
        selected = set(result.node_ids)
        returned_edges = value.get("edge_ids")
        if not isinstance(returned_edges, list):
            raise ValueError("edge_ids must be an array")
        if any(edge_id not in graph_edges for edge_id in returned_edges):
            raise ValueError("slice contains an unknown edge")
        induced = {
            edge.id
            for edge in graph.edges
            if edge.source_node_id in selected and edge.target_node_id in selected
        }
        if set(returned_edges) != induced:
            raise ValueError("edge_ids are not the complete induced edge set")
    except M5DataError:
        raise
    except Exception as error:
        _fail(
            "M5_SLICE_INVARIANT_FAILED",
            f"member data slice disagrees with source document: {error}",
            cause=error,
        )


def validate_member_data_slice_value(
    document: BlueprintDocument,
    value: Mapping[str, Any],
    *,
    source_path: str | Path,
) -> None:
    """Validate source hash, frozen shape and graph-local semantic agreement."""

    if not isinstance(value, Mapping):
        _fail("M5_SLICE_SCHEMA_INVALID", "member data slice root must be an object")
    _validate_source(value, Path(source_path))
    _validate_schema(value)
    _validate_semantics(document, value)


def _remove_owned_temporary(path: Path) -> None:
    try:
        path.unlink(missing_ok=True)
    except OSError:
        pass


def _temporary_sibling(destination: Path) -> Path:
    try:
        handle, raw_path = tempfile.mkstemp(
            prefix=f".{destination.name}.",
            suffix=".tmp",
            dir=destination.parent,
        )
    except OSError as error:
        _fail(
            "M5_PUBLISH_FAILED",
            f"cannot create temporary slice beside {destination}: {error}",
            cause=error,
        )
    temporary = Path(raw_path)
    try:
        os.close(handle)
    except OSError as error:
        try:
            _ORIGINAL_OS_CLOSE(handle)
        except OSError:
            pass
        _remove_owned_temporary(temporary)
        _fail(
            "M5_PUBLISH_FAILED",
            f"cannot close temporary slice beside {destination}: {error}",
            cause=error,
        )
    return temporary


def publish_member_data_slice(
    destination: str | Path,
    document: BlueprintDocument,
    value: Mapping[str, Any],
    *,
    source_path: str | Path,
) -> str:
    """Publish one valid M5 slice atomically and return its lowercase SHA-256."""

    target = Path(destination)
    payload = canonical_data_slice_json_bytes(value)
    temporary = _temporary_sibling(target)
    try:
        try:
            temporary.write_bytes(payload)
            reopened = json.loads(temporary.read_text(encoding="utf-8"))
            if not isinstance(reopened, dict):
                raise TypeError("persisted slice root must be an object")
        except (OSError, UnicodeError, json.JSONDecodeError, TypeError) as error:
            _fail(
                "M5_PUBLISH_FAILED",
                f"cannot persist and reopen temporary slice {temporary}: {error}",
                cause=error,
            )

        validate_member_data_slice_value(
            document,
            reopened,
            source_path=source_path,
        )
        try:
            os.replace(temporary, target)
        except OSError as error:
            _fail(
                "M5_PUBLISH_FAILED",
                f"cannot atomically replace slice destination {target}: {error}",
                cause=error,
            )
    finally:
        _remove_owned_temporary(temporary)
    return hashlib.sha256(payload).hexdigest()
