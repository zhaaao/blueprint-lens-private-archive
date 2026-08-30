"""Build and independently reverify the bounded M5 data-slice packet."""

from __future__ import annotations

from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import tempfile
from typing import Any, Mapping, NoReturn

from ..data_slice import MemberVariableDataSlice, compute_member_variable_data_slice
from ..m4_errors import M4ExecutionError
from ..m5_errors import M5DataError
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .data_criteria import (
    ControlledDataCase,
    CorpusDataCase,
    load_data_criteria,
)
from .data_products import (
    build_member_data_slice_value,
    canonical_data_slice_json_bytes,
    validate_member_data_slice_value,
)
from .project_documents import ProductionManifestProvider, ProjectDocumentError
from .typed_documents import compose_typed_document


_ROOT = Path(__file__).resolve().parents[3]
_EVIDENCE_SCHEMA = (
    _ROOT / "schemas" / "blueprint-lens-m5-data-evidence-v1.schema.json"
)
_CONTRACT_PATHS = {
    "criteria_schema": "schemas/blueprint-lens-m5-data-criteria-v1.schema.json",
    "data_rules": "idea-stage/docs/data_slice_specification_v1.md",
    "evidence_schema": "schemas/blueprint-lens-m5-data-evidence-v1.schema.json",
    "ground_truth_schema": "schemas/blueprint-lens-ground-truth-v1.schema.json",
    "slice_schema": "schemas/blueprint-lens-slice-v1.schema.json",
}
_CONTRACT_HASHES = {
    "criteria_schema": "97edc100970c23154eab07c2af484be3717ac07eebed1516fdec50b552f8be95",
    "data_rules": "3e856504226c50983be666847b04d3e7487f72afdf00279ed30e2f81f7a7c022",
    "evidence_schema": "03847120a7b76a2fe3e443b296a011c3f3ef5998facfd6cb2331e52601d98de1",
    "ground_truth_schema": "50664d7a8225e66b40193b68ddbf471adb814b222cd4b5c12b12225d73e32c76",
    "slice_schema": "5b15014b7647223b46b505ad45f5d1cead323115bd79078d48d730d6c50a6818",
}
_EXPECTED_PATHS = {
    *(f"run1/M5-S{index:02d}.slice.v1.json" for index in range(1, 9)),
    *(f"run2/M5-S{index:02d}.slice.v1.json" for index in range(1, 9)),
    "data-evidence.v1.json",
}


def _fail(
    code: str,
    message: str,
    *,
    cause: Exception | None = None,
) -> NoReturn:
    raise M5DataError(code, message, cause=cause)


def _evidence_fail(
    message: str,
    *,
    cause: Exception | None = None,
) -> NoReturn:
    _fail("M5_EVIDENCE_MISMATCH", message, cause=cause)


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail("M5_SOURCE_INVALID", f"cannot hash source {path}: {error}", cause=error)


def _load_object(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            "M5_SOURCE_INVALID",
            f"cannot load JSON source {path}: {error}",
            cause=error,
        )
    if not isinstance(value, dict):
        _fail("M5_SOURCE_INVALID", f"JSON source root is not an object: {path}")
    return value


def _relative(root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        _fail("M5_SOURCE_INVALID", f"source is outside project root: {path}", cause=error)


def _portable_parts(stored: Any, context: str) -> tuple[str, ...]:
    if not isinstance(stored, str) or not stored:
        _evidence_fail(f"{context} path must be non-empty")
    posix = PurePosixPath(stored)
    windows = PureWindowsPath(stored)
    if (
        "\\" in stored
        or posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _evidence_fail(f"{context} path is not portable project-relative: {stored}")
    return posix.parts


def _resolve_binding(root: Path, stored: Any, context: str) -> Path:
    parts = _portable_parts(stored, context)
    path = root.joinpath(*parts).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        _evidence_fail(f"{context} path escapes project root", cause=error)
    if not path.is_file():
        _evidence_fail(f"{context} path does not name a file")
    return path


def _resolve_report_path(
    root: Path,
    report_path: Path,
    stored: Any,
    context: str,
) -> Path:
    if not isinstance(stored, str) or not stored:
        _fail("M5_SOURCE_INVALID", f"{context} path must be non-empty")
    posix = PurePosixPath(stored)
    windows = PureWindowsPath(stored)
    if (
        "\\" in stored
        or posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _fail(
            "M5_SOURCE_INVALID",
            f"{context} path is not portable project-relative: {stored}",
        )
    parts = posix.parts
    path = report_path.parent.joinpath(*parts).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        _fail("M5_SOURCE_INVALID", f"{context} escapes project root", cause=error)
    if not path.is_file():
        _fail("M5_SOURCE_INVALID", f"{context} does not name a file")
    return path


def _reason_map(
    result: MemberVariableDataSlice,
    *,
    edges: bool = False,
) -> dict[str, list[str]]:
    source = result.edge_inclusion_reasons if edges else result.inclusion_reasons
    entity_ids = result.edge_ids if edges else result.node_ids
    return {entity_id: list(source[entity_id]) for entity_id in entity_ids}


def _boundaries(result: MemberVariableDataSlice) -> list[dict[str, Any]]:
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
    document: BlueprintDocument,
    graph_id: str,
    member_guid: str,
    result: MemberVariableDataSlice,
) -> None:
    if (
        compute_member_variable_data_slice(
            _permuted(document),
            graph_id,
            member_guid,
        )
        != result
    ):
        _evidence_fail("input permutation changed member data slice")


def _controlled_product(
    root: Path,
    case: ControlledDataCase,
) -> tuple[dict[str, Any], MemberVariableDataSlice]:
    if _sha256(case.document_path) != case.document_sha256:
        _evidence_fail(f"{case.case_id} controlled document hash drifted")
    if _sha256(case.ground_truth_path) != case.ground_truth_sha256:
        _evidence_fail(f"{case.case_id} controlled truth hash drifted")
    document = load_blueprint_lens_v1(case.document_path)
    truth = _load_object(case.ground_truth_path)
    criterion = truth.get("criterion")
    expected = truth.get("expected")
    if not isinstance(criterion, Mapping) or not isinstance(expected, Mapping):
        _evidence_fail(f"{case.case_id} ground truth lacks criterion/expected")
    graph_id = criterion.get("graph_id")
    member_guid = criterion.get("member_guid")
    member_name = criterion.get("member_name")
    question = criterion.get("question")
    if not all(
        isinstance(value, str) and value
        for value in (graph_id, member_guid, member_name, question)
    ):
        _evidence_fail(f"{case.case_id} controlled criterion is invalid")
    result = compute_member_variable_data_slice(document, graph_id, member_guid)
    expected_counts = expected.get("counts")
    if not isinstance(expected_counts, Mapping):
        _evidence_fail(f"{case.case_id} expected counts are invalid")
    if (
        list(result.node_ids) != expected.get("node_ids")
        or list(result.edge_ids) != expected.get("edge_ids")
        or len(result.node_ids) != expected_counts.get("nodes")
        or len(result.edge_ids) != expected_counts.get("edges")
    ):
        _evidence_fail(f"{case.case_id} controlled membership differs from truth")
    if result.member_name != member_name:
        _evidence_fail(f"{case.case_id} controlled member name disagrees")
    _assert_permutation(document, graph_id, member_guid, result)
    value = build_member_data_slice_value(
        document,
        result,
        source_fixture=_relative(root, case.document_path),
        source_sha256=case.document_sha256,
        question=question,
    )
    validate_member_data_slice_value(document, value, source_path=case.document_path)
    report_case = {
        "boundaries": _boundaries(result),
        "case_id": case.case_id,
        "edge_inclusion_reasons": _reason_map(result, edges=True),
        "expected_edge_count": expected_counts["edges"],
        "expected_node_count": expected_counts["nodes"],
        "ground_truth_sha256": case.ground_truth_sha256,
        "membership_equal": True,
        "node_inclusion_reasons": _reason_map(result),
        "observed_edge_count": len(result.edge_ids),
        "observed_node_count": len(result.node_ids),
        "review_status": case.review_status,
        "source_sha256": case.document_sha256,
    }
    return report_case, result


def _map_typed_failure(error: M4ExecutionError) -> NoReturn:
    code = (
        "M5_TYPED_DOCUMENT_INVALID"
        if error.code == "M4_TYPED_DOCUMENT_INVALID"
        else "M5_SOURCE_INVALID"
    )
    raise M5DataError(code, str(error), cause=error) from error


def _pipeline_runs(
    report_path: Path,
) -> tuple[Mapping[str, Any], dict[str, Mapping[str, Any]]]:
    report = _load_object(report_path)
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        _fail("M5_SOURCE_INVALID", "M3 pipeline report must contain exactly two runs")
    indexed: dict[str, Mapping[str, Any]] = {}
    for raw_run in runs:
        if not isinstance(raw_run, Mapping) or raw_run.get("run_id") not in {
            "run1",
            "run2",
        }:
            _fail("M5_SOURCE_INVALID", "M3 pipeline report run identity is invalid")
        run_id = str(raw_run["run_id"])
        if run_id in indexed:
            _fail("M5_SOURCE_INVALID", f"M3 pipeline report repeats {run_id}")
        indexed[run_id] = raw_run
    if set(indexed) != {"run1", "run2"}:
        _fail("M5_SOURCE_INVALID", "M3 pipeline report must bind run1 and run2")
    return report, indexed


def _run_sources(
    root: Path,
    report_path: Path,
    run: Mapping[str, Any],
) -> tuple[ProductionManifestProvider, dict[str, Mapping[str, Any]]]:
    manifest_path = _resolve_report_path(
        root,
        report_path,
        run.get("batch_manifest_relative_path"),
        "M3 batch manifest",
    )
    declared_hash = run.get("batch_manifest_sha256")
    if _sha256(manifest_path) != declared_hash:
        _fail("M5_SOURCE_INVALID", f"M3 batch manifest hash mismatch: {manifest_path}")
    try:
        provider = ProductionManifestProvider(manifest_path)
    except ProjectDocumentError as error:
        _fail(
            "M5_SOURCE_INVALID",
            f"M3 provider rejected {manifest_path}: {error}",
            cause=error,
        )
    assets = run.get("assets")
    if not isinstance(assets, list):
        _fail("M5_SOURCE_INVALID", "M3 run assets must be an array")
    index: dict[str, Mapping[str, Any]] = {}
    for row in assets:
        if not isinstance(row, Mapping) or not isinstance(row.get("object_path"), str):
            _fail("M5_SOURCE_INVALID", "M3 pipeline asset row is invalid")
        object_path = str(row["object_path"])
        if object_path in index:
            _fail("M5_SOURCE_INVALID", f"M3 pipeline repeats asset {object_path}")
        index[object_path] = row
    return provider, index


def _smoke_run_product(
    root: Path,
    report_path: Path,
    run_id: str,
    case: CorpusDataCase,
    provider: ProductionManifestProvider,
    assets: Mapping[str, Mapping[str, Any]],
) -> tuple[str, bytes, dict[str, Any], MemberVariableDataSlice, str]:
    try:
        source = provider.load(case.blueprint_object_path)
    except ProjectDocumentError as error:
        _fail(
            "M5_SOURCE_INVALID",
            f"M3 provider cannot load {case.blueprint_object_path}: {error}",
            cause=error,
        )
    try:
        typed = compose_typed_document(source)
    except M4ExecutionError as error:
        _map_typed_failure(error)
    row = assets.get(case.blueprint_object_path)
    if row is None:
        _fail("M5_SOURCE_INVALID", f"M3 report lacks {case.blueprint_object_path}")
    typed_hash = row.get("typed_ir_sha256")
    if not isinstance(typed_hash, str):
        _fail("M5_SOURCE_INVALID", "M3 typed-IR hash binding is invalid")
    typed_path = _resolve_report_path(
        root,
        report_path,
        row.get("typed_ir_relative_path"),
        f"{case.case_id} {run_id} typed IR",
    )
    if _sha256(typed_path) != typed_hash or typed.typed_ir_sha256 != typed_hash:
        _fail(
            "M5_SOURCE_INVALID",
            f"typed-IR hash disagreement for {case.case_id} {run_id}",
        )
    result = compute_member_variable_data_slice(
        typed.document,
        case.graph_id,
        case.member_guid,
    )
    if result.graph_id != case.graph_id or result.member_name != case.member_name:
        _evidence_fail(f"{case.case_id} criterion binding disagrees in {run_id}")
    _assert_permutation(typed.document, case.graph_id, case.member_guid, result)
    value = build_member_data_slice_value(
        typed.document,
        result,
        source_fixture=_relative(root, typed_path),
        source_sha256=typed_hash,
        question=case.question,
    )
    validate_member_data_slice_value(
        typed.document,
        value,
        source_path=typed_path,
    )
    payload = canonical_data_slice_json_bytes(value)
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


def _contract_bindings(root: Path) -> dict[str, dict[str, str]]:
    bindings: dict[str, dict[str, str]] = {}
    for name, relative in sorted(_CONTRACT_PATHS.items()):
        path = root / relative
        actual = _sha256(path)
        if actual != _CONTRACT_HASHES[name]:
            _fail(
                "M5_SOURCE_INVALID",
                f"frozen contract hash mismatch for {name}: {actual}",
            )
        bindings[name] = {"path": relative, "sha256": actual}
    return bindings


def _load_evidence_schema() -> Mapping[str, Any]:
    return _load_object(_EVIDENCE_SCHEMA)


def _compose_expected(
    root: Path,
    registry_path: Path,
    report_path: Path,
) -> tuple[dict[str, bytes], dict[str, Any]]:
    registry = load_data_criteria(registry_path, project_root=root)
    resolved_report = report_path.resolve()
    if resolved_report != registry.m3_pipeline_report_path:
        _fail(
            "M5_SOURCE_INVALID",
            "supplied M3 pipeline report differs from registry binding",
        )
    if _sha256(resolved_report) != registry.m3_pipeline_report_sha256:
        _fail("M5_SOURCE_INVALID", "bound M3 pipeline report hash drifted")
    _, runs = _pipeline_runs(resolved_report)
    providers: dict[str, ProductionManifestProvider] = {}
    asset_indexes: dict[str, dict[str, Mapping[str, Any]]] = {}
    for run_id in ("run1", "run2"):
        provider, assets = _run_sources(root, resolved_report, runs[run_id])
        providers[run_id] = provider
        asset_indexes[run_id] = assets

    products: dict[str, bytes] = {}
    controlled_cases: list[dict[str, Any]] = []
    all_results: list[MemberVariableDataSlice] = []
    for case in registry.controlled_cases:
        report_case, result = _controlled_product(root, case)
        controlled_cases.append(report_case)
        all_results.append(result)

    smoke_cases: list[dict[str, Any]] = []
    for case in registry.corpus_smoke_cases:
        run_details: dict[str, dict[str, Any]] = {}
        run_hashes: dict[str, str] = {}
        for run_id in ("run1", "run2"):
            relative, payload, details, result, typed_hash = _smoke_run_product(
                root,
                resolved_report,
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
                "graph_id": case.graph_id,
                "member_guid": case.member_guid,
                "member_name": case.member_name,
                "normalized_equal": True,
                "question": case.question,
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

    if len(all_results) != 18:
        _evidence_fail("M5 packet must aggregate exactly 18 computations")
    selected_nodes = sum(len(result.node_ids) for result in all_results)
    selected_edges = sum(len(result.edge_ids) for result in all_results)
    selected_boundaries = sum(len(result.boundaries) for result in all_results)
    report = {
        "accountability": {
            "boundaries_reported": selected_boundaries,
            "boundaries_valid": True,
            "edges_with_reasons": sum(
                len(result.edge_inclusion_reasons) for result in all_results
            ),
            "nodes_with_reasons": sum(
                len(result.inclusion_reasons) for result in all_results
            ),
            "selected_boundaries": selected_boundaries,
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
        "contracts": _contract_bindings(root),
        "controlled": {"cases": controlled_cases, "passed": 2, "total": 2},
        "corpus_smoke": {
            "cases": smoke_cases,
            "normalized_agreement": 8,
            "run1_passed": 8,
            "run2_passed": 8,
            "total_per_run": 8,
        },
        "criterion_registry": _binding(root, registry_path),
        "m3_pipeline_report": _binding(root, resolved_report),
        "outcome": "G5_READY_FOR_OWNER_ACCEPTANCE",
        "rules_version": "1.0.0",
        "schema_name": "blueprint-lens-m5-data-evidence",
        "schema_version": "1.0.0",
        "verification": {
            "controlled_membership_passed": 2,
            "controlled_membership_total": 2,
            "input_permutation_passed": 18,
            "input_permutation_total": 18,
            "packet_reverification_passed": True,
            "slice_contract_passed": 18,
            "slice_contract_total": 18,
            "termination_passed": 18,
            "termination_total": 18,
            "two_run_determinism_passed": 8,
            "two_run_determinism_total": 8,
        },
    }
    try:
        validate_instance(report, _load_evidence_schema())
    except Exception as error:
        _evidence_fail(
            f"generated evidence report violates schema: {error}",
            cause=error,
        )
    products["data-evidence.v1.json"] = _canonical_json_bytes(report)
    if set(products) != _EXPECTED_PATHS:
        _evidence_fail("generated packet path set is not exact")
    return products, report


def _write_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def _owned_staging(target: Path) -> Path:
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        raw = tempfile.mkdtemp(
            prefix=".data-slice.",
            suffix=".staging",
            dir=target.parent,
        )
    except OSError as error:
        _fail("M5_PUBLISH_FAILED", f"cannot create packet staging: {error}", cause=error)
    return Path(raw)


def _remove_staging(staging: Path, target: Path) -> None:
    try:
        if (
            staging.resolve().parent == target.resolve().parent
            and staging.name.startswith(".data-slice.")
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


def _verify_source_bindings(
    root: Path,
    report: Mapping[str, Any],
) -> tuple[Path, Path]:
    registry_binding = report.get("criterion_registry")
    pipeline_binding = report.get("m3_pipeline_report")
    contracts = report.get("contracts")
    if not isinstance(registry_binding, Mapping) or not isinstance(
        pipeline_binding, Mapping
    ):
        _evidence_fail("evidence source bindings are invalid")
    if not isinstance(contracts, Mapping) or set(contracts) != set(_CONTRACT_PATHS):
        _evidence_fail("evidence contract bindings are invalid")
    registry_path = _resolve_binding(root, registry_binding.get("path"), "registry")
    report_path = _resolve_binding(root, pipeline_binding.get("path"), "M3 report")
    if _sha256(registry_path) != registry_binding.get("sha256"):
        _evidence_fail("criterion registry binding hash mismatch")
    if _sha256(report_path) != pipeline_binding.get("sha256"):
        _evidence_fail("M3 pipeline report binding hash mismatch")
    for name, binding in contracts.items():
        if not isinstance(binding, Mapping):
            _evidence_fail(f"contract binding is invalid: {name}")
        contract_path = _resolve_binding(root, binding.get("path"), name)
        if _sha256(contract_path) != binding.get("sha256"):
            _evidence_fail(f"contract binding hash mismatch: {name}")
    return registry_path, report_path


def _verify_slice_hashes(
    actual: Mapping[str, bytes],
    report: Mapping[str, Any],
) -> None:
    corpus = report.get("corpus_smoke")
    if not isinstance(corpus, Mapping) or not isinstance(corpus.get("cases"), list):
        _evidence_fail("corpus smoke report is invalid")
    observed_paths: set[str] = set()
    for case in corpus["cases"]:
        if not isinstance(case, Mapping):
            _evidence_fail("corpus smoke case is invalid")
        for run_id in ("run1", "run2"):
            relative = case.get(f"{run_id}_slice_path")
            digest = case.get(f"{run_id}_slice_sha256")
            if not isinstance(relative, str) or relative not in actual:
                _evidence_fail("reported slice path is absent from packet")
            if relative in observed_paths:
                _evidence_fail(f"reported slice path is duplicated: {relative}")
            observed_paths.add(relative)
            if hashlib.sha256(actual[relative]).hexdigest() != digest:
                _evidence_fail(f"reported slice hash mismatch: {relative}")
    if observed_paths != _EXPECTED_PATHS - {"data-evidence.v1.json"}:
        _evidence_fail("reported slice paths are not exact")


def verify_data_slice_packet(
    destination: str | Path,
    *,
    project_root: str | Path,
) -> Mapping[str, Any]:
    """Independently recompute every retained M5 product and aggregate."""

    packet = Path(destination)
    root = Path(project_root).resolve()
    try:
        actual = _actual_files(packet)
        if set(actual) != _EXPECTED_PATHS:
            _evidence_fail("packet retained path set is not exact")
        report_value = json.loads(actual["data-evidence.v1.json"].decode("utf-8"))
        if not isinstance(report_value, dict):
            _evidence_fail("evidence report root is not an object")
        validate_instance(report_value, _load_evidence_schema())
        registry_path, report_path = _verify_source_bindings(root, report_value)
        _verify_slice_hashes(actual, report_value)
        expected, expected_report = _compose_expected(
            root,
            registry_path,
            report_path,
        )
        if actual != expected:
            differing = sorted(
                relative
                for relative in set(actual) | set(expected)
                if actual.get(relative) != expected.get(relative)
            )
            _evidence_fail(
                f"packet differs from independent recomputation: {differing}"
            )
        return expected_report
    except M5DataError as error:
        if error.code == "M5_EVIDENCE_MISMATCH":
            raise
        _evidence_fail(f"packet reverification failed: {error}", cause=error)
    except Exception as error:
        _evidence_fail(f"packet reverification failed: {error}", cause=error)


def build_data_slice_packet(
    *,
    project_root: str | Path,
    registry_path: str | Path,
    m3_pipeline_report_path: str | Path,
    destination: str | Path,
) -> Mapping[str, Any]:
    """Build, reverify and atomically publish the exact 17-file M5 packet."""

    root = Path(project_root).resolve()
    target = Path(destination)
    if target.exists():
        _fail("M5_OUTPUT_EXISTS", f"packet destination already exists: {target}")
    staging = _owned_staging(target)
    try:
        try:
            products, report = _compose_expected(
                root,
                Path(registry_path).resolve(),
                Path(m3_pipeline_report_path).resolve(),
            )
            for relative, payload in sorted(products.items()):
                _write_bytes(staging / relative, payload)
            verify_data_slice_packet(staging, project_root=root)
            staging.rename(target)
            return report
        except M5DataError:
            raise
        except OSError as error:
            _fail(
                "M5_PUBLISH_FAILED",
                f"cannot publish evidence packet: {error}",
                cause=error,
            )
    finally:
        if staging.exists():
            _remove_staging(staging, target)
