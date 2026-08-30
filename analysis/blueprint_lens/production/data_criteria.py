"""Strict versioned input registry for the bounded M5 data-slice gate."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
from types import MappingProxyType
from typing import Any, Literal, Mapping, NoReturn

from ..contract_validation import validate_contract_file
from ..m5_errors import M5DataError
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .corpus import load_corpus_manifest


_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_REGISTRY_SCHEMA_PATH = (
    _PROJECT_ROOT / "schemas" / "blueprint-lens-m5-data-criteria-v1.schema.json"
)
_GROUND_TRUTH_SCHEMA_PATH = (
    _PROJECT_ROOT / "schemas" / "blueprint-lens-ground-truth-v1.schema.json"
)
_CONTROLLED_CASE_IDS = tuple(f"M5-T{index:02d}" for index in range(1, 3))
_SMOKE_CASE_IDS = tuple(f"M5-S{index:02d}" for index in range(1, 9))
_CANDIDATE_IDS = tuple(f"M3-C{index:02d}" for index in range(1, 9))
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True, slots=True)
class ControlledDataCase:
    case_id: str
    document_path: Path
    document_sha256: str
    ground_truth_path: Path
    ground_truth_sha256: str
    review_status: Literal["independent_reviewed", "frozen"]


@dataclass(frozen=True, slots=True)
class CorpusDataCase:
    case_id: str
    candidate_id: str
    blueprint_object_path: str
    graph_id: str
    member_guid: str
    member_name: str
    question: str


@dataclass(frozen=True, slots=True)
class DataCriteriaRegistry:
    rules_version: str
    corpus_manifest_path: Path
    corpus_manifest_sha256: str
    m3_pipeline_report_path: Path
    m3_pipeline_report_sha256: str
    controlled_cases: tuple[ControlledDataCase, ...]
    corpus_smoke_cases: tuple[CorpusDataCase, ...]


def _registry_fail(
    message: str,
    *,
    cause: Exception | None = None,
) -> NoReturn:
    raise M5DataError(
        "M5_REGISTRY_INVALID",
        f"cannot load registry: {message}",
        cause=cause,
    )


def _object(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        _registry_fail(f"{context} must be an object")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        _registry_fail(f"{context} must be a non-empty string")
    return value


def _sha_string(value: Any, context: str) -> str:
    result = _string(value, context)
    if _SHA256_PATTERN.fullmatch(result) is None:
        _registry_fail(f"{context} must be a lowercase SHA-256")
    return result


def _load_object(path: Path, context: str) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _registry_fail(f"cannot read {context} {path}: {error}", cause=error)
    return _object(value, context)


def _sha256(path: Path, context: str) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _registry_fail(f"cannot hash {context} {path}: {error}", cause=error)


def _require_hash(path: Path, expected: str, context: str) -> None:
    actual = _sha256(path, context)
    if actual != expected:
        _registry_fail(
            f"{context} SHA-256 mismatch: expected {expected}, got {actual}"
        )


def _portable_parts(stored_path: Any, context: str) -> tuple[str, ...]:
    value = _string(stored_path, context)
    posix = PurePosixPath(value)
    windows = PureWindowsPath(value)
    if (
        "\\" in value
        or posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _registry_fail(f"{context} is not a portable relative path: {value}")
    return posix.parts


def _resolve_contained_path(
    base: Path,
    project_root: Path,
    stored_path: Any,
    context: str,
) -> Path:
    parts = _portable_parts(stored_path, context)
    root = project_root.resolve()
    resolved = (base.resolve() / Path(*parts)).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        _registry_fail(f"{context} escapes the project root", cause=error)
    if not resolved.is_file():
        _registry_fail(f"{context} does not name a file: {'/'.join(parts)}")
    return resolved


def _resolve_project_path(
    project_root: Path,
    stored_path: Any,
    context: str,
) -> Path:
    return _resolve_contained_path(
        project_root,
        project_root,
        stored_path,
        context,
    )


def _rows(value: Mapping[str, Any], field: str) -> list[Mapping[str, Any]]:
    raw_rows = value.get(field)
    if not isinstance(raw_rows, list):
        _registry_fail(f"{field} must be an array")
    return [
        _object(row, f"{field}[{index}]")
        for index, row in enumerate(raw_rows)
    ]


def _require_exact_ids(
    rows: list[Mapping[str, Any]],
    field: str,
    expected_ids: tuple[str, ...],
) -> None:
    actual_ids = [_string(row.get(field), field) for row in rows]
    if len(set(actual_ids)) != len(actual_ids):
        _registry_fail(f"duplicate {field} values are forbidden")
    if tuple(sorted(actual_ids)) != expected_ids:
        _registry_fail(
            f"{field} values must be exactly {list(expected_ids)}, "
            f"got {sorted(actual_ids)}"
        )


def _member_accesses(
    document: BlueprintDocument,
    graph_id: str,
    member_guid: str,
    expected_name: str,
    context: str,
) -> None:
    graphs = [graph for graph in document.graphs if graph.id == graph_id]
    if len(graphs) != 1:
        _registry_fail(f"{context} graph must resolve exactly once")
    accesses = [
        node
        for node in graphs[0].nodes
        if isinstance(node.symbol, Mapping)
        and node.symbol.get("kind") == "variable"
        and node.symbol.get("guid") == member_guid
        and node.symbol.get("access") in {"get", "set"}
        and node.symbol.get("is_local_scope") is False
    ]
    if not accesses:
        _registry_fail(f"{context} member GUID has no non-local Get/Set access")
    names = {node.symbol.get("name") for node in accesses if node.symbol is not None}
    if names != {expected_name}:
        _registry_fail(
            f"{context} member name disagrees with GUID: "
            f"expected {expected_name!r}, got {sorted(str(name) for name in names)}"
        )


def _load_controlled_case(
    row: Mapping[str, Any],
    project_root: Path,
) -> ControlledDataCase:
    case_id = _string(row.get("case_id"), "controlled case ID")
    document_hash = _sha_string(
        row.get("document_sha256"), f"{case_id}.document_sha256"
    )
    ground_truth_hash = _sha_string(
        row.get("ground_truth_sha256"), f"{case_id}.ground_truth_sha256"
    )
    document_path = _resolve_project_path(
        project_root,
        row.get("document_path"),
        f"{case_id}.document_path",
    )
    ground_truth_path = _resolve_project_path(
        project_root,
        row.get("ground_truth_path"),
        f"{case_id}.ground_truth_path",
    )
    _require_hash(document_path, document_hash, f"{case_id} document")
    _require_hash(ground_truth_path, ground_truth_hash, f"{case_id} ground truth")

    document = load_blueprint_lens_v1(document_path)
    validate_contract_file(ground_truth_path, _GROUND_TRUTH_SCHEMA_PATH)
    ground_truth = _load_object(ground_truth_path, f"{case_id} ground truth")
    if ground_truth.get("format") != "blueprint-lens-ground-truth":
        _registry_fail(f"{case_id} ground truth format is invalid")
    if ground_truth.get("schema_version") != "1.0.0":
        _registry_fail(f"{case_id} ground truth schema version is invalid")
    if ground_truth.get("slice_kind") != "member_variable_data_dependency":
        _registry_fail(f"{case_id} ground truth has the wrong slice kind")
    if ground_truth.get("rules_version") != "1.0.0":
        _registry_fail(f"{case_id} ground truth rules version is invalid")
    source_hash = _string(
        ground_truth.get("source_sha256"),
        f"{case_id}.source_sha256",
    )
    if source_hash.lower() != document_hash:
        _registry_fail(f"{case_id} ground truth source hash disagrees")
    review = _object(ground_truth.get("review"), f"{case_id}.review")
    review_status = _string(row.get("review_status"), f"{case_id}.review_status")
    if review_status not in {"independent_reviewed", "frozen"}:
        _registry_fail(f"{case_id} review status is not approved")
    if review.get("status") != review_status:
        _registry_fail(f"{case_id} review status disagrees with ground truth")

    criterion = _object(
        ground_truth.get("criterion"),
        f"{case_id}.criterion",
    )
    graph_id = _string(criterion.get("graph_id"), f"{case_id}.criterion.graph_id")
    member_guid = _string(
        criterion.get("member_guid"),
        f"{case_id}.criterion.member_guid",
    )
    member_name = _string(
        criterion.get("member_name"),
        f"{case_id}.criterion.member_name",
    )
    _string(criterion.get("question"), f"{case_id}.criterion.question")
    _member_accesses(document, graph_id, member_guid, member_name, case_id)

    return ControlledDataCase(
        case_id=case_id,
        document_path=document_path,
        document_sha256=document_hash,
        ground_truth_path=ground_truth_path,
        ground_truth_sha256=ground_truth_hash,
        review_status=review_status,
    )


def _corpus_candidate_index(
    manifest: Mapping[str, Any],
) -> dict[str, Mapping[str, Any]]:
    rows = manifest.get("candidate_graphs")
    if not isinstance(rows, list):
        _registry_fail("corpus candidate_graphs must be an array")
    index: dict[str, Mapping[str, Any]] = {}
    for position, raw_row in enumerate(rows):
        row = _object(raw_row, f"candidate_graphs[{position}]")
        candidate_id = _string(row.get("id"), f"candidate_graphs[{position}].id")
        if candidate_id in index:
            _registry_fail(f"corpus repeats candidate {candidate_id}")
        index[candidate_id] = row
    if tuple(sorted(index)) != _CANDIDATE_IDS:
        _registry_fail("corpus candidates must be exactly M3-C01 through M3-C08")
    return index


def _load_smoke_case(
    row: Mapping[str, Any],
    candidate: Mapping[str, Any],
) -> CorpusDataCase:
    case_id = _string(row.get("case_id"), "smoke case ID")
    candidate_id = _string(row.get("candidate_id"), f"{case_id}.candidate_id")
    object_path = _string(
        row.get("blueprint_object_path"),
        f"{case_id}.blueprint_object_path",
    )
    graph_id = _string(row.get("graph_id"), f"{case_id}.graph_id")
    if candidate.get("object_path") != object_path:
        _registry_fail(f"{case_id} object path disagrees with {candidate_id}")
    if candidate.get("graph_id") != graph_id:
        _registry_fail(f"{case_id} graph ID disagrees with {candidate_id}")
    member_guid = _string(row.get("member_guid"), f"{case_id}.member_guid")
    member_name = _string(row.get("member_name"), f"{case_id}.member_name")
    question = _string(row.get("question"), f"{case_id}.question")
    expected_question = (
        f"Where is {member_name} read and written, where do its written values "
        "come from, and which direct conditions control those writes?"
    )
    if question != expected_question:
        _registry_fail(f"{case_id} question is not the approved literal")
    return CorpusDataCase(
        case_id=case_id,
        candidate_id=candidate_id,
        blueprint_object_path=object_path,
        graph_id=graph_id,
        member_guid=member_guid,
        member_name=member_name,
        question=question,
    )


def _pipeline_runs(
    report: Mapping[str, Any],
    corpus_hash: str,
) -> Mapping[str, Mapping[str, Any]]:
    if report.get("schema_name") != "blueprint-lens-m3-production-pipeline-report":
        _registry_fail("M3 pipeline report schema name is invalid")
    if report.get("schema_version") != "1.0.0":
        _registry_fail("M3 pipeline report schema version is invalid")
    if report.get("corpus_manifest_sha256") != corpus_hash:
        _registry_fail("M3 pipeline report corpus binding disagrees")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        _registry_fail("M3 pipeline report must contain exactly two runs")
    indexed: dict[str, Mapping[str, Any]] = {}
    for raw_run in runs:
        run = _object(raw_run, "M3 pipeline run")
        run_id = _string(run.get("run_id"), "M3 pipeline run ID")
        if run_id not in {"run1", "run2"} or run_id in indexed:
            _registry_fail(f"M3 pipeline run identity is invalid: {run_id}")
        indexed[run_id] = run
    if set(indexed) != {"run1", "run2"}:
        _registry_fail("M3 pipeline report must bind run1 and run2")
    return MappingProxyType(indexed)


def _asset_index(run: Mapping[str, Any], run_id: str) -> Mapping[str, Mapping[str, Any]]:
    assets = run.get("assets")
    if not isinstance(assets, list):
        _registry_fail(f"{run_id} assets must be an array")
    indexed: dict[str, Mapping[str, Any]] = {}
    for position, raw_row in enumerate(assets):
        row = _object(raw_row, f"{run_id}.assets[{position}]")
        object_path = _string(
            row.get("object_path"),
            f"{run_id}.assets[{position}].object_path",
        )
        if object_path in indexed:
            _registry_fail(f"{run_id} repeats asset {object_path}")
        indexed[object_path] = row
    return MappingProxyType(indexed)


def _preflight_smoke_sources(
    project_root: Path,
    report_path: Path,
    runs: Mapping[str, Mapping[str, Any]],
    smoke_cases: tuple[CorpusDataCase, ...],
) -> None:
    for run_id in ("run1", "run2"):
        assets = _asset_index(runs[run_id], run_id)
        for case in smoke_cases:
            row = assets.get(case.blueprint_object_path)
            if row is None:
                _registry_fail(
                    f"{run_id} lacks asset {case.blueprint_object_path}"
                )
            typed_hash = _sha_string(
                row.get("typed_ir_sha256"),
                f"{run_id}.{case.case_id}.typed_ir_sha256",
            )
            typed_path = _resolve_contained_path(
                report_path.parent,
                project_root,
                row.get("typed_ir_relative_path"),
                f"{run_id}.{case.case_id}.typed_ir_relative_path",
            )
            _require_hash(typed_path, typed_hash, f"{run_id} {case.case_id} typed IR")
            document = load_blueprint_lens_v1(typed_path)
            _member_accesses(
                document,
                case.graph_id,
                case.member_guid,
                case.member_name,
                f"{run_id}.{case.case_id}",
            )


def load_data_criteria(
    path: str | Path,
    *,
    project_root: str | Path,
) -> DataCriteriaRegistry:
    """Load and preflight every immutable M5 criterion and source binding."""

    source = Path(path)
    root = Path(project_root)
    try:
        value = _load_object(source, "M5 data criteria registry")
        schema = _load_object(_REGISTRY_SCHEMA_PATH, "M5 data criteria schema")
        validate_instance(value, schema)

        controlled_rows = _rows(value, "controlled_cases")
        smoke_rows = _rows(value, "corpus_smoke_cases")
        _require_exact_ids(controlled_rows, "case_id", _CONTROLLED_CASE_IDS)
        _require_exact_ids(smoke_rows, "case_id", _SMOKE_CASE_IDS)
        _require_exact_ids(smoke_rows, "candidate_id", _CANDIDATE_IDS)

        corpus_binding = _object(value.get("corpus_manifest"), "corpus_manifest")
        corpus_hash = _sha_string(
            corpus_binding.get("sha256"),
            "corpus_manifest.sha256",
        )
        corpus_path = _resolve_project_path(
            root,
            corpus_binding.get("path"),
            "corpus_manifest.path",
        )
        _require_hash(corpus_path, corpus_hash, "corpus manifest")
        corpus_manifest = load_corpus_manifest(corpus_path)
        candidates = _corpus_candidate_index(corpus_manifest)

        controlled = tuple(
            sorted(
                (_load_controlled_case(row, root) for row in controlled_rows),
                key=lambda case: case.case_id,
            )
        )
        smoke = tuple(
            sorted(
                (
                    _load_smoke_case(row, candidates[str(row["candidate_id"])])
                    for row in smoke_rows
                ),
                key=lambda case: case.case_id,
            )
        )

        report_binding = _object(
            value.get("m3_pipeline_report"),
            "m3_pipeline_report",
        )
        report_hash = _sha_string(
            report_binding.get("sha256"),
            "m3_pipeline_report.sha256",
        )
        report_path = _resolve_project_path(
            root,
            report_binding.get("path"),
            "m3_pipeline_report.path",
        )
        _require_hash(report_path, report_hash, "M3 pipeline report")
        report = _load_object(report_path, "M3 pipeline report")
        runs = _pipeline_runs(report, corpus_hash)
        _preflight_smoke_sources(root, report_path, runs, smoke)

        return DataCriteriaRegistry(
            rules_version=_string(value.get("rules_version"), "rules_version"),
            corpus_manifest_path=corpus_path,
            corpus_manifest_sha256=corpus_hash,
            m3_pipeline_report_path=report_path,
            m3_pipeline_report_sha256=report_hash,
            controlled_cases=controlled,
            corpus_smoke_cases=smoke,
        )
    except M5DataError:
        raise
    except Exception as error:
        _registry_fail(f"{source}: {error}", cause=error)
