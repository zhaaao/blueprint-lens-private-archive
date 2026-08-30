"""M10 LC geometry-capacity measurements over real M7 execution slices."""

from __future__ import annotations

from collections.abc import Mapping
import hashlib
import json
from pathlib import Path
from typing import Any

from ..execution_slice import compute_execution_slice
from ..raw_probe import BlueprintDocument, Graph, load_blueprint_lens_v1


_MANIFEST_RELATIVE = "fixtures/m7/m7-corpus-manifest.v1.json"
_TYPED_IR_RELATIVE = "artifacts/m7/export/run1/typed-ir"
_LC7_TRUTH_RELATIVE = "artifacts/r1/lc7-static-scc-truth"
_REPORT_RELATIVE = "artifacts/m10/lc-capacity/lc-capacity.v1.json"
_MEASURED_ON = "2026-08-24"

INVARIANTS = (
    "collinear_overlap_pairs",
    "out_of_bounds_ids",
    "long_relation_route_ids",
    "max_bends",
    "first_screen_fact_errors",
)
_GRAMMARS = ("LC4", "LC5", "LC6", "LC7")

_REFUSAL_REASONS = {
    "LC4": (
        "The real M7-C05 execution slice contains static nodes and edges, while "
        "lc4_async_visual requires the four-invocation accountable async ledger "
        "with its event, relation, and invocation records. M7-C05 does not produce "
        "that ledger shape, so the target layout is refused."
    ),
    "LC5": (
        "The real M7-C01 execution slices expose graph nodes and edges, while "
        "lc5_visual requires its fixed four-occurrence pure-call ledger with the "
        "binding and internal-relation records. The selected slices do not produce "
        "that authored ledger shape, so the target layout is refused."
    ),
    "LC6": (
        "The real M7-C06 execution slice is one backward execution context, while "
        "lc6_visual requires one four-scenario boundary ledger with query and action "
        "ownership. M7-C06 does not produce that ledger shape, so the target layout "
        "is refused."
    ),
}

_PLANS = {
    "LC4": {
        "graph_id": "/Game/LensCorpus/BP_LC4_AsyncBarrier.BP_LC4_AsyncBarrier:EventGraph",
        "unit_counts": (1, 2, 3, 4),
    },
    "LC5": {
        "graph_id": "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph",
        "unit_counts": (1, 2, 3, 4),
    },
    "LC6": {
        "graph_id": "/Game/LensCorpus/BP_LC6_BoundaryMatrix.BP_LC6_BoundaryMatrix:EventGraph",
        "unit_counts": (1, 2, 3, 5),
    },
    "LC7": {
        "graph_id": "/Game/LensCorpus/BP_LC7_StaticSCC.BP_LC7_StaticSCC:EventGraph",
        "unit_counts": (1, 2, 7, 8),
    },
}

_LIMITATIONS = (
    {
        "id": "GEOMETRY_NOT_COMPREHENSION",
        "statement": (
            "These outcomes are target-geometry checks only; they do not establish "
            "human comprehension or interpretation."
        ),
    },
    {
        "id": "ONE_AUTHORED_CORPUS",
        "statement": (
            "The measurements use one retained, project-owned M7 corpus and are not "
            "a distribution over other authored corpora."
        ),
    },
    {
        "id": "TARGET_NOT_RENDERER",
        "statement": (
            "The checks run against Python target geometry; they do not establish "
            "what Slate or an Unreal renderer paints."
        ),
    },
)


class _CapacityRefusal(ValueError):
    """A real corpus slice cannot be driven by the selected grammar contract."""


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _root_path(root: Path, relative: str) -> Path:
    return root / Path(relative)


def _typed_documents(root: Path) -> dict[str, BlueprintDocument]:
    documents: dict[str, BlueprintDocument] = {}
    directory = _root_path(root, _TYPED_IR_RELATIVE)
    for path in sorted(directory.glob("*.blueprint-lens-v1.json")):
        document = load_blueprint_lens_v1(path)
        for graph in document.graphs:
            documents[graph.id] = document
    return documents


def _manifest(root: Path) -> dict[str, Any]:
    path = _root_path(root, _MANIFEST_RELATIVE)
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or not isinstance(value.get("candidate_graphs"), list):
        raise ValueError("M7 corpus manifest has no candidate_graphs list")
    return value


def _candidate_graphs(manifest: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    rows = manifest["candidate_graphs"]
    result: dict[str, Mapping[str, Any]] = {}
    for row in rows:
        if not isinstance(row, Mapping) or not isinstance(row.get("graph_id"), str):
            raise ValueError("M7 corpus manifest candidate graph has invalid shape")
        result[row["graph_id"]] = row
    return result


def _slice_candidates(document: BlueprintDocument, graph: Graph) -> dict[int, tuple[Any, Any]]:
    candidates: dict[int, tuple[Any, Any]] = {}
    for node in sorted(graph.nodes, key=lambda item: item.id):
        try:
            execution_slice = compute_execution_slice(document, node.id)
        except Exception:
            continue
        unit_count = len(execution_slice.node_ids)
        candidates.setdefault(unit_count, (node, execution_slice))
    return candidates


def _refused_measurement(
    grammar: str,
    unit_count: int,
    source_graph_id: str,
    source_criterion_node_id: str,
    reason: str,
) -> dict[str, Any]:
    evidence = f"{grammar} N={unit_count}: {reason}"
    return {
        "unit_count": unit_count,
        "source_graph_id": source_graph_id,
        "source_criterion_node_id": source_criterion_node_id,
        "laid_out": False,
        "invariants": {
            invariant: {"outcome": "refused", "evidence": evidence}
            for invariant in INVARIANTS
        },
    }


def _lc7_truth_for_slice(root: Path, graph: Graph, execution_slice: Any) -> dict[str, Any]:
    """Bind the existing LC7 scene grammar to the real M7-C07 source IDs."""

    from blueprint_lens.lc7_visual import EXPECTED_RELATIONS, EXPECTED_UNITS, load_lc7_visual_truth

    truth = load_lc7_visual_truth(_root_path(root, _LC7_TRUTH_RELATIVE))
    node_by_guid = {
        node.id.rsplit("::node::", 1)[1]: node
        for node in graph.nodes
        if "::node::" in node.id
    }
    selected_node_ids = set(execution_slice.node_ids)
    units: dict[str, dict[str, Any]] = {}
    raw_unit_ids: set[str] = set()
    for key, (unit_id, title) in EXPECTED_UNITS.items():
        guid = unit_id.rsplit(".", 1)[1]
        node = node_by_guid.get(guid)
        if node is None or node.id not in selected_node_ids:
            raise _CapacityRefusal(f"LC7 source slice is missing the expected {key} node")
        if node.title != title:
            raise _CapacityRefusal(
                f"LC7 source node {node.id} is titled {node.title!r}, not {title!r}"
            )
        raw_unit_ids.add(node.id)
        units[unit_id] = dict(truth["units"][unit_id], source_node_id=node.id)
    if selected_node_ids != raw_unit_ids:
        raise _CapacityRefusal(
            "LC7 source slice has a different node inventory from its eight-unit grammar"
        )

    edge_by_relation: dict[str, Any] = {}
    selected_edge_ids = set(execution_slice.edge_ids)
    for relation_key, (source_key, target_key, _label) in EXPECTED_RELATIONS.items():
        source_guid = EXPECTED_UNITS[source_key][0].rsplit(".", 1)[1]
        target_guid = EXPECTED_UNITS[target_key][0].rsplit(".", 1)[1]
        matches = [
            edge
            for edge in graph.edges
            if edge.id in selected_edge_ids
            and edge.source_node_id.rsplit("::node::", 1)[1] == source_guid
            and edge.target_node_id.rsplit("::node::", 1)[1] == target_guid
        ]
        if len(matches) != 1:
            raise _CapacityRefusal(
                f"LC7 source slice has {len(matches)} edges for relation {relation_key}"
            )
        edge_by_relation[relation_key] = matches[0]
    if set(edge.id for edge in edge_by_relation.values()) != selected_edge_ids:
        raise _CapacityRefusal(
            "LC7 source slice has a different edge inventory from its eight-relation grammar"
        )

    relations: dict[str, dict[str, Any]] = {}
    relation_ids: dict[str, str] = {}
    for relation_key, edge in edge_by_relation.items():
        retained_relation_id = truth["relation_ids"][relation_key]
        retained_relation = truth["relations"][retained_relation_id]
        relations[edge.id] = dict(
            retained_relation,
            source_edge_id=edge.id,
            source_graph_id=graph.id,
            source_node_id=edge.source_node_id,
            target_node_id=edge.target_node_id,
            source_kind=edge.kind,
        )
        relation_ids[relation_key] = edge.id

    return dict(
        truth,
        units=units,
        relations=relations,
        relation_ids=relation_ids,
        binding=dict(
            truth["binding"],
            source_graph_id=graph.id,
            source_criterion_node_id=execution_slice.criterion_node_id,
        ),
    )


def _json_fragment(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def _lc7_measurement(
    root: Path,
    graph: Graph,
    execution_slice: Any,
    unit_count: int,
    source_criterion_node_id: str,
) -> dict[str, Any]:
    from blueprint_lens.lc7_adaptive_visual import (
        CONDITION_IDS,
        adaptive_scene_checks,
        build_scene,
    )

    truth = _lc7_truth_for_slice(root, graph, execution_slice)
    checks_by_condition: dict[str, Mapping[str, Any]] = {}
    for condition_id in CONDITION_IDS:
        scene = build_scene(truth, condition_id)
        checks_by_condition[condition_id] = adaptive_scene_checks(scene, truth)

    overlap = {
        condition_id: checks["geometry"]["collinear_overlap_pairs"]
        for condition_id, checks in checks_by_condition.items()
    }
    out_of_bounds = {
        condition_id: checks["out_of_bounds_ids"]
        for condition_id, checks in checks_by_condition.items()
    }
    long_routes = {
        condition_id: {
            "long_relation_route_ids": checks["long_relation_route_ids"],
            "route_density_errors": checks["route_density_errors"],
        }
        for condition_id, checks in checks_by_condition.items()
    }
    bends = {
        condition_id: checks["geometry"]["max_bends"]
        for condition_id, checks in checks_by_condition.items()
    }
    first_screen = {
        condition_id: checks["first_screen_fact_errors"]
        for condition_id, checks in checks_by_condition.items()
    }

    def result(
        invariant: str,
        broken: bool,
        values: Any,
    ) -> dict[str, str]:
        return {
            "outcome": "broken" if broken else "held",
            "evidence": (
                f"LC7 adaptive target geometry across {','.join(CONDITION_IDS)}; "
                f"{invariant}={_json_fragment(values)}"
            ),
        }

    invariants = {
        "collinear_overlap_pairs": result(
            "collinear_overlap_pairs",
            any(bool(value) for value in overlap.values()),
            overlap,
        ),
        "out_of_bounds_ids": result(
            "out_of_bounds_ids",
            any(bool(value) for value in out_of_bounds.values()),
            out_of_bounds,
        ),
        "long_relation_route_ids": result(
            "long_relation_route_ids",
            any(bool(value["route_density_errors"]) for value in long_routes.values()),
            long_routes,
        ),
        "max_bends": result(
            "max_bends",
            any(value > 3 for value in bends.values()),
            bends,
        ),
        "first_screen_fact_errors": result(
            "first_screen_fact_errors",
            any(bool(value) for value in first_screen.values()),
            first_screen,
        ),
    }
    return {
        "unit_count": unit_count,
        "source_graph_id": graph.id,
        "source_criterion_node_id": source_criterion_node_id,
        "laid_out": True,
        "invariants": invariants,
    }


def _measure_grammar(
    grammar: str,
    root: Path,
    documents: Mapping[str, BlueprintDocument],
    manifest_graphs: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    plan = _PLANS[grammar]
    graph_id = plan["graph_id"]
    if graph_id not in manifest_graphs:
        raise KeyError(f"capacity graph is absent from the M7 manifest: {graph_id}")
    document = documents.get(graph_id)
    if document is None:
        raise KeyError(f"capacity graph has no typed-IR document: {graph_id}")
    graph = next((item for item in document.graphs if item.id == graph_id), None)
    if graph is None:
        raise KeyError(graph_id)
    candidates = _slice_candidates(document, graph)
    measurements: list[dict[str, Any]] = []
    for unit_count in plan["unit_counts"]:
        candidate = candidates.get(unit_count)
        if candidate is None:
            raise ValueError(f"no real M7 execution slice at N={unit_count} for {grammar}")
        node, execution_slice = candidate
        if grammar == "LC7" and unit_count == 8:
            try:
                measurements.append(
                    _lc7_measurement(root, graph, execution_slice, unit_count, node.id)
                )
            except _CapacityRefusal as error:
                measurements.append(
                    _refused_measurement(
                        grammar, unit_count, graph_id, node.id, str(error)
                    )
                )
        elif grammar == "LC7":
            measurements.append(
                _refused_measurement(
                    grammar,
                    unit_count,
                    graph_id,
                    node.id,
                    "The LC7 adaptive grammar requires the complete eight-unit source inventory.",
                )
            )
        else:
            measurements.append(
                _refused_measurement(
                    grammar,
                    unit_count,
                    graph_id,
                    node.id,
                    _REFUSAL_REASONS[grammar],
                )
            )

    return _grammar_summary(grammar, measurements)


def _grammar_summary(grammar: str, measurements: list[dict[str, Any]]) -> dict[str, Any]:
    broken: list[tuple[int, str, str]] = []
    held_counts: list[int] = []
    for measurement in measurements:
        outcomes = measurement["invariants"]
        if all(value["outcome"] == "held" for value in outcomes.values()):
            held_counts.append(measurement["unit_count"])
        for invariant in INVARIANTS:
            if outcomes[invariant]["outcome"] == "broken":
                broken.append(
                    (
                        measurement["unit_count"],
                        invariant,
                        outcomes[invariant]["evidence"],
                    )
                )
    broken.sort(key=lambda item: (item[0], INVARIANTS.index(item[1])))
    first_break = None
    if broken:
        unit_count, invariant, evidence = broken[0]
        first_break = {
            "invariant": invariant,
            "unit_count": unit_count,
            "evidence": evidence,
        }
    maximum = max(measurement["unit_count"] for measurement in measurements)
    held_to = max(held_counts, default=0) if first_break is not None else maximum
    monotone = True
    non_monotone_note = ""
    if first_break is not None and held_counts and max(held_counts) > first_break["unit_count"]:
        monotone = False
        non_monotone_note = (
            f"Held measurements reach N={max(held_counts)} after the first broken "
            f"invariant at N={first_break['unit_count']}; every attempted N is retained."
        )
    return {
        "grammar": grammar,
        "measurements": measurements,
        "first_break": first_break,
        "held_to_at_least": held_to,
        "monotone": monotone,
        "non_monotone_note": non_monotone_note,
    }


def _measure(root: Path) -> dict[str, Any]:
    manifest_path = _root_path(root, _MANIFEST_RELATIVE)
    manifest = _manifest(root)
    manifest_graphs = _candidate_graphs(manifest)
    documents = _typed_documents(root)
    grammars = [
        _measure_grammar(grammar, root, documents, manifest_graphs)
        for grammar in _GRAMMARS
    ]
    return {
        "schema_name": "blueprint-lens-m10-lc-capacity",
        "schema_version": "1.0.0",
        "measured_on": _MEASURED_ON,
        "corpus": {
            "manifest_path": _MANIFEST_RELATIVE,
            "manifest_sha256": _sha256(manifest_path),
            "typed_ir_path": _TYPED_IR_RELATIVE,
            "graph_count": len(manifest_graphs),
            "selection_policy": (
                "For each grammar, use the first criterion by stable node ID at each "
                "declared distinct returned execution-slice size. LC7 geometry is "
                "run only for the real eight-unit M7-C07 slice."
            ),
        },
        "grammars": grammars,
        "limitations": list(_LIMITATIONS),
    }


def _unique(errors: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(errors)))


def _derived_summary(row: Mapping[str, Any]) -> tuple[Any, int, bool, str] | None:
    measurements = row.get("measurements")
    if not isinstance(measurements, list) or not measurements:
        return None
    broken: list[tuple[int, str, str]] = []
    held_counts: list[int] = []
    for measurement in measurements:
        if not isinstance(measurement, Mapping):
            return None
        invariants = measurement.get("invariants")
        if not isinstance(invariants, Mapping):
            return None
        if all(
            isinstance(invariants.get(invariant), Mapping)
            and invariants[invariant].get("outcome") == "held"
            for invariant in INVARIANTS
        ):
            held_counts.append(measurement.get("unit_count"))
        for invariant in INVARIANTS:
            result = invariants.get(invariant)
            if isinstance(result, Mapping) and result.get("outcome") == "broken":
                broken.append(
                    (
                        measurement.get("unit_count"),
                        invariant,
                        result.get("evidence", ""),
                    )
                )
    broken.sort(key=lambda item: (item[0], INVARIANTS.index(item[1])))
    first = None
    if broken:
        unit_count, invariant, evidence = broken[0]
        first = {"invariant": invariant, "unit_count": unit_count, "evidence": evidence}
    maximum = max(measurement.get("unit_count") for measurement in measurements)
    held_to = max(held_counts, default=0) if first is not None else maximum
    monotone = not (first is not None and held_counts and max(held_counts) > first["unit_count"])
    note = (
        f"Held measurements reach N={max(held_counts)} after the first broken "
        f"invariant at N={first['unit_count']}; every attempted N is retained."
        if not monotone
        else ""
    )
    return first, held_to, monotone, note


def _lc_capacity_errors(document: Any) -> tuple[str, ...]:
    """Return stable internal-coherence errors without raising."""

    if not isinstance(document, Mapping):
        return ("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: root must be an object",)
    required = {
        "schema_name",
        "schema_version",
        "measured_on",
        "corpus",
        "grammars",
        "limitations",
    }
    missing = sorted(required - set(document))
    if missing:
        return (
            "M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: missing "
            + ",".join(missing),
        )
    errors: list[str] = []
    if document.get("schema_name") != "blueprint-lens-m10-lc-capacity" or document.get("schema_version") != "1.0.0":
        errors.append("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: schema identity")
    if not isinstance(document.get("corpus"), Mapping):
        errors.append("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: corpus")
    grammars = document.get("grammars")
    limitations = document.get("limitations")
    if not isinstance(grammars, list) or not isinstance(limitations, list):
        errors.append("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: grammars or limitations")
        return _unique(errors)
    grammar_ids = [row.get("grammar") for row in grammars if isinstance(row, Mapping)]
    if len(grammar_ids) != len(grammars) or len(grammar_ids) != len(set(grammar_ids)):
        errors.append("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: grammar rows")
    if set(grammar_ids) != set(_GRAMMARS):
        errors.append("M10_LC_CAPACITY_COUNT_MISMATCH: grammar set")
    required_limitation_ids = {"GEOMETRY_NOT_COMPREHENSION", "ONE_AUTHORED_CORPUS", "TARGET_NOT_RENDERER"}
    limitation_ids = {
        row.get("id") for row in limitations if isinstance(row, Mapping)
    }
    if not required_limitation_ids <= limitation_ids:
        errors.append("M10_LC_CAPACITY_COUNT_MISMATCH: limitations")

    for row in grammars:
        if not isinstance(row, Mapping):
            errors.append("M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: grammar row shape")
            continue
        grammar = row.get("grammar", "?")
        measurements = row.get("measurements")
        if not isinstance(measurements, list):
            errors.append(f"M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: {grammar} measurements")
            continue
        counts = [
            measurement.get("unit_count")
            for measurement in measurements
            if isinstance(measurement, Mapping)
        ]
        if len(counts) != len(measurements) or len(counts) != len(set(counts)) or counts != sorted(counts):
            errors.append(f"M10_LC_CAPACITY_COUNT_MISMATCH: {grammar} unit counts")
        for measurement in measurements:
            if not isinstance(measurement, Mapping):
                errors.append(f"M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: {grammar} measurement shape")
                continue
            invariants = measurement.get("invariants")
            if not isinstance(invariants, Mapping) or set(invariants) != set(INVARIANTS):
                errors.append(f"M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: {grammar} invariant set")
                continue
            outcomes = {
                value.get("outcome")
                for value in invariants.values()
                if isinstance(value, Mapping)
            }
            if not all(
                isinstance(invariants[invariant], Mapping)
                and invariants[invariant].get("outcome") in {"held", "broken", "refused"}
                and isinstance(invariants[invariant].get("evidence"), str)
                and invariants[invariant].get("evidence", "").strip()
                for invariant in INVARIANTS
            ):
                errors.append(f"M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: {grammar} invariant result")
            if measurement.get("laid_out") is False and outcomes != {"refused"}:
                errors.append(f"M10_LC_CAPACITY_DRIFT: {grammar} refusal is not fail-closed")
        derived = _derived_summary(row)
        if derived is not None:
            expected_first, expected_held_to, expected_monotone, expected_note = derived
            if row.get("first_break") != expected_first:
                errors.append(f"M10_LC_CAPACITY_DRIFT: {grammar} first_break")
            if row.get("held_to_at_least") != expected_held_to:
                errors.append(f"M10_LC_CAPACITY_DRIFT: {grammar} held_to_at_least")
            if row.get("monotone") != expected_monotone or row.get("non_monotone_note") != expected_note:
                errors.append(f"M10_LC_CAPACITY_DRIFT: {grammar} monotonicity")
    return _unique(errors)


def lc_capacity_errors(document: Any) -> tuple[str, ...]:
    """Return stable shape, count, and derived-value errors without raising."""

    try:
        return _lc_capacity_errors(document)
    except Exception as error:
        return (
            "M10_LC_CAPACITY_DOCUMENT_SHAPE_INVALID: invalid document shape "
            f"({type(error).__name__})",
        )


def build_lc_capacity(root: Path, out: Path | None = None) -> int:
    """Build the canonical capacity report, writing only to ``out`` when supplied."""

    target = Path(out) if out is not None else _root_path(Path(root), _REPORT_RELATIVE)
    try:
        value = _measure(Path(root))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(_canonical(value))
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError, ImportError):
        return 1
    return 0


__all__ = ["build_lc_capacity", "lc_capacity_errors"]
