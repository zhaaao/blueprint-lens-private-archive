"""Compose immutable frozen typed documents above the M3 provider seam."""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping

from ..m4_errors import M4ExecutionError
from ..raw_probe import (
    BlueprintDocument,
    Graph,
    Node,
    Pin,
    reconstruct_blueprint_lens_v1,
)
from ..schema_validation import validate_instance
from ..typed_ir import build_typed_ir
from .project_documents import AssetProvenance, ProjectDocument


_ROOT = Path(__file__).resolve().parents[3]
_TYPED_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-v1.schema.json"


@dataclass(frozen=True, slots=True)
class TypedProjectDocument:
    document: BlueprintDocument
    raw_path: Path
    raw_sha256: str
    typed_ir_sha256: str
    provenance: AssetProvenance


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _load_typed_schema() -> Mapping[str, Any]:
    try:
        value = json.loads(_TYPED_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise M4ExecutionError(
            "M4_TYPED_DOCUMENT_INVALID",
            f"cannot read typed-IR schema {_TYPED_SCHEMA_PATH}: {error}",
            cause=error,
        ) from error
    if not isinstance(value, dict):
        error = ValueError("typed-IR schema root must be an object")
        raise M4ExecutionError(
            "M4_TYPED_DOCUMENT_INVALID",
            f"cannot read typed-IR schema {_TYPED_SCHEMA_PATH}: {error}",
            cause=error,
        ) from error
    return value


def _deep_freeze(value: Any) -> Any:
    if isinstance(value, Mapping):
        return MappingProxyType({key: _deep_freeze(item) for key, item in value.items()})
    if isinstance(value, (list, tuple)):
        return tuple(_deep_freeze(item) for item in value)
    return value


def _freeze_pin(pin: Pin) -> Pin:
    return replace(pin, type=_deep_freeze(pin.type), default=_deep_freeze(pin.default))


def _freeze_node(node: Node) -> Node:
    return replace(
        node,
        symbol=None if node.symbol is None else _deep_freeze(node.symbol),
        pins=tuple(_freeze_pin(pin) for pin in node.pins),
    )


def _freeze_graph(graph: Graph) -> Graph:
    return replace(graph, nodes=tuple(_freeze_node(node) for node in graph.nodes))


def _freeze_blueprint_document(document: BlueprintDocument) -> BlueprintDocument:
    return replace(document, graphs=tuple(_freeze_graph(graph) for graph in document.graphs))


def compose_typed_document(project_document: ProjectDocument) -> TypedProjectDocument:
    """Reopen, hash, promote, validate and reconstruct one provider document."""

    try:
        raw_bytes = project_document.raw_path.read_bytes()
        if hashlib.sha256(raw_bytes).hexdigest() != project_document.raw_sha256:
            raise ValueError("raw source hash changed after provider validation")
        raw_value = json.loads(raw_bytes.decode("utf-8"))
        if not isinstance(raw_value, dict):
            raise ValueError("raw source root must be an object")
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        raise M4ExecutionError(
            "M4_SOURCE_INVALID",
            f"cannot reopen provider source {project_document.raw_path}: {error}",
            cause=error,
        ) from error

    try:
        typed_value = build_typed_ir(
            raw_value,
            expected_blueprint_path=project_document.provenance.blueprint_object_path,
        )
        validate_instance(typed_value, _load_typed_schema())
        document = reconstruct_blueprint_lens_v1(typed_value)
        typed_bytes = _canonical_json_bytes(typed_value)
    except M4ExecutionError:
        raise
    except Exception as error:
        raise M4ExecutionError(
            "M4_TYPED_DOCUMENT_INVALID",
            f"typed document rejected {project_document.raw_path.name}: {error}",
            cause=error,
        ) from error

    return TypedProjectDocument(
        document=_freeze_blueprint_document(document),
        raw_path=project_document.raw_path,
        raw_sha256=project_document.raw_sha256,
        typed_ir_sha256=hashlib.sha256(typed_bytes).hexdigest(),
        provenance=project_document.provenance,
    )
