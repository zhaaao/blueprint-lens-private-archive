"""Bounded M3 readiness audit for future interprocedural adapters.

This module deliberately audits inputs and compatibility seams only.  It does
not resolve calls, traverse another Blueprint, expand macros, build a project
graph, or add cross-document edges.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import inspect
import json
import os
from pathlib import Path
import re
import tempfile
from typing import Any, Literal, Mapping

from ..data_slice import compute_member_variable_data_slice
from ..execution_slice import compute_execution_slice
from ..schema_validation import validate_instance
from ..typed_ir import build_typed_ir
from .project_documents import (
    FrozenFixture,
    FrozenFixtureProvider,
    ProjectDocument,
    ProjectDocumentProvider,
)


_ROOT = Path(__file__).resolve().parents[3]
_READINESS_SCHEMA_PATH = (
    _ROOT / "schemas" / "blueprint-lens-m3-interprocedural-readiness-v1.schema.json"
)
_CORE_V1_GOLDEN_PATH = _ROOT / "fixtures" / "v1" / "BP_SlicingProbe.v1.json"
_CORE_V1_GOLDEN_SHA256 = (
    "7a39ff8159761491dc8490aabb0449bac4638af9adf488bbe3cb476f58943496"
)
_EXECUTION_SIGNATURE = (
    "(document: 'BlueprintDocument', criterion_node_id: 'str') -> 'ExecutionSlice'"
)
_DATA_SIGNATURE = (
    "(document: 'BlueprintDocument', graph_id: 'str', member_guid: 'str') "
    "-> 'MemberVariableDataSlice'"
)
_READINESS_NAME = "interprocedural-readiness.v1.json"
_HASHES_NAME = "hashes.sha256"
_SHA256 = re.compile(r"[0-9a-f]{64}")
_CALL_REFERENCE_FIELDS = frozenset(
    {
        "guid",
        "is_latent",
        "is_local_scope",
        "is_pure",
        "is_self_context",
        "kind",
        "name",
        "parent_class",
    }
)


@dataclass(frozen=True, slots=True)
class ReadinessCheck:
    check_id: str
    status: Literal["PASS", "FAIL"]
    evidence: Mapping[str, Any]


class ReadinessError(ValueError):
    """A fail-closed readiness publication error with a stable code."""

    def __init__(self, code: str, message: str, *, cause: Exception | None = None) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause


def _canonical_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _check(check_id: str, passed: bool, **evidence: Any) -> ReadinessCheck:
    return ReadinessCheck(
        check_id=check_id,
        status="PASS" if passed else "FAIL",
        evidence=evidence,
    )


def _check_value(check: ReadinessCheck) -> dict[str, Any]:
    return {
        "check_id": check.check_id,
        "evidence": dict(check.evidence),
        "status": check.status,
    }


def _validate_readiness_schema(value: Mapping[str, Any]) -> None:
    try:
        schema = json.loads(_READINESS_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(value, schema)
    except Exception as error:
        raise ReadinessError(
            "M3_READINESS_REPORT_INVALID",
            f"readiness report violates its schema: {error}",
            cause=error,
        ) from error


def _corpus_asset_ids(corpus: Mapping[str, Any]) -> tuple[str, ...]:
    identities: set[str] = set()
    regression = corpus.get("regression_assets")
    candidates = corpus.get("candidate_graphs")
    if not isinstance(regression, list) or not isinstance(candidates, list):
        return ()
    for row in (*regression, *candidates):
        if not isinstance(row, Mapping):
            return ()
        object_path = row.get("object_path")
        if not isinstance(object_path, str) or not object_path:
            return ()
        identities.add(object_path)
    return tuple(sorted(identities))


def _candidate_graph_ids(corpus: Mapping[str, Any]) -> tuple[tuple[str, str], ...]:
    candidates = corpus.get("candidate_graphs")
    if not isinstance(candidates, list):
        return ()
    identities: list[tuple[str, str]] = []
    for row in candidates:
        if not isinstance(row, Mapping):
            return ()
        object_path = row.get("object_path")
        graph_id = row.get("graph_id")
        if not isinstance(object_path, str) or not isinstance(graph_id, str):
            return ()
        identities.append((object_path, graph_id))
    return tuple(sorted(identities))


def _pipeline_runs(
    pipeline_report: Mapping[str, Any],
) -> tuple[Mapping[str, Any], Mapping[str, Any]] | None:
    runs = pipeline_report.get("runs")
    if (
        not isinstance(runs, list)
        or len(runs) != 2
        or not all(isinstance(run, Mapping) for run in runs)
        or [run.get("run_id") for run in runs] != ["run1", "run2"]
    ):
        return None
    return runs[0], runs[1]


def _report_assets(run: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    rows = run.get("assets")
    if not isinstance(rows, list):
        return {}
    assets: dict[str, Mapping[str, Any]] = {}
    for row in rows:
        if not isinstance(row, Mapping):
            return {}
        object_path = row.get("object_path")
        if not isinstance(object_path, str) or not object_path or object_path in assets:
            return {}
        assets[object_path] = row
    return assets


def _normalised_report_assets(
    assets: Mapping[str, Mapping[str, Any]],
) -> dict[str, dict[str, Any]]:
    return {
        asset_id: {
            key: value
            for key, value in row.items()
            if key not in {"raw_relative_path", "typed_ir_relative_path"}
        }
        for asset_id, row in sorted(assets.items())
    }


def _load_documents(
    provider: ProjectDocumentProvider,
) -> tuple[tuple[str, ...], dict[str, ProjectDocument], bool]:
    try:
        asset_ids = provider.list_asset_ids()
        if tuple(sorted(asset_ids)) != asset_ids or len(asset_ids) != len(set(asset_ids)):
            return asset_ids, {}, False
        documents = {asset_id: provider.load(asset_id) for asset_id in asset_ids}
    except Exception:
        return (), {}, False
    return asset_ids, documents, True


def _valid_call_reference(symbol: Mapping[str, Any]) -> bool:
    if set(symbol) != _CALL_REFERENCE_FIELDS or symbol.get("kind") != "function":
        return False
    string_fields = ("guid", "name", "parent_class")
    bool_fields = ("is_latent", "is_local_scope", "is_pure", "is_self_context")
    if not all(isinstance(symbol.get(field), str) for field in string_fields):
        return False
    if not symbol.get("name") or not all(
        isinstance(symbol.get(field), bool) for field in bool_fields
    ):
        return False
    if symbol["is_self_context"]:
        return bool(symbol["guid"])
    return bool(symbol["parent_class"])


def _build_typed_products(
    documents: Mapping[str, ProjectDocument],
) -> tuple[dict[str, Mapping[str, Any]], dict[str, str], bool]:
    values: dict[str, Mapping[str, Any]] = {}
    hashes: dict[str, str] = {}
    try:
        for asset_id, project_document in sorted(documents.items()):
            raw = json.loads(project_document.raw_path.read_text(encoding="utf-8"))
            if not isinstance(raw, dict):
                return {}, {}, False
            typed = build_typed_ir(raw, expected_blueprint_path=asset_id)
            values[asset_id] = typed
            hashes[asset_id] = _sha256_bytes(_canonical_bytes(typed))
    except Exception:
        return {}, {}, False
    return values, hashes, True


def build_readiness(
    corpus_manifest: Mapping[str, Any],
    provider: ProjectDocumentProvider,
    pipeline_report: Mapping[str, Any],
) -> Mapping[str, Any]:
    """Build a deterministic, non-resolving readiness record from M3 products."""

    corpus_ids = _corpus_asset_ids(corpus_manifest)
    candidate_graphs = _candidate_graph_ids(corpus_manifest)
    provider_ids, documents, documents_loaded = _load_documents(provider)
    runs = _pipeline_runs(pipeline_report)
    run1_assets = _report_assets(runs[0]) if runs is not None else {}
    run2_assets = _report_assets(runs[1]) if runs is not None else {}
    report_ids = tuple(sorted(run1_assets))
    corpus_sha256 = _sha256_bytes(_canonical_bytes(corpus_manifest))
    pipeline_sha256 = _sha256_bytes(_canonical_bytes(pipeline_report))

    repeated_products_identical = bool(
        runs is not None
        and pipeline_report.get("normalized_runs_identical") is True
        and runs[0].get("batch_manifest_sha256")
        == runs[1].get("batch_manifest_sha256")
        and _SHA256.fullmatch(str(runs[0].get("batch_manifest_sha256", "")))
        and _normalised_report_assets(run1_assets)
        == _normalised_report_assets(run2_assets)
    )
    asset_identity_ok = bool(
        corpus_manifest.get("schema_name") == "blueprint-lens-m3-corpus"
        and corpus_manifest.get("schema_version") == "1.0.0"
        and pipeline_report.get("schema_name")
        == "blueprint-lens-m3-production-pipeline-report"
        and pipeline_report.get("schema_version") == "1.0.0"
        and pipeline_report.get("corpus_manifest_sha256") == corpus_sha256
        and pipeline_report.get("unique_asset_count") == len(corpus_ids)
        and corpus_ids
        and corpus_ids == provider_ids == report_ids == tuple(sorted(run2_assets))
        and documents_loaded
        and repeated_products_identical
    )

    provenance_values = [document.provenance for document in documents.values()]
    package_ok = bool(
        asset_identity_ok
        and all(
            provenance.package_persistent_guid
            and _SHA256.fullmatch(provenance.package_source_sha256)
            for provenance in provenance_values
        )
    )
    compile_ok = bool(
        asset_identity_ok
        and all(provenance.compile_status == "up_to_date" for provenance in provenance_values)
    )
    generated_class_ok = bool(
        asset_identity_ok
        and all(
            provenance.generated_class_path
            == f"{provenance.blueprint_object_path}_C"
            for provenance in provenance_values
        )
    )

    all_graphs = [
        graph for document in documents.values() for graph in document.document.graphs
    ]
    callable_graphs = [
        graph for graph in all_graphs if graph.kind in {"function", "delegate_signature"}
    ]
    function_symbols = [
        node.symbol
        for document in documents.values()
        for node in document.document.nodes
        if node.symbol is not None and node.symbol.get("kind") == "function"
    ]
    graph_lookup = {
        (asset_id, graph.id)
        for asset_id, document in documents.items()
        for graph in document.document.graphs
    }
    graph_symbols_ok = bool(
        asset_identity_ok
        and candidate_graphs
        and set(candidate_graphs).issubset(graph_lookup)
        and callable_graphs
        and function_symbols
        and all(graph.id and graph.name and graph.kind for graph in all_graphs)
    )
    call_references_ok = bool(
        graph_symbols_ok
        and all(_valid_call_reference(symbol) for symbol in function_symbols)
    )

    adapter_equivalence_ok = False
    if asset_identity_ok:
        try:
            frozen = FrozenFixtureProvider(
                {
                    asset_id: FrozenFixture(
                        raw_path=document.raw_path,
                        raw_sha256=document.raw_sha256,
                        provenance=document.provenance,
                    )
                    for asset_id, document in documents.items()
                }
            )
            adapter_equivalence_ok = frozen.list_asset_ids() == provider_ids and all(
                frozen.load(asset_id) == documents[asset_id] for asset_id in provider_ids
            )
        except Exception:
            adapter_equivalence_ok = False

    raw_hashes_match = bool(
        asset_identity_ok
        and all(
            document.document.format == "blueprint-lens-raw-probe"
            and document.document.format_version == "0.2"
            and run1_assets[asset_id].get("raw_sha256") == document.raw_sha256
            and run2_assets[asset_id].get("raw_sha256") == document.raw_sha256
            for asset_id, document in documents.items()
        )
    )
    typed_values, typed_hashes, typed_built = _build_typed_products(documents)
    typed_ir_ok = bool(
        asset_identity_ok
        and typed_built
        and all(
            typed_values[asset_id].get("format") == "blueprint-lens"
            and typed_values[asset_id].get("schema_version") == "1.0.0"
            and run1_assets[asset_id].get("typed_ir_sha256") == typed_hashes[asset_id]
            and run2_assets[asset_id].get("typed_ir_sha256") == typed_hashes[asset_id]
            for asset_id in documents
        )
    )

    execution_signature = str(inspect.signature(compute_execution_slice))
    data_signature = str(inspect.signature(compute_member_variable_data_slice))
    try:
        core_v1_sha256 = _sha256_bytes(_CORE_V1_GOLDEN_PATH.read_bytes())
    except OSError:
        core_v1_sha256 = ""

    checks = (
        _check(
            "asset_identity",
            asset_identity_ok,
            asset_count=len(provider_ids),
            asset_ids_sha256=_sha256_bytes("\n".join(provider_ids).encode("utf-8")),
            corpus_manifest_sha256=corpus_sha256,
            pipeline_report_sha256=pipeline_sha256,
            repeated_run_count=2 if runs is not None else 0,
        ),
        _check(
            "package_guid_source_hash",
            package_ok,
            asset_count=len(provenance_values),
            package_guid_count=sum(
                bool(provenance.package_persistent_guid)
                for provenance in provenance_values
            ),
            package_source_sha256_count=sum(
                bool(_SHA256.fullmatch(provenance.package_source_sha256))
                for provenance in provenance_values
            ),
        ),
        _check(
            "compile_status",
            compile_ok,
            up_to_date_count=sum(
                provenance.compile_status == "up_to_date"
                for provenance in provenance_values
            ),
        ),
        _check(
            "generated_class",
            generated_class_ok,
            generated_class_count=sum(
                bool(provenance.generated_class_path)
                for provenance in provenance_values
            ),
        ),
        _check(
            "graph_function_symbols",
            graph_symbols_ok,
            function_graph_count=len(callable_graphs),
            function_symbol_count=len(function_symbols),
            graph_count=len(all_graphs),
        ),
        _check(
            "call_reference_fields",
            call_references_ok,
            call_reference_count=len(function_symbols),
            external_context_count=sum(
                not bool(symbol.get("is_self_context")) for symbol in function_symbols
            ),
            field_names=sorted(_CALL_REFERENCE_FIELDS),
            self_context_count=sum(
                bool(symbol.get("is_self_context")) for symbol in function_symbols
            ),
        ),
        _check(
            "production_frozen_adapter_equivalence",
            adapter_equivalence_ok,
            equivalent_asset_count=len(documents) if adapter_equivalence_ok else 0,
        ),
        _check(
            "raw_0_2",
            raw_hashes_match,
            document_count=len(documents),
            format="blueprint-lens-raw-probe",
            format_version="0.2",
        ),
        _check(
            "typed_ir_1_0_0",
            typed_ir_ok,
            document_count=len(typed_values),
            format="blueprint-lens",
            schema_version="1.0.0",
        ),
        _check(
            "compute_execution_slice_signature",
            execution_signature == _EXECUTION_SIGNATURE,
            signature=execution_signature,
        ),
        _check(
            "compute_member_variable_data_slice_signature",
            data_signature == _DATA_SIGNATURE,
            signature=data_signature,
        ),
        _check(
            "core_v1_output_hash",
            core_v1_sha256 == _CORE_V1_GOLDEN_SHA256,
            relative_path="fixtures/v1/BP_SlicingProbe.v1.json",
            sha256=core_v1_sha256,
        ),
    )
    status: Literal["PASS", "FAIL"] = (
        "PASS" if all(check.status == "PASS" for check in checks) else "FAIL"
    )
    batch_sha256 = (
        [str(run.get("batch_manifest_sha256", "")) for run in runs]
        if runs is not None
        else []
    )
    readiness = {
        "checks": [_check_value(check) for check in checks],
        "claim_boundary": {
            "established": (
                "M3 production products carry deterministic identity, provenance, "
                "symbol and compatibility inputs for a future adapter."
            ),
            "not_established": [
                "Interprocedural resolution correctness",
                "Cross-Blueprint traversal correctness",
                "Macro expansion correctness",
                "Scale, performance, layout, UX, comprehension or default-product claims",
            ],
        },
        "evidence_bindings": {
            "batch_manifest_sha256": batch_sha256,
            "corpus_manifest_sha256": corpus_sha256,
            "pipeline_report_sha256": pipeline_sha256,
        },
        "implementation_flags": {
            "cross_blueprint_traversal_implemented": False,
            "macro_expansion_implemented": False,
            "resolution_implemented": False,
        },
        "schema_name": "blueprint-lens-m3-interprocedural-readiness",
        "schema_version": "1.0.0",
        "status": status,
    }
    _validate_readiness_schema(readiness)
    return readiness


def _load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReadinessError(
            "M3_READINESS_INPUT_INVALID", f"cannot load {path}: {error}", cause=error
        ) from error
    if not isinstance(value, dict):
        raise ReadinessError(
            "M3_READINESS_INPUT_INVALID", f"{path} must contain one JSON object"
        )
    return value


def _atomic_write(path: Path, payload: bytes) -> None:
    try:
        handle, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
        )
        os.close(handle)
        temporary = Path(temporary_name)
        try:
            temporary.write_bytes(payload)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    except OSError as error:
        raise ReadinessError(
            "M3_READINESS_WRITE_FAILED", f"cannot publish {path}: {error}", cause=error
        ) from error


def _hash_registry_bytes(evidence_root: Path) -> bytes:
    hashes_path = (evidence_root / _HASHES_NAME).resolve()
    files = sorted(
        path.resolve()
        for path in evidence_root.rglob("*")
        if path.is_file() and path.resolve() != hashes_path
    )
    lines = [
        f"{_sha256_bytes(path.read_bytes())}  {path.relative_to(evidence_root).as_posix()}"
        for path in files
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def freeze_readiness(
    evidence_root: str | Path,
    *,
    corpus_path: str | Path | None = None,
) -> Path:
    """Atomically publish a passing readiness record and refresh its hash binding."""

    from .project_documents import ProductionManifestProvider

    evidence = Path(evidence_root).resolve()
    corpus_source = (
        Path(corpus_path).resolve()
        if corpus_path is not None
        else _ROOT / "fixtures" / "m3" / "m3-corpus-manifest.v1.json"
    )
    corpus = _load_object(corpus_source)
    pipeline = _load_object(evidence / "pipeline-report.v1.json")
    try:
        provider = ProductionManifestProvider(
            evidence / "run1" / "batch-result.v1.json", root=evidence / "run1"
        )
        readiness = build_readiness(corpus, provider, pipeline)
    except ReadinessError:
        raise
    except Exception as error:
        raise ReadinessError(
            "M3_READINESS_INPUT_INVALID", f"readiness inputs were rejected: {error}", cause=error
        ) from error
    if readiness.get("status") != "PASS":
        failed = [
            row.get("check_id")
            for row in readiness.get("checks", [])
            if isinstance(row, Mapping) and row.get("status") != "PASS"
        ]
        raise ReadinessError(
            "M3_READINESS_CHECK_FAILED", f"failed checks: {failed}"
        )

    readiness_path = evidence / _READINESS_NAME
    hashes_path = evidence / _HASHES_NAME
    previous_readiness = readiness_path.read_bytes() if readiness_path.is_file() else None
    previous_hashes = hashes_path.read_bytes() if hashes_path.is_file() else None
    try:
        _atomic_write(readiness_path, _canonical_bytes(readiness))
        _atomic_write(hashes_path, _hash_registry_bytes(evidence))
    except ReadinessError:
        if previous_readiness is None:
            readiness_path.unlink(missing_ok=True)
        else:
            _atomic_write(readiness_path, previous_readiness)
        if previous_hashes is not None:
            _atomic_write(hashes_path, previous_hashes)
        raise
    return readiness_path


__all__ = [
    "ReadinessCheck",
    "ReadinessError",
    "build_readiness",
    "freeze_readiness",
]
