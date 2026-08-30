"""Frozen M3 corpus loading and deterministic structural-risk auditing.

The SCC audit is graph-local and uses source-visible execution edges only. It
does not reuse LC7 fixture-specific truth and does not infer runtime iteration.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping, Sequence

from ..raw_probe import Graph
from ..schema_validation import validate_instance
from .project_documents import ProjectDocument, ProjectDocumentProvider


RISK_DIMENSIONS: tuple[str, ...] = (
    "source_traceability_and_progressive_disclosure",
    "branching_and_incomparable_outcomes",
    "data_provenance_fan_in_fan_out",
    "sequence_async_completion_and_synchronization",
    "call_and_context",
    "opaque_unsupported_and_query_budget_boundaries",
    "cycles_and_multiple_sccs",
    "small_medium_large_scale",
)

SCALE_BANDS: Mapping[str, tuple[int, int]] = MappingProxyType(
    {"small": (1, 10), "medium": (11, 31), "large": (32, 64)}
)


@dataclass(frozen=True, slots=True)
class CorpusAudit:
    regression_asset_count: int
    candidate_graph_count: int
    covered_risk_dimensions: tuple[str, ...]
    scale_band_counts: Mapping[str, int]
    errors: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class _RegressionSpec:
    id: str
    object_path: str

    def as_mapping(self) -> Mapping[str, Any]:
        return {"id": self.id, "object_path": self.object_path}


@dataclass(frozen=True, slots=True)
class _CandidateSpec:
    id: str
    object_path: str
    graph_id: str
    band: str
    risk_dimensions: tuple[str, ...]

    def as_mapping(self) -> Mapping[str, Any]:
        return {
            "id": self.id,
            "object_path": self.object_path,
            "graph_id": self.graph_id,
            "band": self.band,
            "risk_dimensions": list(self.risk_dimensions),
        }


_REGRESSION_SPECS: tuple[_RegressionSpec, ...] = (
    _RegressionSpec("M3-R01", "/Game/LensCorpus/BP_LC1_LongChain.BP_LC1_LongChain"),
    _RegressionSpec("M3-R02", "/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"),
    _RegressionSpec(
        "M3-R03", "/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance"
    ),
    _RegressionSpec(
        "M3-R04",
        "/Game/LensCorpus/BP_LC4_SequenceDisclosure.BP_LC4_SequenceDisclosure",
    ),
    _RegressionSpec(
        "M3-R05", "/Game/LensCorpus/BP_LC4_AsyncBarrier.BP_LC4_AsyncBarrier"
    ),
    _RegressionSpec("M3-R06", "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe"),
    _RegressionSpec(
        "M3-R07", "/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix"
    ),
    _RegressionSpec("M3-R08", "/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC"),
)

_CANDIDATE_SPECS: tuple[_CandidateSpec, ...] = (
    _CandidateSpec(
        "M3-C01",
        "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe",
        "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph",
        "medium",
        (RISK_DIMENSIONS[0],),
    ),
    _CandidateSpec(
        "M3-C02",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:EvaluateEncounter",
        "medium",
        (RISK_DIMENSIONS[1],),
    ),
    _CandidateSpec(
        "M3-C03",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:ComputeReadiness",
        "small",
        (RISK_DIMENSIONS[2],),
    ),
    _CandidateSpec(
        "M3-C04",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main",
        "/Game/LensCorpus/BP_LensCorpus_Main.BP_LensCorpus_Main:CallService",
        "small",
        (RISK_DIMENSIONS[4],),
    ),
    _CandidateSpec(
        "M3-C05",
        "/Game/LensCorpus/BP_LC4_AsyncBarrier.BP_LC4_AsyncBarrier",
        "/Game/LensCorpus/BP_LC4_AsyncBarrier.BP_LC4_AsyncBarrier:EventGraph",
        "medium",
        (RISK_DIMENSIONS[3],),
    ),
    _CandidateSpec(
        "M3-C06",
        "/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix",
        "/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix:EventGraph",
        "medium",
        (RISK_DIMENSIONS[5],),
    ),
    _CandidateSpec(
        "M3-C07",
        "/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC",
        "/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph",
        "small",
        (RISK_DIMENSIONS[6],),
    ),
    _CandidateSpec(
        "M3-C08",
        "/Game/LensCorpus/BP_M3_MultiSCCRisk.BP_M3_MultiSCCRisk",
        "/Game/LensCorpus/BP_M3_MultiSCCRisk.BP_M3_MultiSCCRisk:EventGraph",
        "large",
        (RISK_DIMENSIONS[6], RISK_DIMENSIONS[7]),
    ),
)

_SCHEMA_PATH = (
    Path(__file__).resolve().parents[3] / "schemas" / "blueprint-lens-m3-corpus-v1.schema.json"
)


def load_corpus_manifest(path: str | Path) -> Mapping[str, Any]:
    """Load and schema-check one versioned M3 corpus manifest."""

    source = Path(path)
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise TypeError("root must be an object")
        validate_instance(value, schema)
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError, ValueError) as error:
        raise ValueError(
            f"M3_CORPUS_MANIFEST_INVALID: cannot load {source}: {error}"
        ) from error
    return value


def _rows(manifest: Mapping[str, Any], field: str, errors: list[str]) -> list[Any]:
    value = manifest.get(field)
    if not isinstance(value, (list, tuple)):
        errors.append(f"M3_CORPUS_COLLECTION_INVALID: {field} must be an array")
        return []
    return list(value)


def _index_rows(rows: Sequence[Any], field: str, errors: list[str]) -> dict[str, Mapping[str, Any]]:
    grouped: dict[str, list[Mapping[str, Any]]] = {}
    row_errors: list[str] = []
    for row in rows:
        if not isinstance(row, Mapping):
            row_errors.append(f"M3_CORPUS_ROW_INVALID: {field} contains a non-object row")
            continue
        row_id = row.get("id")
        if not isinstance(row_id, str) or not row_id:
            row_errors.append(f"M3_CORPUS_ROW_INVALID: {field} contains a row without an ID")
            continue
        grouped.setdefault(row_id, []).append(row)

    indexed: dict[str, Mapping[str, Any]] = {}
    for row_id in sorted(grouped):
        candidates = sorted(
            grouped[row_id],
            key=lambda row: json.dumps(dict(row), sort_keys=True, separators=(",", ":")),
        )
        indexed[row_id] = candidates[0]
        if len(candidates) > 1:
            row_errors.append(f"M3_CORPUS_DUPLICATE_ID: {field} repeats {row_id}")
    errors.extend(sorted(row_errors))
    return indexed


def _nontrivial_execution_sccs(graph: Graph) -> tuple[frozenset[str], ...]:
    node_ids = {node.id for node in graph.nodes}
    adjacency: dict[str, set[str]] = {node_id: set() for node_id in node_ids}
    for edge in graph.edges:
        if (
            edge.kind == "execution"
            and edge.source_node_id in node_ids
            and edge.target_node_id in node_ids
        ):
            adjacency[edge.source_node_id].add(edge.target_node_id)

    indices: dict[str, int] = {}
    low_links: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[frozenset[str]] = []
    next_index = 0

    def strong_connect(node_id: str) -> None:
        nonlocal next_index
        indices[node_id] = next_index
        low_links[node_id] = next_index
        next_index += 1
        stack.append(node_id)
        on_stack.add(node_id)

        for successor in sorted(adjacency[node_id]):
            if successor not in indices:
                strong_connect(successor)
                low_links[node_id] = min(low_links[node_id], low_links[successor])
            elif successor in on_stack:
                low_links[node_id] = min(low_links[node_id], indices[successor])

        if low_links[node_id] != indices[node_id]:
            return
        component: set[str] = set()
        while True:
            member = stack.pop()
            on_stack.remove(member)
            component.add(member)
            if member == node_id:
                break
        if len(component) >= 2:
            components.append(frozenset(component))

    for node_id in sorted(node_ids):
        if node_id not in indices:
            strong_connect(node_id)
    return tuple(sorted(components, key=lambda component: tuple(sorted(component))))


def audit_corpus(
    manifest: Mapping[str, Any], documents: ProjectDocumentProvider
) -> CorpusAudit:
    """Audit exact admission, document membership, scale and static execution SCCs."""

    errors: list[str] = []
    regression_rows = _rows(manifest, "regression_assets", errors)
    candidate_rows = _rows(manifest, "candidate_graphs", errors)
    regression_by_id = _index_rows(regression_rows, "regression_assets", errors)
    candidate_by_id = _index_rows(candidate_rows, "candidate_graphs", errors)

    if len(regression_rows) != len(_REGRESSION_SPECS):
        errors.append(
            "M3_CORPUS_REGRESSION_COUNT: "
            f"expected {len(_REGRESSION_SPECS)}, observed {len(regression_rows)}"
        )
    if len(candidate_rows) != len(_CANDIDATE_SPECS):
        errors.append(
            "M3_CORPUS_CANDIDATE_COUNT: "
            f"expected {len(_CANDIDATE_SPECS)}, observed {len(candidate_rows)}"
        )

    try:
        available_asset_ids = set(documents.list_asset_ids())
    except Exception as error:  # provider boundary is intentionally protocol-based
        available_asset_ids = set()
        errors.append(f"M3_CORPUS_PROVIDER_INVALID: cannot list documents: {error}")

    loaded_documents: dict[str, ProjectDocument | None] = {}

    def load_document(asset_id: str) -> ProjectDocument | None:
        if asset_id not in available_asset_ids:
            return None
        if asset_id not in loaded_documents:
            try:
                loaded_documents[asset_id] = documents.load(asset_id)
            except Exception as error:  # provider supplies its own stable detail
                errors.append(
                    f"M3_CORPUS_DOCUMENT_LOAD_FAILED: cannot load {asset_id}: {error}"
                )
                loaded_documents[asset_id] = None
        return loaded_documents[asset_id]

    for spec in _REGRESSION_SPECS:
        row = regression_by_id.get(spec.id)
        if row is None:
            errors.append(f"M3_CORPUS_REGRESSION_MISSING: {spec.id}")
        elif dict(row) != dict(spec.as_mapping()):
            errors.append(f"M3_CORPUS_REGRESSION_ROW_MISMATCH: {spec.id}")
        project_document = load_document(spec.object_path)
        if project_document is None:
            errors.append(
                f"M3_CORPUS_REGRESSION_DOCUMENT_MISSING: {spec.id} {spec.object_path}"
            )
        elif project_document.document.blueprint_path != spec.object_path:
            errors.append(f"M3_CORPUS_REGRESSION_DOCUMENT_MISMATCH: {spec.id}")

    covered_risks: set[str] = set()
    scale_counts = {band: 0 for band in SCALE_BANDS}
    for spec in _CANDIDATE_SPECS:
        row = candidate_by_id.get(spec.id)
        if row is None:
            errors.append(f"M3_CORPUS_CANDIDATE_MISSING: {spec.id}")
            continue
        if dict(row) != dict(spec.as_mapping()):
            errors.append(f"M3_CORPUS_CANDIDATE_ROW_MISMATCH: {spec.id}")

        raw_risks = row.get("risk_dimensions")
        if not isinstance(raw_risks, (list, tuple)):
            errors.append(f"M3_CORPUS_RISK_INVALID: {spec.id} risk_dimensions")
            raw_risks = ()
        for risk in raw_risks:
            if risk in RISK_DIMENSIONS:
                covered_risks.add(risk)
            else:
                errors.append(f"M3_CORPUS_RISK_INVALID: {spec.id} {risk!r}")

        band = row.get("band")
        if band not in SCALE_BANDS:
            errors.append(f"M3_CORPUS_BAND_INVALID: {spec.id} {band!r}")

        object_path = row.get("object_path")
        graph_id = row.get("graph_id")
        if not isinstance(object_path, str) or not isinstance(graph_id, str):
            errors.append(f"M3_CORPUS_CANDIDATE_REFERENCE_INVALID: {spec.id}")
            continue
        project_document = load_document(object_path)
        if project_document is None:
            errors.append(
                f"M3_CORPUS_CANDIDATE_DOCUMENT_MISSING: {spec.id} {object_path}"
            )
            continue
        graph = next(
            (candidate for candidate in project_document.document.graphs if candidate.id == graph_id),
            None,
        )
        if graph is None:
            errors.append(f"M3_CORPUS_GRAPH_MISSING: {spec.id} {graph_id}")
            continue

        if band in SCALE_BANDS:
            scale_counts[band] += 1
            lower, upper = SCALE_BANDS[band]
            if not lower <= len(graph.nodes) <= upper:
                errors.append(
                    f"M3_CORPUS_BAND_MISMATCH: {spec.id} band={band} nodes={len(graph.nodes)}"
                )
        if spec.id == "M3-C08":
            scc_count = len(_nontrivial_execution_sccs(graph))
            if scc_count < 3:
                errors.append(
                    f"M3_CORPUS_C08_SCC_COUNT: expected at least 3, observed {scc_count}"
                )

    for risk in RISK_DIMENSIONS:
        if risk not in covered_risks:
            errors.append(f"M3_CORPUS_RISK_COVERAGE_MISSING: {risk}")
    for band in SCALE_BANDS:
        if scale_counts[band] == 0:
            errors.append(f"M3_CORPUS_SCALE_BAND_MISSING: {band}")

    return CorpusAudit(
        regression_asset_count=len(regression_rows),
        candidate_graph_count=len(candidate_rows),
        covered_risk_dimensions=tuple(
            risk for risk in RISK_DIMENSIONS if risk in covered_risks
        ),
        scale_band_counts=MappingProxyType(dict(scale_counts)),
        errors=tuple(errors),
    )


__all__ = [
    "CorpusAudit",
    "RISK_DIMENSIONS",
    "SCALE_BANDS",
    "audit_corpus",
    "load_corpus_manifest",
]
