"""Strict versioned input registry for the bounded M4 execution slice gate."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any, Literal, Mapping, NoReturn

from ..contract_validation import validate_contract_file
from ..m4_errors import M4ExecutionError
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .corpus import load_corpus_manifest


_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_REGISTRY_SCHEMA_PATH = (
    _PROJECT_ROOT
    / "schemas"
    / "blueprint-lens-m4-execution-criteria-v1.schema.json"
)
_GROUND_TRUTH_SCHEMA_PATH = (
    _PROJECT_ROOT / "schemas" / "blueprint-lens-ground-truth-v1.schema.json"
)
_CONTROLLED_CASE_IDS = tuple(f"M4-T{index:02d}" for index in range(1, 4))
_SMOKE_CASE_IDS = tuple(f"M4-S{index:02d}" for index in range(1, 9))
_CANDIDATE_IDS = tuple(f"M3-C{index:02d}" for index in range(1, 9))


@dataclass(frozen=True, slots=True)
class ControlledExecutionCase:
    case_id: str
    document_path: Path
    document_sha256: str
    ground_truth_path: Path
    ground_truth_sha256: str
    review_status: Literal["independent_reviewed", "frozen"]


@dataclass(frozen=True, slots=True)
class CorpusExecutionCase:
    case_id: str
    candidate_id: str
    blueprint_object_path: str
    graph_id: str
    criterion_node_id: str
    description: str


@dataclass(frozen=True, slots=True)
class ExecutionCriteriaRegistry:
    rules_version: str
    corpus_manifest_path: Path
    corpus_manifest_sha256: str
    controlled_cases: tuple[ControlledExecutionCase, ...]
    corpus_smoke_cases: tuple[CorpusExecutionCase, ...]


def _registry_fail(
    message: str, *, cause: Exception | None = None
) -> NoReturn:
    raise M4ExecutionError("M4_REGISTRY_INVALID", message, cause=cause)


def _object(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        _registry_fail(f"{context} must be an object")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        _registry_fail(f"{context} must be a non-empty string")
    return value


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


def _resolve_portable_path(
    project_root: Path, stored_path: Any, context: str
) -> Path:
    value = _string(stored_path, context)
    posix = PurePosixPath(value)
    windows = PureWindowsPath(value)
    if (
        "\\" in value
        or posix.is_absolute()
        or windows.is_absolute()
        or windows.drive
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _registry_fail(f"{context} is not a portable project-relative path: {value}")
    root = project_root.resolve()
    resolved = (root / Path(*posix.parts)).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        _registry_fail(f"{context} escapes the project root: {value}", cause=error)
    if not resolved.is_file():
        _registry_fail(f"{context} does not name a file: {value}")
    return resolved


def _rows(value: Mapping[str, Any], field: str) -> list[Mapping[str, Any]]:
    raw_rows = value.get(field)
    if not isinstance(raw_rows, list):
        _registry_fail(f"{field} must be an array")
    return [_object(row, f"{field}[{index}]") for index, row in enumerate(raw_rows)]


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
            f"{field} values must be exactly {list(expected_ids)}, got {sorted(actual_ids)}"
        )


def _criterion_in_document(
    document: BlueprintDocument,
    ground_truth: Mapping[str, Any],
    context: str,
) -> None:
    criterion = _object(ground_truth.get("criterion"), f"{context}.criterion")
    graph_id = _string(criterion.get("graph_id"), f"{context}.criterion.graph_id")
    node_id = _string(criterion.get("node_id"), f"{context}.criterion.node_id")
    _string(criterion.get("description"), f"{context}.criterion.description")

    graphs = [graph for graph in document.graphs if graph.id == graph_id]
    if len(graphs) != 1:
        _registry_fail(f"{context} criterion graph must resolve exactly once")
    nodes = [node for node in graphs[0].nodes if node.id == node_id]
    if len(nodes) != 1:
        _registry_fail(f"{context} criterion node must resolve exactly once")
    if not any(pin.kind == "execution" and pin.direction == "input" for pin in nodes[0].pins):
        _registry_fail(f"{context} criterion node has no execution input")


def _load_controlled_case(
    row: Mapping[str, Any], project_root: Path
) -> ControlledExecutionCase:
    case_id = _string(row.get("case_id"), "controlled case ID")
    document_hash = _string(
        row.get("document_sha256"), f"{case_id}.document_sha256"
    )
    ground_truth_hash = _string(
        row.get("ground_truth_sha256"), f"{case_id}.ground_truth_sha256"
    )
    document_path = _resolve_portable_path(
        project_root, row.get("document_path"), f"{case_id}.document_path"
    )
    ground_truth_path = _resolve_portable_path(
        project_root,
        row.get("ground_truth_path"),
        f"{case_id}.ground_truth_path",
    )
    _require_hash(document_path, document_hash, f"{case_id} document")
    _require_hash(ground_truth_path, ground_truth_hash, f"{case_id} ground truth")

    try:
        document = load_blueprint_lens_v1(document_path)
        validate_contract_file(ground_truth_path, _GROUND_TRUTH_SCHEMA_PATH)
        ground_truth = _load_object(ground_truth_path, f"{case_id} ground truth")
    except M4ExecutionError:
        raise
    except Exception as error:
        _registry_fail(f"{case_id} controlled input is invalid: {error}", cause=error)

    if ground_truth.get("format") != "blueprint-lens-ground-truth":
        _registry_fail(f"{case_id} ground truth format is invalid")
    if ground_truth.get("schema_version") != "1.0.0":
        _registry_fail(f"{case_id} ground truth schema version is invalid")
    if ground_truth.get("slice_kind") != "execution_context":
        _registry_fail(f"{case_id} ground truth must be execution_context")
    if ground_truth.get("rules_version") != "1.0.0":
        _registry_fail(f"{case_id} ground truth rules version is invalid")
    source_hash = _string(
        ground_truth.get("source_sha256"), f"{case_id}.source_sha256"
    )
    if source_hash.lower() != document_hash:
        _registry_fail(f"{case_id} ground truth source hash does not match its document")
    review = _object(ground_truth.get("review"), f"{case_id}.review")
    review_status = _string(row.get("review_status"), f"{case_id}.review_status")
    if review.get("status") != review_status:
        _registry_fail(f"{case_id} review status disagrees with its ground truth")
    _criterion_in_document(document, ground_truth, case_id)

    if review_status not in {"independent_reviewed", "frozen"}:
        _registry_fail(f"{case_id} review status is not approved")
    return ControlledExecutionCase(
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
    row: Mapping[str, Any], candidate: Mapping[str, Any]
) -> CorpusExecutionCase:
    case_id = _string(row.get("case_id"), "smoke case ID")
    candidate_id = _string(row.get("candidate_id"), f"{case_id}.candidate_id")
    object_path = _string(
        row.get("blueprint_object_path"), f"{case_id}.blueprint_object_path"
    )
    graph_id = _string(row.get("graph_id"), f"{case_id}.graph_id")
    if candidate.get("object_path") != object_path:
        _registry_fail(f"{case_id} object path disagrees with {candidate_id}")
    if candidate.get("graph_id") != graph_id:
        _registry_fail(f"{case_id} graph ID disagrees with {candidate_id}")
    criterion_node_id = _string(
        row.get("criterion_node_id"), f"{case_id}.criterion_node_id"
    )
    if not criterion_node_id.startswith(f"{graph_id}::node::"):
        _registry_fail(f"{case_id} criterion node is not inside its declared graph")
    return CorpusExecutionCase(
        case_id=case_id,
        candidate_id=candidate_id,
        blueprint_object_path=object_path,
        graph_id=graph_id,
        criterion_node_id=criterion_node_id,
        description=_string(row.get("description"), f"{case_id}.description"),
    )


def load_execution_criteria(
    path: str | Path,
    *,
    project_root: str | Path,
) -> ExecutionCriteriaRegistry:
    """Load the one bounded M4 registry and prove all immutable input bindings."""

    source = Path(path)
    root = Path(project_root)
    try:
        value = _load_object(source, "M4 execution criteria registry")
        schema = _load_object(_REGISTRY_SCHEMA_PATH, "M4 registry schema")
        validate_instance(value, schema)

        controlled_rows = _rows(value, "controlled_cases")
        smoke_rows = _rows(value, "corpus_smoke_cases")
        _require_exact_ids(controlled_rows, "case_id", _CONTROLLED_CASE_IDS)
        _require_exact_ids(smoke_rows, "case_id", _SMOKE_CASE_IDS)
        _require_exact_ids(smoke_rows, "candidate_id", _CANDIDATE_IDS)

        corpus_binding = _object(value.get("corpus_manifest"), "corpus_manifest")
        corpus_hash = _string(corpus_binding.get("sha256"), "corpus_manifest.sha256")
        corpus_path = _resolve_portable_path(
            root, corpus_binding.get("path"), "corpus_manifest.path"
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
        return ExecutionCriteriaRegistry(
            rules_version=_string(value.get("rules_version"), "rules_version"),
            corpus_manifest_path=corpus_path,
            corpus_manifest_sha256=corpus_hash,
            controlled_cases=controlled,
            corpus_smoke_cases=smoke,
        )
    except M4ExecutionError as error:
        if error.code == "M4_REGISTRY_INVALID":
            raise
        _registry_fail(f"registry dependency emitted {error.code}: {error}", cause=error)
    except Exception as error:
        _registry_fail(f"cannot load registry {source}: {error}", cause=error)
