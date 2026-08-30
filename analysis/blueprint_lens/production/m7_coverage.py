"""M7 graph x risk-dimension x LC1-LC7 coverage matrix.

The matrix is derived from the admitted M7 manifest and the retained typed-IR
products.  LC membership is a separate, explicitly declared binding table
because the corpus manifest intentionally does not carry LC fields.  The
binding artefacts are checked by their bytes, not by their filenames.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping

from ..schema_validation import validate_instance
from .m7_corpus import (
    RISK_DIMENSIONS,
    TypedIRDirectoryProvider,
    _band_definitions,
    _dimension_exhibited,
)


LC_IDS: tuple[str, ...] = tuple(f"LC{index}" for index in range(1, 8))
LC_EXPECTED_DIMENSIONS: Mapping[str, str] = MappingProxyType(
    {
        "LC1": "source_traceability_and_progressive_disclosure",
        "LC2": "branching_and_incomparable_outcomes",
        "LC3": "data_provenance_fan_in_fan_out",
        "LC4": "sequence_async_completion_and_synchronization",
        "LC5": "call_and_context",
        "LC6": "opaque_unsupported_and_query_budget_boundaries",
        "LC7": "cycles_and_multiple_sccs",
    }
)

_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-m7-coverage-v1.schema.json"


@dataclass(frozen=True, slots=True)
class _LCBindingSpec:
    """One controller-declared LC truth binding."""

    lc: str
    expected_dimension: str
    truth_artefact: str | None
    graph_id: str | None
    reason: str


# This is deliberately an explicit table rather than a filename convention.
# LC4 has two truth directories, so it has two rows.  The LC4 sequence row is a
# regression-only graph; the async row is the one admitted M7 candidate.
_LC_BINDING_SPECS: tuple[_LCBindingSpec, ...] = (
    _LCBindingSpec(
        lc="LC1",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC1"],
        truth_artefact=None,
        graph_id=None,
        reason=(
            "LC1 has no frozen truth directory; artifacts/r1/lc1-* contains "
            "closure, visual-candidate and walkthrough material only"
        ),
    ),
    _LCBindingSpec(
        lc="LC2",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC2"],
        truth_artefact=(
            "artifacts/r1/lc2-guard-truth/BP_LC2_NestedGuards.ir.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards:EventGraph"
        ),
        reason="LC2 frozen guard truth is a regression asset, not an admitted candidate",
    ),
    _LCBindingSpec(
        lc="LC3",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC3"],
        truth_artefact=(
            "artifacts/r1/lc3-value-truth/BP_LC3_ValueProvenance.ir.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance:EventGraph"
        ),
        reason="LC3 frozen value truth is a regression asset, not an admitted candidate",
    ),
    _LCBindingSpec(
        lc="LC4",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC4"],
        truth_artefact=(
            "artifacts/r1/lc4-sequence-truth/"
            "BP_LC4_SequenceDisclosure.ir.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC4_SequenceDisclosure."
            "BP_LC4_SequenceDisclosure:EventGraph"
        ),
        reason=(
            "LC4 sequence truth is a regression asset; only LC4 async is an "
            "admitted candidate"
        ),
    ),
    _LCBindingSpec(
        lc="LC4",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC4"],
        truth_artefact=(
            "artifacts/r1/lc4-async-truth/"
            "BP_LC4_AsyncBarrier.async-profile.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC4_AsyncBarrier."
            "BP_LC4_AsyncBarrier:EventGraph"
        ),
        reason="LC4 async truth binds to the admitted M7 sequence/async candidate",
    ),
    _LCBindingSpec(
        lc="LC5",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC5"],
        truth_artefact=(
            "artifacts/r1/lc5-intra-bp-pure-truth/"
            "BP_SlicingProbe.contextual-slice.v1.json"
        ),
        graph_id=(
            "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery"
        ),
        reason=(
            "LC5 truth is the pure intra-Blueprint CalculateRecovery graph, "
            "not the admitted EventGraph candidate"
        ),
    ),
    _LCBindingSpec(
        lc="LC6",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC6"],
        truth_artefact=(
            "artifacts/r1/lc6-boundary-truth/"
            "BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC6_BoundaryMatrix."
            "BP_LC6_BoundaryMatrix:EventGraph"
        ),
        reason="LC6 frozen boundary truth binds to the admitted M7 candidate",
    ),
    _LCBindingSpec(
        lc="LC7",
        expected_dimension=LC_EXPECTED_DIMENSIONS["LC7"],
        truth_artefact=(
            "artifacts/r1/lc7-static-scc-truth/"
            "BP_LC7_StaticSCC.ir.v1.json"
        ),
        graph_id=(
            "/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph"
        ),
        reason="LC7 frozen static-SCC truth binds to the admitted M7 candidate",
    ),
)


@dataclass(frozen=True, slots=True)
class CoverageCell:
    """One declared candidate graph x risk-dimension cell."""

    row_id: str
    graph_id: str
    dimension: str
    verification: str
    exhibited: bool | None
    node_count: int
    band: str
    reason: str


@dataclass(frozen=True, slots=True)
class LCBinding:
    """One checked LC truth binding."""

    lc: str
    expected_dimension: str
    truth_artefact: str | None
    graph_id: str | None
    candidate_id: str | None
    is_candidate: bool
    artefact_verified: bool
    reason: str


@dataclass(frozen=True, slots=True)
class CoverageMatrix:
    """Pure, fail-closed results for one M7 coverage computation."""

    candidate_graph_count: int
    cells: tuple[CoverageCell, ...]
    lc_bindings: tuple[LCBinding, ...]
    dimension_totals: Mapping[str, int]
    lc_totals: Mapping[str, int]
    gaps: tuple[str, ...]
    errors: tuple[str, ...]


def _unique_sorted(values: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def _candidate_rows(
    manifest: Mapping[str, Any], errors: list[str]
) -> list[Mapping[str, Any]]:
    if not isinstance(manifest, Mapping):
        errors.append("M7_COVERAGE_DOCUMENT_SHAPE_INVALID: manifest must be an object")
        return []
    raw_rows = manifest.get("candidate_graphs")
    if not isinstance(raw_rows, (list, tuple)):
        errors.append(
            "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: candidate_graphs must be an array"
        )
        return []
    rows: list[Mapping[str, Any]] = []
    for index, row in enumerate(raw_rows):
        if not isinstance(row, Mapping):
            errors.append(
                "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                f"candidate_graphs[{index}] must be an object"
            )
            continue
        rows.append(row)
    return rows


def _band_ranges(manifest: Mapping[str, Any], errors: list[str]) -> dict[str, tuple[int, int]]:
    details: list[str] = []
    try:
        ranges, _ = _band_definitions(manifest, details)
    except Exception as error:  # pragma: no cover - defensive provider boundary
        errors.append(
            "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
            f"band_definitions could not be read: {error}"
        )
        return {}
    for detail in details:
        errors.append(f"M7_COVERAGE_DOCUMENT_SHAPE_INVALID: {detail}")
    return ranges


def _graph_index(provider: Any, errors: list[str]) -> dict[str, Any]:
    try:
        graph_ids = sorted(set(provider.list_graph_ids()))
    except Exception as error:  # pragma: no cover - defensive provider boundary
        errors.append(
            "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
            f"typed-IR provider listing failed: {error}"
        )
        return {}

    graphs: dict[str, Any] = {}
    for graph_id in graph_ids:
        try:
            graphs[graph_id] = provider.load_graph(graph_id)
        except Exception:
            # Unadmitted graphs are irrelevant to the matrix.  An admitted row
            # that cannot be loaded is reported by _build_cells below.
            continue
    return graphs


def _build_cells(
    rows: list[Mapping[str, Any]],
    graphs: Mapping[str, Any],
    band_ranges: Mapping[str, tuple[int, int]],
    errors: list[str],
) -> tuple[CoverageCell, ...]:
    cells: list[CoverageCell] = []
    ordered_rows = sorted(
        rows,
        key=lambda row: str(row.get("id", "")),
    )
    for row_index, row in enumerate(ordered_rows):
        row_id = row.get("id")
        graph_id = row.get("graph_id")
        band = row.get("band")
        if not isinstance(row_id, str) or not row_id:
            errors.append(
                "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                f"candidate_graphs row {row_index} has no id"
            )
            continue
        if not isinstance(graph_id, str) or not graph_id:
            errors.append(
                f"M7_COVERAGE_DOCUMENT_SHAPE_INVALID: {row_id} graph_id must be a string"
            )
            continue
        graph = graphs.get(graph_id)
        if graph is None:
            errors.append(
                "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                f"{row_id} graph {graph_id!r} is absent from typed IR"
            )
            continue
        if not isinstance(band, str) or band not in band_ranges:
            errors.append(
                "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                f"{row_id} band must name a declared band"
            )
            row_band = None
            band_value = ""
        else:
            row_band = band
            band_value = band

        raw_dimensions = row.get("risk_dimensions")
        if not isinstance(raw_dimensions, (list, tuple)):
            errors.append(
                "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                f"{row_id} risk_dimensions must be an array"
            )
            continue

        node_count = len(graph.nodes)
        declarations: list[Mapping[str, Any]] = []
        for declaration in raw_dimensions:
            if not isinstance(declaration, Mapping):
                errors.append(
                    "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                    f"{row_id} contains a non-object risk dimension"
                )
                continue
            declarations.append(declaration)

        for declaration in sorted(
            declarations, key=lambda item: str(item.get("dimension", ""))
        ):
            dimension = declaration.get("dimension")
            verification = declaration.get("verification")
            if not isinstance(dimension, str) or dimension not in RISK_DIMENSIONS:
                errors.append(
                    "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                    f"{row_id} has unknown dimension {dimension!r}"
                )
                continue
            if verification not in {"structural", "declared_unverified"}:
                errors.append(
                    "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                    f"{row_id} {dimension} has invalid verification {verification!r}"
                )
                continue

            if verification == "declared_unverified":
                reason_value = declaration.get("unchecked_reason")
                if not isinstance(reason_value, str) or not reason_value:
                    errors.append(
                        "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                        f"{row_id} {dimension} requires unchecked_reason"
                    )
                    continue
                exhibited: bool | None = None
                reason = f"declared_unverified: {reason_value}"
            else:
                try:
                    exhibited = bool(
                        _dimension_exhibited(
                            dimension,
                            graph,
                            node_count,
                            row_band,
                            band_ranges,
                        )
                    )
                except Exception as error:  # pragma: no cover - defensive boundary
                    errors.append(
                        "M7_COVERAGE_DOCUMENT_SHAPE_INVALID: "
                        f"{row_id} {dimension} predicate failed: {error}"
                    )
                    continue
                reason = (
                    "typed-IR structural predicate exhibited"
                    if exhibited
                    else "typed-IR structural predicate not exhibited"
                )

            cells.append(
                CoverageCell(
                    row_id=row_id,
                    graph_id=graph_id,
                    dimension=dimension,
                    verification=verification,
                    exhibited=exhibited,
                    node_count=node_count,
                    band=band_value,
                    reason=reason,
                )
            )
    return tuple(cells)


def _candidate_index(
    rows: list[Mapping[str, Any]],
) -> dict[str, tuple[str, Mapping[str, Any]]]:
    index: dict[str, tuple[str, Mapping[str, Any]]] = {}
    for row in sorted(rows, key=lambda item: str(item.get("id", ""))):
        row_id = row.get("id")
        graph_id = row.get("graph_id")
        if (
            isinstance(row_id, str)
            and row_id
            and isinstance(graph_id, str)
            and graph_id
            and graph_id not in index
        ):
            index[graph_id] = (row_id, row)
    return index


def _check_binding_artefact(
    spec: _LCBindingSpec, errors: list[str]
) -> bool:
    if spec.truth_artefact is None:
        return False
    path = _ROOT / Path(spec.truth_artefact)
    if not path.is_file():
        errors.append(
            "M7_COVERAGE_LC_ARTEFACT_MISSING: "
            f"{spec.lc} {spec.truth_artefact}"
        )
        return False
    try:
        content = path.read_bytes()
    except OSError as error:
        errors.append(
            "M7_COVERAGE_LC_ARTEFACT_MISMATCH: "
            f"{spec.lc} {spec.truth_artefact} could not be read: {error}"
        )
        return False
    graph_bytes = (spec.graph_id or "").encode("utf-8")
    if not graph_bytes or graph_bytes not in content:
        errors.append(
            "M7_COVERAGE_LC_ARTEFACT_MISMATCH: "
            f"{spec.lc} {spec.truth_artefact} does not name {spec.graph_id}"
        )
        return False
    return True


def _build_bindings(
    rows: list[Mapping[str, Any]],
    gaps: list[str],
    errors: list[str],
) -> tuple[LCBinding, ...]:
    candidate_by_graph = _candidate_index(rows)
    bindings: list[LCBinding] = []
    for spec in _LC_BINDING_SPECS:
        artefact_verified = _check_binding_artefact(spec, errors)
        candidate = candidate_by_graph.get(spec.graph_id or "")
        candidate_id = candidate[0] if candidate is not None else None
        is_candidate = candidate is not None

        if spec.truth_artefact is None:
            gaps.append(f"M7_COVERAGE_LC_UNBOUND: {spec.lc}: {spec.reason}")
        elif not is_candidate:
            gaps.append(
                "M7_COVERAGE_LC_NOT_A_CANDIDATE: "
                f"{spec.lc} graph {spec.graph_id} is not an admitted candidate"
            )
        else:
            declarations = candidate[1].get("risk_dimensions")
            dimensions = {
                item.get("dimension")
                for item in declarations
                if isinstance(item, Mapping)
            } if isinstance(declarations, (list, tuple)) else set()
            if spec.expected_dimension not in dimensions:
                gaps.append(
                    "M7_COVERAGE_LC_DIMENSION_DISAGREEMENT: "
                    f"{spec.lc} graph {spec.graph_id} expects "
                    f"{spec.expected_dimension}, candidate declares "
                    f"{sorted(str(item) for item in dimensions)!r}"
                )

        if spec.truth_artefact is None:
            status = "unbound; no truth artefact"
        elif artefact_verified:
            status = "truth artefact names the bound graph"
        else:
            status = "truth artefact could not be verified"
        candidate_status = (
            f"admitted candidate {candidate_id}"
            if candidate_id is not None
            else "not an admitted candidate"
        )
        bindings.append(
            LCBinding(
                lc=spec.lc,
                expected_dimension=spec.expected_dimension,
                truth_artefact=spec.truth_artefact,
                graph_id=spec.graph_id,
                candidate_id=candidate_id,
                is_candidate=is_candidate,
                artefact_verified=artefact_verified,
                reason=f"{spec.reason}; {status}; {candidate_status}",
            )
        )
    return tuple(bindings)


def build_coverage_matrix(
    manifest: Mapping[str, Any], provider: TypedIRDirectoryProvider
) -> CoverageMatrix:
    """Recompute the M7 coverage matrix from a manifest and typed-IR provider."""

    errors: list[str] = []
    gaps: list[str] = []
    rows = _candidate_rows(manifest, errors)
    ranges = _band_ranges(manifest, errors)
    graphs = _graph_index(provider, errors)
    cells = _build_cells(rows, graphs, ranges, errors)
    bindings = _build_bindings(rows, gaps, errors)

    dimension_totals = {
        dimension: sum(
            cell.exhibited is True
            for cell in cells
            if cell.dimension == dimension
        )
        for dimension in RISK_DIMENSIONS
    }
    for dimension in RISK_DIMENSIONS:
        dimension_cells = [cell for cell in cells if cell.dimension == dimension]
        if any(cell.exhibited is True for cell in dimension_cells):
            continue
        if not dimension_cells:
            reason = "no admitted candidate declares this risk dimension"
        elif all(cell.verification == "declared_unverified" for cell in dimension_cells):
            reason = "all admitted declarations are explicitly unverified"
        else:
            reason = "no admitted candidate's typed-IR predicate exhibits it"
        gaps.append(
            "M7_COVERAGE_DIMENSION_NO_REPRESENTATIVE: "
            f"{dimension}: {reason}"
        )

    lc_totals = {
        lc: sum(binding.is_candidate for binding in bindings if binding.lc == lc)
        for lc in LC_IDS
    }
    return CoverageMatrix(
        candidate_graph_count=len(rows),
        cells=cells,
        lc_bindings=bindings,
        dimension_totals=MappingProxyType(
            dict(sorted(dimension_totals.items()))
        ),
        lc_totals=MappingProxyType(dict(sorted(lc_totals.items()))),
        gaps=_unique_sorted(gaps),
        errors=_unique_sorted(errors),
    )


def _cell_document(cell: CoverageCell) -> dict[str, Any]:
    return {
        "band": cell.band,
        "dimension": cell.dimension,
        "exhibited": cell.exhibited,
        "graph_id": cell.graph_id,
        "node_count": cell.node_count,
        "reason": cell.reason,
        "row_id": cell.row_id,
        "verification": cell.verification,
    }


def _binding_document(binding: LCBinding) -> dict[str, Any]:
    return {
        "artefact_verified": binding.artefact_verified,
        "candidate_id": binding.candidate_id,
        "expected_dimension": binding.expected_dimension,
        "graph_id": binding.graph_id,
        "is_candidate": binding.is_candidate,
        "lc": binding.lc,
        "reason": binding.reason,
        "truth_artefact": binding.truth_artefact,
    }


def coverage_matrix_document(matrix: CoverageMatrix) -> dict[str, Any]:
    """Return the canonical JSON-compatible document for a matrix."""

    return {
        "candidate_graph_count": matrix.candidate_graph_count,
        "cells": [_cell_document(cell) for cell in matrix.cells],
        "dimension_totals": dict(matrix.dimension_totals),
        "errors": list(matrix.errors),
        "gaps": list(matrix.gaps),
        "lc_bindings": [_binding_document(binding) for binding in matrix.lc_bindings],
        "lc_totals": dict(matrix.lc_totals),
        "schema_name": "blueprint-lens-m7-coverage",
        "schema_version": "1.0.0",
    }


def _shape_error(document: Any, detail: str) -> tuple[str, ...]:
    return (f"M7_COVERAGE_DOCUMENT_SHAPE_INVALID: {detail}",)


def _validate_document_shape(document: Any) -> tuple[str, ...]:
    if not isinstance(document, dict):
        return _shape_error(document, "root must be an object")
    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(document, schema)
    except Exception as error:
        return _shape_error(document, str(error))
    return ()


def verify_coverage_matrix(
    document: Any, manifest: Mapping[str, Any], provider: Any
) -> tuple[str, ...]:
    """Recompute and compare a matrix document without trusting its contents."""

    shape_errors = _validate_document_shape(document)
    if shape_errors:
        return shape_errors

    try:
        expected = coverage_matrix_document(build_coverage_matrix(manifest, provider))
    except Exception as error:  # pragma: no cover - defensive fail-closed boundary
        return _shape_error(document, f"matrix recomputation failed: {error}")

    errors: list[str] = []
    if document.get("candidate_graph_count") != expected["candidate_graph_count"]:
        errors.append(
            "M7_COVERAGE_TOTAL_MISMATCH: candidate_graph_count differs from recomputation"
        )
    if document.get("dimension_totals") != expected["dimension_totals"]:
        errors.append(
            "M7_COVERAGE_TOTAL_MISMATCH: dimension_totals differ from recomputation"
        )
    if document.get("lc_totals") != expected["lc_totals"]:
        errors.append(
            "M7_COVERAGE_TOTAL_MISMATCH: lc_totals differ from recomputation"
        )

    for field in (
        "candidate_graph_count",
        "cells",
        "dimension_totals",
        "errors",
        "gaps",
        "lc_bindings",
        "lc_totals",
        "schema_name",
        "schema_version",
    ):
        if document.get(field) != expected[field]:
            errors.append(f"M7_COVERAGE_DOCUMENT_DRIFT: {field} differs from recomputation")
    return _unique_sorted(errors)


__all__ = [
    "CoverageCell",
    "CoverageMatrix",
    "LCBinding",
    "LC_EXPECTED_DIMENSIONS",
    "LC_IDS",
    "build_coverage_matrix",
    "coverage_matrix_document",
    "verify_coverage_matrix",
]
