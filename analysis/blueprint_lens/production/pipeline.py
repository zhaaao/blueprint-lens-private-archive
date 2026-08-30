"""Deterministic external raw-probe to Blueprint Lens v1 orchestration."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Mapping, Sequence

from ..raw_probe import load_blueprint_lens_v1, load_raw_probe
from ..schema_validation import validate_instance
from ..typed_ir import build_typed_ir


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-v1.schema.json"
_RAW_FORMAT = "blueprint-lens-raw-probe"
_RAW_VERSION = "0.2"


@dataclass(frozen=True)
class PipelineItem:
    asset_id: str
    blueprint_object_path: str
    raw_path: Path
    typed_ir_path: Path


@dataclass(frozen=True)
class PipelineResult:
    asset_id: str
    raw_sha256: str
    typed_ir_sha256: str
    graph_count: int
    node_count: int
    pin_count: int
    edge_count: int


class ProductionPipelineError(ValueError):
    """A production-pipeline failure with a stable machine-readable code."""

    def __init__(self, code: str, message: str, *, cause: Exception | None = None) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _load_raw_json(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        raw_bytes = path.read_bytes()
        decoded = raw_bytes.decode("utf-8")
        raw = json.loads(decoded)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProductionPipelineError(
            "M3_RAW_JSON_INVALID", f"cannot decode raw JSON {path}: {error}", cause=error
        ) from error
    if not isinstance(raw, dict):
        error = ValueError("raw JSON root must be an object")
        raise ProductionPipelineError(
            "M3_RAW_JSON_INVALID", f"cannot decode raw JSON {path}: {error}", cause=error
        ) from error
    return raw, raw_bytes


def _load_schema() -> Mapping[str, Any]:
    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProductionPipelineError(
            "M3_TYPED_IR_SCHEMA_INVALID",
            f"cannot read typed-IR schema {_SCHEMA_PATH}: {error}",
            cause=error,
        ) from error
    if not isinstance(schema, dict):
        error = ValueError("typed-IR schema root must be an object")
        raise ProductionPipelineError(
            "M3_TYPED_IR_SCHEMA_INVALID",
            f"cannot read typed-IR schema {_SCHEMA_PATH}: {error}",
            cause=error,
        ) from error
    return schema


def _temporary_sibling(destination: Path) -> Path:
    try:
        handle, raw_path = tempfile.mkstemp(
            prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
        )
    except OSError as error:
        raise ProductionPipelineError(
            "M3_TYPED_IR_WRITE_FAILED",
            f"cannot create typed-IR temporary output beside {destination}: {error}",
            cause=error,
        ) from error
    temporary = Path(raw_path)
    try:
        os.close(handle)
    except OSError as error:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise ProductionPipelineError(
            "M3_TYPED_IR_WRITE_FAILED",
            f"cannot close typed-IR temporary output beside {destination}: {error}",
            cause=error,
        ) from error
    return temporary


def _remove_temporary(path: Path) -> None:
    try:
        path.unlink(missing_ok=True)
    except OSError:
        pass


def build_item(item: PipelineItem) -> PipelineResult:
    """Build one raw ``0.2`` input into a validated, canonical typed IR file."""

    raw, raw_bytes = _load_raw_json(item.raw_path)
    try:
        load_raw_probe(item.raw_path)
        if raw.get("format") != _RAW_FORMAT or raw.get("format_version") != _RAW_VERSION:
            raise ValueError("production pipeline requires raw probe 0.2")
    except Exception as error:
        if isinstance(error, ProductionPipelineError):
            raise
        raise ProductionPipelineError(
            "M3_RAW_CONTRACT_INVALID",
            f"raw contract rejected {item.raw_path}: {error}",
            cause=error,
        ) from error

    try:
        typed_ir = build_typed_ir(
            raw, expected_blueprint_path=item.blueprint_object_path
        )
    except Exception as error:
        raise ProductionPipelineError(
            "M3_TYPED_IR_BUILD_FAILED",
            f"typed-IR build failed for {item.asset_id}: {error}",
            cause=error,
        ) from error

    if not isinstance(typed_ir, Mapping):
        error = ValueError("typed-IR builder returned a non-object")
        raise ProductionPipelineError(
            "M3_TYPED_IR_SCHEMA_INVALID",
            f"typed-IR schema rejected {item.asset_id}: {error}",
            cause=error,
        ) from error

    try:
        payload = _canonical_json_bytes(typed_ir)
    except (TypeError, ValueError) as error:
        raise ProductionPipelineError(
            "M3_TYPED_IR_SCHEMA_INVALID",
            f"typed-IR serialization rejected {item.asset_id}: {error}",
            cause=error,
        ) from error

    temporary = _temporary_sibling(item.typed_ir_path)
    try:
        try:
            temporary.write_bytes(payload)
            persisted = json.loads(temporary.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise ProductionPipelineError(
                "M3_TYPED_IR_WRITE_FAILED",
                f"cannot persist typed IR for {item.asset_id}: {error}",
                cause=error,
            ) from error

        try:
            validate_instance(persisted, _load_schema())
            document = load_blueprint_lens_v1(temporary)
        except Exception as error:
            if isinstance(error, ProductionPipelineError):
                raise
            raise ProductionPipelineError(
                "M3_TYPED_IR_SCHEMA_INVALID",
                f"typed-IR schema rejected {item.asset_id}: {error}",
                cause=error,
            ) from error

        try:
            os.replace(temporary, item.typed_ir_path)
            final_bytes = item.typed_ir_path.read_bytes()
        except OSError as error:
            raise ProductionPipelineError(
                "M3_TYPED_IR_WRITE_FAILED",
                f"cannot publish typed IR for {item.asset_id}: {error}",
                cause=error,
            ) from error
    finally:
        _remove_temporary(temporary)

    return PipelineResult(
        asset_id=item.asset_id,
        raw_sha256=hashlib.sha256(raw_bytes).hexdigest(),
        typed_ir_sha256=hashlib.sha256(final_bytes).hexdigest(),
        graph_count=len(document.graphs),
        node_count=len(document.nodes),
        pin_count=len(document.pins),
        edge_count=len(document.edges),
    )


def build_batch(items: Sequence[PipelineItem]) -> tuple[PipelineResult, ...]:
    """Build a deterministic asset-id-sorted batch after duplicate preflight."""

    asset_ids = [item.asset_id for item in items]
    if len(asset_ids) != len(set(asset_ids)):
        error = ValueError("duplicate asset_id values are not allowed")
        raise ProductionPipelineError(
            "M3_DUPLICATE_ASSET_ID", str(error), cause=error
        ) from error
    return tuple(build_item(item) for item in sorted(items, key=lambda item: item.asset_id))
