"""Thin M6 composition and atomic publication for semantic session packets."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time
from typing import Any, Mapping, NoReturn, cast

from ..data_slice import compute_member_variable_data_slice
from ..execution_slice import compute_execution_slice
from ..m4_errors import M4ExecutionError
from ..m5_errors import M5DataError
from ..m6_errors import M6Error
from ..raw_probe import load_raw_probe, reconstruct_blueprint_lens_v1
from ..schema_validation import validate_instance
from ..typed_ir import build_typed_ir
from .data_products import (
    build_member_data_slice_value,
    validate_member_data_slice_value,
)
from .execution_products import (
    build_execution_slice_value,
    validate_execution_slice_value,
)
from .session_contracts import (
    DataSessionCriterion,
    ExecutionSessionCriterion,
    SessionRequest,
    canonical_json_bytes,
    load_session_request,
)
from .session_explanation import (
    build_session_explanation,
    derive_presentation_visibility,
    validate_baseline_facts,
)


_ROOT = Path(__file__).resolve().parents[3]
_DEFAULT_SCHEMA_ROOT = _ROOT / "schemas"
_MANIFEST_SCHEMA_NAME = "blueprint-lens-m6-session-manifest-v1.schema.json"
_TYPED_SCHEMA_NAME = "blueprint-lens-v1.schema.json"
_EXPLANATION_SCHEMA_NAME = "blueprint-lens-explanation-v1.schema.json"
_BASELINE_SCHEMA_NAME = "blueprint-lens-m6-baseline-facts-v1.schema.json"
_FILE_RECORDS = (
    ("request", "request.json"),
    ("raw_source", "raw-source.json"),
    ("typed_source", "typed-source.json"),
    ("slice", "slice.json"),
    ("explanation", "explanation.json"),
    ("baseline_facts", "baseline-facts.json"),
)
PACKET_FILES = tuple(name for _, name in _FILE_RECORDS) + ("manifest.json",)


@dataclass(frozen=True, slots=True)
class SessionPacketResult:
    output_dir: Path
    semantic_sha256: str
    manifest: Mapping[str, object]


def _fail(
    code: str,
    message: str,
    *,
    phase: str,
    retryable: bool = False,
    diagnostics: Mapping[str, object] | None = None,
    cause: Exception | None = None,
) -> NoReturn:
    raise M6Error(
        code,
        message,
        phase=phase,
        retryable=retryable,
        diagnostics=diagnostics,
        cause=cause,
    )


class _StageTrace:
    _ERROR_CODES = {
        "typed_document": "M6_PIPELINE_TYPED_DOCUMENT_INVALID",
        "slice": "M6_PIPELINE_SLICE_FAILED",
        "explanation": "M6_PIPELINE_EXPLANATION_FAILED",
    }

    def __init__(self, path: Path | None) -> None:
        self._path = Path(path) if path is not None else None
        self._rows: list[dict[str, object]] = []

    def run(self, stage: str, operation: Any) -> Any:
        if self._path is None:
            return operation()
        started_seconds = time.perf_counter()
        started_timestamp = datetime.now(timezone.utc).isoformat().replace(
            "+00:00", "Z"
        )
        error_code = ""
        try:
            return operation()
        except M6Error as error:
            error_code = error.code
            raise
        except Exception:
            error_code = self._ERROR_CODES[stage]
            raise
        finally:
            self._rows.append(
                {
                    "stage": stage,
                    "start_timestamp": started_timestamp,
                    "result_timestamp": datetime.now(timezone.utc)
                    .isoformat()
                    .replace("+00:00", "Z"),
                    "duration_ms": max(
                        0.0, (time.perf_counter() - started_seconds) * 1000.0
                    ),
                    "error_code": error_code,
                }
            )
            self._path.parent.mkdir(parents=True, exist_ok=True)
            self._path.write_bytes(
                canonical_json_bytes(
                    {"schema_version": "1.0.0", "stages": self._rows}
                )
            )


def _object(value: Any, context: str, code: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        _fail(code, f"{context} must be an object", phase="packet_validation")
    return cast(dict[str, Any], value)


def _read_json(path: Path, *, code: str, context: str) -> dict[str, Any]:
    try:
        payload = path.read_bytes()
        if payload.startswith(b"\xef\xbb\xbf"):
            _fail(code, f"{context} must not contain a UTF-8 BOM", phase="packet_validation")
        return _object(json.loads(payload.decode("utf-8")), context, code)
    except M6Error:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            code,
            f"cannot read {context}: {error}",
            phase="packet_validation",
            cause=error,
        )


def _load_schema(path: Path, *, code: str) -> Mapping[str, Any]:
    return _read_json(path, code=code, context=f"schema {path.name}")


def _load_manifest_schema(schema_root: Path) -> Mapping[str, Any]:
    return _load_schema(
        schema_root / _MANIFEST_SCHEMA_NAME,
        code="M6_PACKET_SCHEMA_INVALID",
    )


def _validate_schema(
    value: Mapping[str, Any],
    schema_path: Path,
    *,
    code: str,
    context: str,
) -> None:
    try:
        validate_instance(value, _load_schema(schema_path, code=code))
    except M6Error:
        raise
    except Exception as error:
        _fail(
            code,
            f"{context} violates its schema: {error}",
            phase="packet_validation",
            cause=error,
        )


def _write_packet_file(path: Path, payload: bytes) -> None:
    path.write_bytes(payload)


def _remove_owned_tree(path: Path) -> None:
    try:
        if path.exists():
            shutil.rmtree(path)
    except OSError:
        pass


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _file_records(payloads: Mapping[str, bytes]) -> list[dict[str, str]]:
    return [
        {
            "role": role,
            "path": name,
            "sha256": _sha256(payloads[name]),
        }
        for role, name in _FILE_RECORDS
    ]


def _build_slice(
    document: Any,
    request: SessionRequest,
    typed_sha256: str,
) -> dict[str, Any]:
    try:
        if isinstance(request.criterion, ExecutionSessionCriterion):
            result = compute_execution_slice(
                document,
                request.criterion.criterion_node_id,
            )
            return build_execution_slice_value(
                document,
                result,
                source_fixture="typed-source.json",
                source_sha256=typed_sha256,
                description="Why does the selected execution criterion execute?",
            )
        if not isinstance(request.criterion, DataSessionCriterion):
            raise ValueError("request criterion union is invalid")
        result = compute_member_variable_data_slice(
            document,
            request.graph_id,
            request.criterion.member_guid,
        )
        if result.member_name != request.criterion.expected_member_name:
            raise ValueError("resolved member name disagrees with request")
        return build_member_data_slice_value(
            document,
            result,
            source_fixture="typed-source.json",
            source_sha256=typed_sha256,
            question=(
                "Where does the value assigned to "
                f"{request.criterion.expected_member_name} come from?"
            ),
        )
    except (M4ExecutionError, M5DataError, ValueError) as error:
        _fail(
            "M6_PIPELINE_SLICE_FAILED",
            f"accepted slice pipeline failed: {error}",
            phase="pipeline",
            cause=error,
        )


def _compose_payloads(
    request_path: Path,
    raw_source_path: Path,
    schema_root: Path,
    stage_trace: _StageTrace,
) -> tuple[dict[str, bytes], SessionRequest, dict[str, Any], dict[str, Any]]:
    request = load_session_request(request_path)
    request_value = _read_json(
        request_path,
        code="M6_PRECONDITION_QUERY_INVALID",
        context="M6 request",
    )
    def compose_typed() -> tuple[dict[str, Any], dict[str, Any], Any]:
        try:
            raw_value = _read_json(
                raw_source_path,
                code="M6_PIPELINE_TYPED_DOCUMENT_INVALID",
                context="raw source",
            )
            load_raw_probe(raw_source_path)
            blueprint = raw_value.get("blueprint")
            if not isinstance(blueprint, Mapping):
                raise ValueError("raw blueprint must be an object")
            if blueprint.get("path") != request.asset_path:
                _fail(
                    "M6_EXPORT_SOURCE_MISMATCH",
                    "raw Blueprint path disagrees with the request",
                    phase="export",
                )
            typed_value = build_typed_ir(
                raw_value,
                expected_blueprint_path=request.asset_path,
            )
            _validate_schema(
                typed_value,
                schema_root / _TYPED_SCHEMA_NAME,
                code="M6_PIPELINE_TYPED_DOCUMENT_INVALID",
                context="typed source",
            )
            document = reconstruct_blueprint_lens_v1(typed_value)
            return raw_value, typed_value, document
        except M6Error:
            raise
        except Exception as error:
            _fail(
                "M6_PIPELINE_TYPED_DOCUMENT_INVALID",
                f"typed document pipeline failed: {error}",
                phase="pipeline",
                cause=error,
            )

    raw_value, typed_value, document = stage_trace.run(
        "typed_document", compose_typed
    )

    request_payload = canonical_json_bytes(request_value)
    raw_payload = canonical_json_bytes(raw_value)
    typed_payload = canonical_json_bytes(typed_value)
    typed_sha256 = _sha256(typed_payload)
    slice_value = stage_trace.run(
        "slice", lambda: _build_slice(document, request, typed_sha256)
    )
    observed_nodes = len(slice_value["node_ids"])
    observed_relations = len(slice_value["edge_ids"])
    if observed_nodes > request.semantic_budget.max_selected_nodes:
        _fail(
            "M6_PIPELINE_BUDGET_EXCEEDED",
            "selected nodes exceed the semantic budget",
            phase="pipeline",
            diagnostics={
                "dimension": "selected_nodes",
                "observed": observed_nodes,
                "declared": request.semantic_budget.max_selected_nodes,
            },
        )
    if observed_relations > request.semantic_budget.max_selected_relations:
        _fail(
            "M6_PIPELINE_BUDGET_EXCEEDED",
            "selected relations exceed the semantic budget",
            phase="pipeline",
            diagnostics={
                "dimension": "selected_relations",
                "observed": observed_relations,
                "declared": request.semantic_budget.max_selected_relations,
            },
        )
    slice_payload = canonical_json_bytes(slice_value)
    enriched_typed = dict(typed_value)
    enriched_typed["_m6_source_fingerprint"] = request.source_fingerprint
    products = stage_trace.run(
        "explanation",
        lambda: build_session_explanation(
            enriched_typed,
            slice_value,
            query_kind=request.query_kind,
            renderer_id=request.renderer_id,
            presentation_budget=(
                request.presentation_budget.max_visible_entities,
                request.presentation_budget.max_visible_relations,
            ),
        ),
    )
    explanation_payload = canonical_json_bytes(products.explanation)
    baseline_payload = canonical_json_bytes(products.baseline_facts)
    payloads = {
        "request.json": request_payload,
        "raw-source.json": raw_payload,
        "typed-source.json": typed_payload,
        "slice.json": slice_payload,
        "explanation.json": explanation_payload,
        "baseline-facts.json": baseline_payload,
    }
    return payloads, request, slice_value, dict(products.baseline_facts)


def build_session_packet(
    *,
    request_path: Path,
    raw_source_path: Path,
    output_dir: Path,
    schema_root: Path,
    stage_trace_path: Path | None = None,
) -> SessionPacketResult:
    """Compose, validate, and atomically publish exactly one seven-file packet."""

    request_path = Path(request_path)
    raw_source_path = Path(raw_source_path)
    output_dir = Path(output_dir).resolve()
    schema_root = Path(schema_root).resolve()
    if output_dir.exists():
        _fail(
            "M6_PACKET_OUTPUT_EXISTS",
            f"packet output already exists: {output_dir.name}",
            phase="publish",
        )
    stage_trace = _StageTrace(stage_trace_path)
    payloads, request, slice_value, _ = _compose_payloads(
        request_path,
        raw_source_path,
        schema_root,
        stage_trace,
    )
    records = _file_records(payloads)
    semantic_sha256 = _sha256(canonical_json_bytes(records))
    manifest: dict[str, Any] = {
        "format": "blueprint-lens-m6-session-manifest",
        "schema_version": "1.0.0",
        "generator_version": "m6-session-pipeline-v1",
        "query_kind": request.query_kind,
        "source_fingerprint": request.source_fingerprint,
        "renderer_id": request.renderer_id,
        "versions": {
            "raw": request.raw_version,
            "typed_ir": request.typed_ir_version,
            "slice_rules": request.slice_rules_version,
            "explanation": "1.0.0",
            "baseline_facts": "1.0.0",
        },
        "semantic_sha256": semantic_sha256,
        "files": records,
        "counts": {
            "bound_files": 6,
            "packet_files": 7,
            "selected_entities": slice_value["counts"]["nodes"],
            "selected_relations": slice_value["counts"]["edges"],
        },
    }
    manifest_payload = canonical_json_bytes(manifest)

    staging: Path | None = None
    published = False
    try:
        output_dir.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(
            tempfile.mkdtemp(
                prefix=f".{output_dir.name}.stage-",
                dir=output_dir.parent,
            )
        )
        for _, name in _FILE_RECORDS:
            _write_packet_file(staging / name, payloads[name])
        _load_manifest_schema(schema_root)
        _write_packet_file(staging / "manifest.json", manifest_payload)
        validate_session_packet(staging, _schema_root=schema_root)
        os.replace(staging, output_dir)
        published = True
        staging = None
        result = validate_session_packet(
            output_dir,
            expected_source_fingerprint=request.source_fingerprint,
            _schema_root=schema_root,
        )
        if result.semantic_sha256 != semantic_sha256 or result.manifest != manifest:
            raise ValueError("reopened packet differs from the staged packet")
        return result
    except M6Error as error:
        if error.code == "M6_PACKET_OUTPUT_EXISTS":
            raise
        if published:
            _remove_owned_tree(output_dir)
        _fail(
            "M6_PACKET_PUBLISH_FAILED",
            f"packet publication failed: {error}",
            phase="publish",
            cause=error,
        )
    except Exception as error:
        if published:
            _remove_owned_tree(output_dir)
        _fail(
            "M6_PACKET_PUBLISH_FAILED",
            f"packet publication failed: {error}",
            phase="publish",
            cause=error,
        )
    finally:
        if staging is not None:
            _remove_owned_tree(staging)


def _canonical_file(path: Path, *, code: str) -> dict[str, Any]:
    value = _read_json(path, code=code, context=path.name)
    try:
        expected = canonical_json_bytes(value)
        actual = path.read_bytes()
    except (OSError, TypeError, ValueError, UnicodeError) as error:
        _fail(
            code,
            f"cannot verify canonical bytes for {path.name}: {error}",
            phase="packet_validation",
            cause=error,
        )
    if actual != expected:
        _fail(
            code,
            f"packet file is not canonical JSON: {path.name}",
            phase="packet_validation",
        )
    return value


def validate_session_packet(
    packet_dir: Path,
    *,
    expected_source_fingerprint: str | None = None,
    _schema_root: Path | None = None,
) -> SessionPacketResult:
    """Reopen and validate all packet files without relying on generation state."""

    packet_dir = Path(packet_dir).resolve()
    schema_root = Path(_schema_root or _DEFAULT_SCHEMA_ROOT).resolve()
    try:
        actual_names = sorted(
            path.name for path in packet_dir.iterdir() if path.is_file()
        )
    except OSError as error:
        _fail(
            "M6_PACKET_REFERENCE_INVALID",
            f"cannot enumerate packet: {error}",
            phase="packet_validation",
            cause=error,
        )
    if actual_names != sorted(PACKET_FILES):
        _fail(
            "M6_PACKET_REFERENCE_INVALID",
            f"packet files must be exactly {list(PACKET_FILES)}",
            phase="packet_validation",
        )
    manifest = _canonical_file(
        packet_dir / "manifest.json",
        code="M6_PACKET_CANONICAL_INVALID",
    )
    try:
        validate_instance(manifest, _load_manifest_schema(schema_root))
    except M6Error:
        raise
    except Exception as error:
        _fail(
            "M6_PACKET_SCHEMA_INVALID",
            f"manifest violates its schema: {error}",
            phase="packet_validation",
            cause=error,
        )
    records = manifest.get("files")
    if not isinstance(records, list):
        _fail(
            "M6_PACKET_REFERENCE_INVALID",
            "manifest files must be an array",
            phase="packet_validation",
        )
    expected_role_paths = list(_FILE_RECORDS)
    actual_role_paths = [
        (record.get("role"), record.get("path"))
        for record in records
        if isinstance(record, Mapping)
    ]
    if actual_role_paths != expected_role_paths or len(records) != 6:
        _fail(
            "M6_PACKET_REFERENCE_INVALID",
            "manifest roles/paths are not exact, unique, and ordered",
            phase="packet_validation",
        )
    values: dict[str, dict[str, Any]] = {}
    for record in records:
        path = packet_dir / record["path"]
        values[record["role"]] = _canonical_file(
            path,
            code="M6_PACKET_CANONICAL_INVALID",
        )
        payload = path.read_bytes()
        if _sha256(payload) != record["sha256"]:
            _fail(
                "M6_PACKET_HASH_MISMATCH",
                f"packet hash mismatch: {record['path']}",
                phase="packet_validation",
            )
    expected_semantic_hash = _sha256(canonical_json_bytes(records))
    if manifest.get("semantic_sha256") != expected_semantic_hash:
        _fail(
            "M6_PACKET_HASH_MISMATCH",
            "aggregate semantic SHA-256 mismatch",
            phase="packet_validation",
        )

    request = load_session_request(packet_dir / "request.json")
    if (
        expected_source_fingerprint is not None
        and request.source_fingerprint != expected_source_fingerprint
    ):
        _fail(
            "M6_PACKET_SOURCE_STALE",
            "packet source fingerprint is stale",
            phase="packet_validation",
        )
    if manifest.get("source_fingerprint") != request.source_fingerprint:
        _fail(
            "M6_PACKET_SOURCE_STALE",
            "manifest/request source fingerprints disagree",
            phase="packet_validation",
        )
    try:
        raw_value = values["raw_source"]
        raw_blueprint = raw_value["blueprint"]
        if raw_blueprint["path"] != request.asset_path:
            raise ValueError("raw Blueprint path disagrees with request")
        typed_value = values["typed_source"]
        _validate_schema(
            typed_value,
            schema_root / _TYPED_SCHEMA_NAME,
            code="M6_PACKET_SCHEMA_INVALID",
            context="typed source",
        )
        expected_typed = build_typed_ir(
            raw_value,
            expected_blueprint_path=request.asset_path,
        )
        if typed_value != expected_typed:
            raise ValueError("typed source differs from accepted raw reconstruction")
        document = reconstruct_blueprint_lens_v1(typed_value)
        slice_value = values["slice"]
        if request.query_kind == "execution":
            validate_execution_slice_value(
                document,
                slice_value,
                source_path=packet_dir / "typed-source.json",
            )
        else:
            validate_member_data_slice_value(
                document,
                slice_value,
                source_path=packet_dir / "typed-source.json",
            )
        explanation = values["explanation"]
        _validate_schema(
            explanation,
            schema_root / _EXPLANATION_SCHEMA_NAME,
            code="M6_PACKET_SCHEMA_INVALID",
            context="Explanation",
        )
        source = explanation["source"]
        if (
            source["ir_path"] != "typed-source.json"
            or source["slice_path"] != "slice.json"
            or source["ir_sha256"] != _sha256(
                (packet_dir / "typed-source.json").read_bytes()
            ).upper()
            or source["slice_sha256"] != _sha256(
                (packet_dir / "slice.json").read_bytes()
            ).upper()
            or source["blueprint_package_sha256"]
            != request.source_fingerprint.upper()
            or source["blueprint_asset_path"] != request.asset_path
            or source["graph_id"] != request.graph_id
        ):
            raise ValueError("Explanation source provenance disagrees with packet")
        baseline = values["baseline_facts"]
        _validate_schema(
            baseline,
            schema_root / _BASELINE_SCHEMA_NAME,
            code="M6_PACKET_SCHEMA_INVALID",
            context="baseline facts",
        )
        enriched_typed = dict(typed_value)
        enriched_typed["_m6_source_fingerprint"] = request.source_fingerprint
        validate_baseline_facts(
            baseline,
            typed_document=enriched_typed,
            slice_value=slice_value,
            explanation=explanation,
        )
        entities = baseline["entities"]
        relations = baseline["relations"]
        if not isinstance(entities, list) or not isinstance(relations, list):
            raise ValueError("baseline facts presentation collections are invalid")
        criterion_entity_id = baseline["criterion_entity_id"]
        if not isinstance(criterion_entity_id, str):
            raise ValueError("baseline facts criterion entity is invalid")
        _, visible_ids, visible_relation_count = derive_presentation_visibility(
            entities,
            relations,
            criterion_entity_id=criterion_entity_id,
            max_visible_entities=request.presentation_budget.max_visible_entities,
        )
        expected_presentation = {
            entity_id: (
                "truncated",
                "presentation_budget_exhausted",
            )
            for entity_id in {
                str(entity["id"]) for entity in entities
            }
            - visible_ids
        }
        expected_presentation.update(
            {
                str(entity["id"]): (entity["semantic_status"], "")
                for entity in entities
                if str(entity["id"]) in visible_ids
            }
        )
        actual_presentation = {
            str(entity["id"]): (
                entity["presentation_status"],
                entity["presentation_reason"],
            )
            for entity in entities
        }
        if actual_presentation != expected_presentation:
            raise ValueError("baseline facts presentation states disagree with budget")
        if visible_relation_count > request.presentation_budget.max_visible_relations:
            _fail(
                "M6_PIPELINE_BUDGET_EXCEEDED",
                "visible relations exceed the presentation budget",
                phase="packet_validation",
                diagnostics={
                    "dimension": "visible_relations",
                    "observed": visible_relation_count,
                    "declared": request.presentation_budget.max_visible_relations,
                },
            )
        counts = manifest["counts"]
        expected_counts = {
            "bound_files": 6,
            "packet_files": 7,
            "selected_entities": slice_value["counts"]["nodes"],
            "selected_relations": slice_value["counts"]["edges"],
        }
        if counts != expected_counts:
            raise ValueError("manifest counts disagree with packet products")
        if (
            manifest["query_kind"] != request.query_kind
            or manifest["renderer_id"] != request.renderer_id
        ):
            raise ValueError("manifest request bindings disagree")
    except M6Error:
        raise
    except Exception as error:
        _fail(
            "M6_PACKET_REFERENCE_INVALID",
            f"packet cross-product validation failed: {error}",
            phase="packet_validation",
            cause=error,
        )
    return SessionPacketResult(packet_dir, expected_semantic_hash, manifest)
