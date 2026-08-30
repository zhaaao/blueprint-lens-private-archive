"""Build and independently reverify the complete bounded M4 evidence packet."""

from __future__ import annotations

from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import tempfile
from typing import Any, Mapping, NoReturn

from ..execution_slice import ExecutionSlice, compute_execution_slice
from ..m4_errors import M4ExecutionError
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .execution_criteria import (
    ControlledExecutionCase,
    CorpusExecutionCase,
    load_execution_criteria,
)
from .execution_products import (
    build_execution_slice_value,
    canonical_execution_json_bytes,
    validate_execution_slice_value,
)
from .project_documents import ProductionManifestProvider, ProjectDocumentError
from .typed_documents import compose_typed_document


_ROOT = Path(__file__).resolve().parents[3]
_EVIDENCE_SCHEMA = (
    _ROOT / "schemas" / "blueprint-lens-m4-execution-evidence-v1.schema.json"
)
_CONTRACT_PATHS = {
    "criteria_schema": "schemas/blueprint-lens-m4-execution-criteria-v1.schema.json",
    "design_specification": "execution-slice-v1",
    "evidence_schema": "schemas/blueprint-lens-m4-execution-evidence-v1.schema.json",
    "slice_schema": "schemas/blueprint-lens-slice-v1.schema.json",
}


def _fail(code: str, message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M4ExecutionError(code, message, cause=cause)


def _evidence_fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    _fail("M4_EVIDENCE_MISMATCH", message, cause=cause)


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail("M4_SOURCE_INVALID", f"cannot hash source {path}: {error}", cause=error)


def _load_object(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail("M4_SOURCE_INVALID", f"cannot load JSON source {path}: {error}", cause=error)
    if not isinstance(value, dict):
        _fail("M4_SOURCE_INVALID", f"JSON source root is not an object: {path}")
    return value


def _relative(root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        _fail("M4_SOURCE_INVALID", f"source is outside project root: {path}", cause=error)


def _resolve_binding(root: Path, stored: Any, context: str) -> Path:
    if not isinstance(stored, str) or not stored:
        _evidence_fail(f"{context} path must be non-empty")
    posix = PurePosixPath(stored)
    windows = PureWindowsPath(stored)
    if (
        "\\" in stored
        or posix.is_absolute()
        or windows.is_absolute()
        or windows.drive
        or ".." in posix.parts
    ):
        _evidence_fail(f"{context} path is not portable project-relative: {stored}")
    path = root.joinpath(*posix.parts).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        _evidence_fail(f"{context} path escapes project root", cause=error)
    return path


def _reason_map(result: ExecutionSlice, *, edges: bool = False) -> dict[str, list[str]]:
    source = result.edge_inclusion_reasons if edges else result.inclusion_reasons
    entity_ids = result.edge_ids if edges else result.node_ids
    return {entity_id: list(source[entity_id]) for entity_id in entity_ids}


def _boundaries(result: ExecutionSlice) -> list[dict[str, Any]]:
    return [asdict(boundary) for boundary in result.boundaries]


def _permuted(document: BlueprintDocument) -> BlueprintDocument:
    return replace(
        document,
        graphs=tuple(
            replace(
                graph,
                nodes=tuple(
                    replace(node, pins=tuple(reversed(node.pins)))
                    for node in reversed(graph.nodes)
                ),
                edges=tuple(reversed(graph.edges)),
            )
            for graph in reversed(document.graphs)
        ),
    )


def _assert_permutation(
    document: BlueprintDocument, criterion_node_id: str, result: ExecutionSlice
) -> None:
    if compute_execution_slice(_permuted(document), criterion_node_id) != result:
        _evidence_fail("input permutation changed execution slice")


def _controlled_product(
    root: Path,
    case: ControlledExecutionCase,
) -> tuple[str, bytes, dict[str, Any], ExecutionSlice]:
    document = load_blueprint_lens_v1(case.document_path)
    truth = _load_object(case.ground_truth_path)
    criterion = truth.get("criterion")
    expected = truth.get("expected")
    if not isinstance(criterion, Mapping) or not isinstance(expected, Mapping):
        _evidence_fail(f"{case.case_id} ground truth lacks criterion/expected")
    criterion_node_id = criterion.get("node_id")
    if not isinstance(criterion_node_id, str):
        _evidence_fail(f"{case.case_id} criterion node is invalid")
    result = compute_execution_slice(document, criterion_node_id)
    if list(result.node_ids) != expected.get("node_ids") or list(result.edge_ids) != expected.get(
        "edge_ids"
    ):
        _evidence_fail(f"{case.case_id} controlled membership differs from truth")
    _assert_permutation(document, criterion_node_id, result)
    description = criterion.get("description")
    if not isinstance(description, str):
        _evidence_fail(f"{case.case_id} criterion description is invalid")
    value = build_execution_slice_value(
        document,
        result,
        source_fixture=_relative(root, case.document_path),
        source_sha256=case.document_sha256,
        description=description,
    )
    validate_execution_slice_value(document, value, source_path=case.document_path)
    payload = canonical_execution_json_bytes(value)
    relative = f"controlled/{case.case_id}.slice.v1.json"
    report_case = {
        "boundaries": _boundaries(result),
        "case_id": case.case_id,
        "edge_count": len(result.edge_ids),
        "edge_inclusion_reasons": _reason_map(result, edges=True),
        "ground_truth_sha256": case.ground_truth_sha256,
        "node_count": len(result.node_ids),
        "node_inclusion_reasons": _reason_map(result),
        "review_status": case.review_status,
        "slice_path": relative,
        "slice_sha256": hashlib.sha256(payload).hexdigest(),
        "source_sha256": case.document_sha256,
    }
    return relative, payload, report_case, result


def _pipeline_runs(
    report_path: Path,
) -> tuple[Mapping[str, Any], dict[str, Mapping[str, Any]]]:
    report = _load_object(report_path)
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        _fail("M4_SOURCE_INVALID", "M3 pipeline report must contain exactly two runs")
    indexed: dict[str, Mapping[str, Any]] = {}
    for raw_run in runs:
        if not isinstance(raw_run, Mapping) or raw_run.get("run_id") not in {"run1", "run2"}:
            _fail("M4_SOURCE_INVALID", "M3 pipeline report run identity is invalid")
        run_id = str(raw_run["run_id"])
        if run_id in indexed:
            _fail("M4_SOURCE_INVALID", f"M3 pipeline report repeats {run_id}")
        indexed[run_id] = raw_run
    if set(indexed) != {"run1", "run2"}:
        _fail("M4_SOURCE_INVALID", "M3 pipeline report must bind run1 and run2")
    return report, indexed


def _run_sources(
    report_path: Path, run: Mapping[str, Any]
) -> tuple[ProductionManifestProvider, dict[str, Mapping[str, Any]]]:
    manifest_relative = run.get("batch_manifest_relative_path")
    if not isinstance(manifest_relative, str):
        _fail("M4_SOURCE_INVALID", "M3 run lacks batch manifest path")
    manifest_path = report_path.parent / manifest_relative
    declared_hash = run.get("batch_manifest_sha256")
    if _sha256(manifest_path) != declared_hash:
        _fail("M4_SOURCE_INVALID", f"M3 batch manifest hash mismatch: {manifest_path}")
    try:
        provider = ProductionManifestProvider(manifest_path)
    except ProjectDocumentError as error:
        _fail("M4_SOURCE_INVALID", f"M3 provider rejected {manifest_path}: {error}", cause=error)
    assets = run.get("assets")
    if not isinstance(assets, list):
        _fail("M4_SOURCE_INVALID", "M3 run assets must be an array")
    index: dict[str, Mapping[str, Any]] = {}
    for row in assets:
        if not isinstance(row, Mapping) or not isinstance(row.get("object_path"), str):
            _fail("M4_SOURCE_INVALID", "M3 pipeline asset row is invalid")
        object_path = str(row["object_path"])
        if object_path in index:
            _fail("M4_SOURCE_INVALID", f"M3 pipeline repeats asset {object_path}")
        index[object_path] = row
    return provider, index


def _smoke_run_product(
    root: Path,
    report_path: Path,
    run_id: str,
    case: CorpusExecutionCase,
    provider: ProductionManifestProvider,
    assets: Mapping[str, Mapping[str, Any]],
) -> tuple[str, bytes, dict[str, Any], ExecutionSlice, str]:
    try:
        source = provider.load(case.blueprint_object_path)
    except ProjectDocumentError as error:
        _fail(
            "M4_SOURCE_INVALID",
            f"M3 provider cannot load {case.blueprint_object_path}: {error}",
            cause=error,
        )
    typed = compose_typed_document(source)
    row = assets.get(case.blueprint_object_path)
    if row is None:
        _fail("M4_SOURCE_INVALID", f"M3 report lacks {case.blueprint_object_path}")
    typed_relative = row.get("typed_ir_relative_path")
    typed_hash = row.get("typed_ir_sha256")
    if not isinstance(typed_relative, str) or not isinstance(typed_hash, str):
        _fail("M4_SOURCE_INVALID", "M3 typed-IR binding is invalid")
    typed_path = report_path.parent / typed_relative
    if _sha256(typed_path) != typed_hash or typed.typed_ir_sha256 != typed_hash:
        _fail("M4_SOURCE_INVALID", f"typed-IR hash disagreement for {case.case_id} {run_id}")
    result = compute_execution_slice(typed.document, case.criterion_node_id)
    if result.graph_id != case.graph_id:
        _evidence_fail(f"{case.case_id} graph binding disagrees in {run_id}")
    _assert_permutation(typed.document, case.criterion_node_id, result)
    value = build_execution_slice_value(
        typed.document,
        result,
        source_fixture=_relative(root, typed_path),
        source_sha256=typed_hash,
        description=case.description,
    )
    validate_execution_slice_value(typed.document, value, source_path=typed_path)
    payload = canonical_execution_json_bytes(value)
    relative = f"{run_id}/{case.case_id}.slice.v1.json"
    details = {
        "boundaries": _boundaries(result),
        "edge_count": len(result.edge_ids),
        "edge_inclusion_reasons": _reason_map(result, edges=True),
        "node_count": len(result.node_ids),
        "node_inclusion_reasons": _reason_map(result),
        "slice_path": relative,
        "slice_sha256": hashlib.sha256(payload).hexdigest(),
        "source_sha256": typed_hash,
        "value": value,
    }
    return relative, payload, details, result, typed_hash


def _normalized(value: Mapping[str, Any]) -> Mapping[str, Any]:
    return {key: item for key, item in value.items() if key != "source_fixture"}


def _binding(root: Path, path: Path) -> dict[str, str]:
    return {"path": _relative(root, path), "sha256": _sha256(path)}


def _load_evidence_schema() -> Mapping[str, Any]:
    return _load_object(_EVIDENCE_SCHEMA)


def _compose_expected(
    root: Path, registry_path: Path, report_path: Path
) -> tuple[dict[str, bytes], dict[str, Any]]:
    registry = load_execution_criteria(registry_path, project_root=root)
    _, runs = _pipeline_runs(report_path)
    providers: dict[str, ProductionManifestProvider] = {}
    asset_indexes: dict[str, dict[str, Mapping[str, Any]]] = {}
    for run_id in ("run1", "run2"):
        providers[run_id], asset_indexes[run_id] = _run_sources(report_path, runs[run_id])

    products: dict[str, bytes] = {}
    controlled_cases: list[dict[str, Any]] = []
    all_results: list[ExecutionSlice] = []
    for case in registry.controlled_cases:
        relative, payload, report_case, result = _controlled_product(root, case)
        products[relative] = payload
        controlled_cases.append(report_case)
        all_results.append(result)

    smoke_cases: list[dict[str, Any]] = []
    for case in registry.corpus_smoke_cases:
        run_details: dict[str, dict[str, Any]] = {}
        run_hashes: dict[str, str] = {}
        for run_id in ("run1", "run2"):
            relative, payload, details, result, typed_hash = _smoke_run_product(
                root,
                report_path,
                run_id,
                case,
                providers[run_id],
                asset_indexes[run_id],
            )
            products[relative] = payload
            run_details[run_id] = details
            run_hashes[run_id] = typed_hash
            all_results.append(result)
        if run_hashes["run1"] != run_hashes["run2"]:
            _evidence_fail(f"{case.case_id} typed source hashes differ across runs")
        if _normalized(run_details["run1"]["value"]) != _normalized(
            run_details["run2"]["value"]
        ):
            _evidence_fail(f"{case.case_id} run1/run2 normalized slices disagree")
        smoke_cases.append(
            {
                "blueprint_object_path": case.blueprint_object_path,
                "candidate_id": case.candidate_id,
                "case_id": case.case_id,
                "criterion_node_id": case.criterion_node_id,
                "graph_id": case.graph_id,
                "normalized_equal": True,
                **{
                    f"{run_id}_{field}": run_details[run_id][field]
                    for run_id in ("run1", "run2")
                    for field in (
                        "boundaries",
                        "edge_count",
                        "edge_inclusion_reasons",
                        "node_count",
                        "node_inclusion_reasons",
                        "slice_path",
                        "slice_sha256",
                        "source_sha256",
                    )
                },
            }
        )

    if len(all_results) != 19:
        _evidence_fail("M4 packet must aggregate exactly 19 computations")

    selected_nodes = sum(len(result.node_ids) for result in all_results)
    selected_edges = sum(len(result.edge_ids) for result in all_results)
    report = {
        "accountability": {
            "boundaries_valid": True,
            "edges_with_reasons": sum(len(result.edge_inclusion_reasons) for result in all_results),
            "nodes_with_reasons": sum(len(result.inclusion_reasons) for result in all_results),
            "selected_edges": selected_edges,
            "selected_nodes": selected_nodes,
        },
        "claims": {
            "bounded_corpus_smoke_established": True,
            "controlled_exactness_established": True,
            "general_correctness_established": False,
            "layout_or_human_claim_established": False,
            "runtime_causality_established": False,
            "scale_or_performance_established": False,
        },
        "contracts": {
            name: _binding(root, root / relative)
            for name, relative in sorted(_CONTRACT_PATHS.items())
        },
        "controlled": {"cases": controlled_cases, "passed": 3, "total": 3},
        "corpus_smoke": {
            "cases": smoke_cases,
            "normalized_agreement": 8,
            "run1_passed": 8,
            "run2_passed": 8,
            "total_per_run": 8,
        },
        "criterion_registry": _binding(root, registry_path),
        "m3_pipeline_report": _binding(root, report_path),
        "outcome": "G4_READY_FOR_OWNER_ACCEPTANCE",
        "rules_version": "1.0.0",
        "schema_name": "blueprint-lens-m4-execution-evidence",
        "schema_version": "1.0.0",
        "verification": {
            "input_permutation_passed": 19,
            "input_permutation_total": 19,
            "packet_reverification_passed": True,
            "slice_contract_passed": 19,
            "slice_contract_total": 19,
            "termination_passed": 19,
            "termination_total": 19,
            "two_run_determinism_passed": 8,
            "two_run_determinism_total": 8,
        },
    }
    try:
        validate_instance(report, _load_evidence_schema())
    except Exception as error:
        _evidence_fail(f"generated evidence report violates schema: {error}", cause=error)
    products["execution-evidence.v1.json"] = _canonical_json_bytes(report)
    hashes = "".join(
        f"{hashlib.sha256(payload).hexdigest()}  {relative}\n"
        for relative, payload in sorted(products.items())
    ).encode("utf-8")
    products["hashes.sha256"] = hashes
    return products, report


def _write_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def _owned_staging(target: Path) -> Path:
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        raw = tempfile.mkdtemp(
            prefix=f".{target.name}.", suffix=".staging", dir=target.parent
        )
    except OSError as error:
        _fail("M4_PUBLISH_FAILED", f"cannot create packet staging: {error}", cause=error)
    return Path(raw)


def _remove_staging(staging: Path, target: Path) -> None:
    try:
        if (
            staging.resolve().parent == target.resolve().parent
            and staging.name.startswith(f".{target.name}.")
            and staging.name.endswith(".staging")
        ):
            shutil.rmtree(staging, ignore_errors=True)
    except OSError:
        pass


def _actual_files(root: Path) -> dict[str, bytes]:
    try:
        return {
            path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*")
            if path.is_file()
        }
    except OSError as error:
        _evidence_fail(f"cannot read packet {root}: {error}", cause=error)


def verify_execution_slice_packet(
    destination: str | Path,
    *,
    project_root: str | Path,
) -> Mapping[str, Any]:
    """Independently recompute every retained product and aggregate."""

    packet = Path(destination)
    root = Path(project_root).resolve()
    try:
        actual = _actual_files(packet)
        if set(actual) != {
            *(f"controlled/M4-T{index:02d}.slice.v1.json" for index in range(1, 4)),
            *(f"run1/M4-S{index:02d}.slice.v1.json" for index in range(1, 9)),
            *(f"run2/M4-S{index:02d}.slice.v1.json" for index in range(1, 9)),
            "execution-evidence.v1.json",
            "hashes.sha256",
        }:
            _evidence_fail("packet retained path set is not exact")
        lines = actual["hashes.sha256"].decode("utf-8").splitlines()
        expected_hash_paths = sorted(set(actual) - {"hashes.sha256"})
        parsed: list[tuple[str, str]] = []
        for line in lines:
            parts = line.split("  ", 1)
            if len(parts) != 2:
                _evidence_fail("hash registry line is malformed")
            parsed.append((parts[0], parts[1]))
        if [relative for _, relative in parsed] != expected_hash_paths:
            _evidence_fail("hash registry paths are not exact and sorted")
        for digest, relative in parsed:
            if digest != hashlib.sha256(actual[relative]).hexdigest():
                _evidence_fail(f"packet hash mismatch: {relative}")
        report_value = json.loads(actual["execution-evidence.v1.json"].decode("utf-8"))
        if not isinstance(report_value, dict):
            _evidence_fail("evidence report root is not an object")
        validate_instance(report_value, _load_evidence_schema())
        registry_binding = report_value.get("criterion_registry")
        pipeline_binding = report_value.get("m3_pipeline_report")
        if not isinstance(registry_binding, Mapping) or not isinstance(
            pipeline_binding, Mapping
        ):
            _evidence_fail("evidence source bindings are invalid")
        registry_path = _resolve_binding(root, registry_binding.get("path"), "registry")
        report_path = _resolve_binding(root, pipeline_binding.get("path"), "M3 report")
        if _sha256(registry_path) != registry_binding.get("sha256") or _sha256(
            report_path
        ) != pipeline_binding.get("sha256"):
            _evidence_fail("evidence source binding hash mismatch")
        expected, expected_report = _compose_expected(root, registry_path, report_path)
        if actual != expected:
            differing = sorted(
                relative
                for relative in set(actual) | set(expected)
                if actual.get(relative) != expected.get(relative)
            )
            _evidence_fail(f"packet differs from independent recomputation: {differing}")
        return expected_report
    except M4ExecutionError as error:
        if error.code == "M4_EVIDENCE_MISMATCH":
            raise
        _evidence_fail(f"packet reverification failed: {error}", cause=error)
    except Exception as error:
        _evidence_fail(f"packet reverification failed: {error}", cause=error)


def build_execution_slice_packet(
    *,
    project_root: str | Path,
    registry_path: str | Path,
    m3_pipeline_report_path: str | Path,
    destination: str | Path,
) -> Mapping[str, Any]:
    """Build, reverify and atomically publish the exact 21-file M4 packet."""

    root = Path(project_root).resolve()
    target = Path(destination)
    if target.exists():
        _fail("M4_OUTPUT_EXISTS", f"packet destination already exists: {target}")
    staging = _owned_staging(target)
    try:
        try:
            products, report = _compose_expected(
                root, Path(registry_path).resolve(), Path(m3_pipeline_report_path).resolve()
            )
            for relative, payload in sorted(products.items()):
                _write_bytes(staging / relative, payload)
            verify_execution_slice_packet(staging, project_root=root)
            staging.rename(target)
            return report
        except M4ExecutionError:
            raise
        except OSError as error:
            _fail("M4_PUBLISH_FAILED", f"cannot publish evidence packet: {error}", cause=error)
    finally:
        if staging.exists():
            _remove_staging(staging, target)
