"""M10 RQ3a structural-effect measurement over the frozen M7 corpus."""

from __future__ import annotations

from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import statistics
from typing import Any, Mapping

from ..execution_slice import compute_execution_slice
from ..raw_probe import BlueprintDocument, Graph, load_blueprint_lens_v1
# This private helper is the accepted M7 SCC routine; do not duplicate it here.
from .m7_corpus import _nontrivial_execution_sccs


_MANIFEST_RELATIVE = "fixtures/m7/m7-corpus-manifest.v1.json"
_TYPED_IR_RELATIVE = "artifacts/m7/export/run1/typed-ir"
_REPORT_RELATIVE = "artifacts/m10/structural-effect/structural-effect.v1.json"
_REFUSAL_CODE = "M4_CRITERION_INVALID"
_LIMITATIONS = (
    {
        "id": "ONE_AUTHORED_CORPUS",
        "statement": (
            "This distribution is measured over one retained, project-owned M7 "
            "corpus and should not be generalized beyond that admitted dataset."
        ),
    },
    {
        "id": "STRUCTURAL_NOT_COMPREHENSION",
        "statement": (
            "Node counts, retained fractions and cycle membership describe graph "
            "structure only; they do not establish that a reader understood the result."
        ),
    },
    {
        "id": "BACKWARD_EXECUTION_ONLY",
        "statement": (
            "Every slice is the accepted backward execution context for a node "
            "criterion; bounded forward context is outside this measurement."
        ),
    },
)


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


def _error_code(error: Exception) -> str:
    code = getattr(error, "code", None)
    if isinstance(code, str) and code:
        return code
    text = str(error)
    return text.split(":", 1)[0] if ":" in text else _REFUSAL_CODE


def _distribution(values: list[int]) -> dict[str, Any]:
    ordered = sorted(values)
    return {
        "min": ordered[0],
        "median": round(float(statistics.median(ordered)), 6),
        "max": ordered[-1],
        "mean": round(statistics.fmean(ordered), 6),
    }


def _fraction_distribution(values: list[float]) -> dict[str, Any]:
    ordered = sorted(values)
    return {
        "min": round(ordered[0], 6),
        "median": round(float(statistics.median(ordered)), 6),
        "max": round(ordered[-1], 6),
        "denominator_basis": "graph_node_count",
    }


def _measure_graph(document: BlueprintDocument, graph: Graph, declared: Mapping[str, Any]) -> dict[str, Any]:
    sizes: list[int] = []
    refusal_families: dict[str, Counter[str]] = defaultdict(Counter)
    family_sizes: dict[str, list[int]] = defaultdict(list)
    for node in graph.nodes:
        family = node.class_path
        try:
            result = compute_execution_slice(document, node.id)
        except Exception as error:  # M4 exposes stable error codes at this boundary.
            code = _error_code(error)
            refusal_families[code][family] += 1
            continue
        size = len(result.node_ids)
        sizes.append(size)
        family_sizes[family].append(size)

    refusals = [
        {
            "code": code,
            "count": sum(families.values()),
            "by_node_family": [
                {"class_path": family, "count": count}
                for family, count in sorted(families.items())
            ],
        }
        for code, families in sorted(refusal_families.items())
    ]
    family_counts = Counter(node.class_path for node in graph.nodes)
    by_family = [
        {
            "class_path": family,
            "criteria": count,
            "sliceable": len(family_sizes.get(family, [])),
            "slice_node_median": (
                int(statistics.median(family_sizes[family])) if family_sizes.get(family) else None
            ),
        }
        for family, count in sorted(family_counts.items())
    ]
    components = _nontrivial_execution_sccs(graph)
    scc_nodes = set().union(*components) if components else set()
    # Every SCC node was attempted above.  A returned slice and the M4 kernel's
    # explicit criterion refusal are both terminating outcomes; this field records
    # termination, not successful slice construction.
    terminated_count = len(scc_nodes)
    node_count = len(graph.nodes)
    edge_count = len(graph.edges)
    criteria_sliceable = len(sizes)
    if declared.get("node_count") != node_count or declared.get("edge_count") != edge_count:
        raise ValueError(
            f"manifest count mismatch for {graph.id}: "
            f"{declared.get('node_count')}/{declared.get('edge_count')} != {node_count}/{edge_count}"
        )
    return {
        "graph_id": graph.id,
        "node_count": node_count,
        "edge_count": edge_count,
        "execution": {
            "criteria_total": node_count,
            "criteria_sliceable": criteria_sliceable,
            "criteria_refused": node_count - criteria_sliceable,
            "refusals": refusals,
            "slice_nodes": _distribution(sizes),
            "retained_fraction": _fraction_distribution([size / node_count for size in sizes]),
            "by_node_family": by_family,
        },
        "cycles": {
            "scc_count": len(components),
            "scc_sizes": sorted(len(component) for component in components),
            "criteria_in_scc": len(scc_nodes),
            "criteria_in_scc_terminated": terminated_count,
        },
    }


def _measure(root: Path) -> dict[str, Any]:
    manifest_path = _root_path(root, _MANIFEST_RELATIVE)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    documents = _typed_documents(root)
    rows: list[dict[str, Any]] = []
    for entry in sorted(manifest["candidate_graphs"], key=lambda row: row["graph_id"]):
        graph_id = entry["graph_id"]
        document = documents.get(graph_id)
        if document is None:
            raise KeyError(graph_id)
        graph = next(graph for graph in document.graphs if graph.id == graph_id)
        rows.append(_measure_graph(document, graph, entry["measured"]))
    aggregate = {
        "graph_count": len(rows),
        "node_count": sum(row["node_count"] for row in rows),
        "edge_count": sum(row["edge_count"] for row in rows),
        "criteria_total": sum(row["execution"]["criteria_total"] for row in rows),
        "criteria_sliceable": sum(row["execution"]["criteria_sliceable"] for row in rows),
        "criteria_refused": sum(row["execution"]["criteria_refused"] for row in rows),
        "scc_count": sum(row["cycles"]["scc_count"] for row in rows),
        "criteria_in_scc": sum(row["cycles"]["criteria_in_scc"] for row in rows),
        "criteria_in_scc_terminated": sum(
            row["cycles"]["criteria_in_scc_terminated"] for row in rows
        ),
    }
    return {
        "schema_name": "blueprint-lens-m10-structural-effect",
        "schema_version": "1.0.1",
        "measured_on": "2026-08-23",
        "corpus": {
            "manifest_path": _MANIFEST_RELATIVE,
            "manifest_sha256": _sha256(manifest_path),
            "typed_ir_path": _TYPED_IR_RELATIVE,
            "graph_count": len(rows),
        },
        "graphs": rows,
        "aggregate": aggregate,
        "limitations": list(_LIMITATIONS),
    }


def _unique(errors: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(errors)))


def structural_effect_errors(document: Any) -> tuple[str, ...]:
    """Return stable internal-coherence errors without raising."""
    errors: list[str] = []
    if not isinstance(document, Mapping):
        return ("M10_STRUCTURAL_EFFECT_DOCUMENT_SHAPE_INVALID: root must be an object",)
    required = {"schema_name", "schema_version", "measured_on", "corpus", "graphs", "aggregate", "limitations"}
    missing = sorted(required - set(document))
    if missing:
        return (f"M10_STRUCTURAL_EFFECT_DOCUMENT_SHAPE_INVALID: missing {','.join(missing)}",)
    graphs = document.get("graphs")
    if not isinstance(graphs, list) or not isinstance(document.get("aggregate"), Mapping):
        return ("M10_STRUCTURAL_EFFECT_DOCUMENT_SHAPE_INVALID: graphs and aggregate have invalid shape",)
    try:
        graph_ids = [row["graph_id"] for row in graphs]
        if len(graph_ids) != len(set(graph_ids)):
            errors.append("M10_STRUCTURAL_EFFECT_DOCUMENT_SHAPE_INVALID: duplicate graph_id")
        for row in graphs:
            execution = row["execution"]
            if execution["criteria_total"] != row["node_count"]:
                errors.append(
                    "M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: "
                    f"{row['graph_id']} criteria_total"
                )
            if (
                execution["criteria_sliceable"] + execution["criteria_refused"]
                != execution["criteria_total"]
            ):
                errors.append(
                    "M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: "
                    f"{row['graph_id']} criteria partition"
                )
            if sum(item["count"] for item in execution["refusals"]) != execution["criteria_refused"]:
                errors.append(
                    "M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: "
                    f"{row['graph_id']} refusals"
                )
            fraction = execution["retained_fraction"]
            slice_nodes = execution["slice_nodes"]
            fraction_matches = fraction["denominator_basis"] == "graph_node_count" and all(
                fraction[key] == round(slice_nodes[key] / row["node_count"], 6)
                for key in ("min", "median", "max")
            )
            if not fraction_matches:
                errors.append(
                    "M10_STRUCTURAL_EFFECT_FRACTION_MISMATCH: "
                    f"{row['graph_id']} retained_fraction"
                )
            cycles = row["cycles"]
            if len(cycles["scc_sizes"]) != cycles["scc_count"]:
                errors.append(
                    "M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: "
                    f"{row['graph_id']} scc_sizes"
                )
            if cycles["criteria_in_scc_terminated"] != cycles["criteria_in_scc"]:
                errors.append(
                    "M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: "
                    f"{row['graph_id']} SCC termination"
                )
        aggregate = document["aggregate"]
        fields = ("node_count", "edge_count", "criteria_total", "criteria_sliceable", "criteria_refused")
        for field in fields:
            expected = len(graphs) if field == "graph_count" else sum(
                (row["node_count"] if field == "node_count" else row["edge_count"] if field == "edge_count" else row["execution"][field])
                for row in graphs
            )
            if aggregate.get(field) != expected:
                errors.append(f"M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: aggregate.{field}")
        if aggregate.get("graph_count") != len(graphs):
            errors.append("M10_STRUCTURAL_EFFECT_COUNT_MISMATCH: aggregate.graph_count")
    except (KeyError, TypeError, ZeroDivisionError):
        errors.append("M10_STRUCTURAL_EFFECT_DOCUMENT_SHAPE_INVALID: graph row shape")
    return _unique(errors)


def verify_structural_effect(document: Any, root: Path) -> tuple[str, ...]:
    errors = list(structural_effect_errors(document))
    try:
        fresh = _measure(Path(root))
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        return _unique(errors + [f"M10_STRUCTURAL_EFFECT_GRAPH_MISSING: {error}"])
    if isinstance(document, Mapping):
        actual_ids = {row.get("graph_id") for row in document.get("graphs", []) if isinstance(row, Mapping)}
        fresh_ids = {row["graph_id"] for row in fresh["graphs"]}
        if actual_ids != fresh_ids:
            errors.append("M10_STRUCTURAL_EFFECT_GRAPH_MISSING: graph set differs")
        elif document != fresh:
            errors.append("M10_STRUCTURAL_EFFECT_DRIFT: retained document differs from fresh measurement")
    return _unique(errors)


def build_structural_effect(root: Path, out: Path | None = None) -> int:
    """Build the canonical report, writing only to ``out`` when supplied."""
    target = out or _root_path(Path(root), _REPORT_RELATIVE)
    try:
        value = _measure(Path(root))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(_canonical(value))
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return 1
    return 0


__all__ = ["build_structural_effect", "structural_effect_errors", "verify_structural_effect"]
