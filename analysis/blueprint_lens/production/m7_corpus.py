"""M7 corpus registry loading and typed-IR structural coverage auditing.

The M7 registry is deliberately separate from the frozen M3 audit.  It reads
the retained typed-IR products, checks measured registry fields against those
products, and evaluates each declared risk dimension by its graph predicate.
It records provisional bands and renderer-only dimensions as gaps; this module
does not establish an M7 correctness, coverage, scale, or performance claim.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import json
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping, Sequence

from ..raw_probe import Graph, load_blueprint_lens_v1
from ..schema_validation import validate_instance


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

_UNVERIFIABLE_DIMENSION = "source_traceability_and_progressive_disclosure"
_CHECKABLE_DIMENSIONS = frozenset(RISK_DIMENSIONS) - {_UNVERIFIABLE_DIMENSION}
_SEQUENCE_CLASSES = frozenset(
    {
        "K2Node_ExecutionSequence",
        "K2Node_MultiGate",
        "K2Node_Timeline",
        "K2Node_BaseAsyncTask",
        "K2Node_AsyncAction",
    }
)
_CALL_CLASSES = frozenset(
    {
        "K2Node_CallFunction",
        "K2Node_CallFunctionOnMember",
        "K2Node_CallParentFunction",
        "K2Node_MacroInstance",
    }
)
_BOUNDARY_STATUSES = frozenset({"opaque", "uncertain", "unsupported"})

# Derived, not chosen. Across the eight real authored graphs the highest share
# held by any single node family is 72.7 per cent (M7-C05, where CallFunction
# dominates a small async graph). 0.80 sits above every observed real graph with
# headroom and far below a padded one: the first Task 2 submission offered a
# 270-node graph that was 96.7 per cent K2Node_VariableSet, satisfying every
# predicate with one or two real nodes and representing nothing.
_MAX_SINGLE_FAMILY_SHARE = 0.80

_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-m7-corpus-v1.schema.json"


@dataclass(frozen=True, slots=True)
class M7CorpusAudit:
    """Pure, fail-closed results from auditing one M7 corpus manifest."""

    candidate_graph_count: int
    regression_asset_count: int
    structurally_verified_dimensions: tuple[str, ...]
    declared_unverified_dimensions: tuple[str, ...]
    origin_counts: Mapping[str, int]
    scale_band_counts: Mapping[str, int]
    observed_max_node_count: int
    gaps: tuple[str, ...]
    errors: tuple[str, ...]


class TypedIRDirectoryProvider:
    """Index every graph in a directory of frozen typed-IR documents."""

    def __init__(self, directory: str | Path) -> None:
        source = Path(directory)
        graphs: dict[str, Graph] = {}
        graph_sources: dict[str, Path] = {}
        for path in sorted(source.rglob("*.blueprint-lens-v1.json")):
            document = load_blueprint_lens_v1(path)
            for graph in document.graphs:
                previous = graph_sources.get(graph.id)
                if previous is not None:
                    raise ValueError(
                        "M7_CORPUS_DUPLICATE_GRAPH_ID: "
                        f"{graph.id} occurs in {previous} and {path}"
                    )
                graphs[graph.id] = graph
                graph_sources[graph.id] = path
        self._graphs = graphs

    def list_graph_ids(self) -> tuple[str, ...]:
        """Return all indexed graph identities in deterministic order."""

        return tuple(sorted(self._graphs))

    def load_graph(self, graph_id: str) -> Graph:
        """Return one indexed graph, raising ``KeyError`` when it is absent."""

        return self._graphs[graph_id]


def load_m7_corpus_manifest(path: str | Path) -> Mapping[str, Any]:
    """Load and validate one versioned M7 corpus manifest."""

    source = Path(path)
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise TypeError("root must be an object")
        validate_instance(value, schema)
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError, ValueError) as error:
        raise ValueError(
            f"M7_CORPUS_MANIFEST_INVALID: cannot load {source}: {error}"
        ) from error
    return value


def _rows(manifest: Mapping[str, Any], field: str, errors: list[str]) -> list[Any]:
    value = manifest.get(field)
    if not isinstance(value, (list, tuple)):
        errors.append(f"M7_CORPUS_MANIFEST_SHAPE_INVALID: {field} must be an array")
        return []
    return list(value)


def _index_rows(
    rows: Sequence[Any], field: str, errors: list[str]
) -> dict[str, Mapping[str, Any]]:
    indexed: dict[str, Mapping[str, Any]] = {}
    for row in rows:
        if not isinstance(row, Mapping):
            errors.append(
                f"M7_CORPUS_MANIFEST_SHAPE_INVALID: {field} contains a non-object row"
            )
            continue
        row_id = row.get("id")
        if not isinstance(row_id, str) or not row_id:
            errors.append(
                f"M7_CORPUS_MANIFEST_SHAPE_INVALID: {field} contains a row without an ID"
            )
            continue
        if row_id in indexed:
            errors.append(f"M7_CORPUS_DUPLICATE_ID: {field} repeats {row_id}")
            continue
        indexed[row_id] = row
    return indexed


def _band_definitions(
    manifest: Mapping[str, Any], errors: list[str]
) -> tuple[dict[str, tuple[int, int]], str | None]:
    container = manifest.get("band_definitions")
    if not isinstance(container, Mapping):
        errors.append(
            "M7_CORPUS_MANIFEST_SHAPE_INVALID: band_definitions must be an object"
        )
        return {}, None

    binding = container.get("binding")
    binding_value = binding if isinstance(binding, str) else None
    raw_bands = container.get("bands")
    if not isinstance(raw_bands, (list, tuple)):
        errors.append("M7_CORPUS_MANIFEST_SHAPE_INVALID: bands must be an array")
        return {}, binding_value

    ranges: dict[str, tuple[int, int]] = {}
    ordered: list[tuple[str, int, int]] = []
    for item in raw_bands:
        if not isinstance(item, Mapping):
            errors.append(
                "M7_CORPUS_MANIFEST_SHAPE_INVALID: band definition must be an object"
            )
            continue
        name = item.get("band")
        lower = item.get("min_nodes")
        upper = item.get("max_nodes")
        if (
            not isinstance(name, str)
            or not isinstance(lower, int)
            or isinstance(lower, bool)
            or not isinstance(upper, int)
            or isinstance(upper, bool)
        ):
            errors.append(
                "M7_CORPUS_MANIFEST_SHAPE_INVALID: "
                "band definition requires an integer range"
            )
            continue
        if name in ranges:
            errors.append(f"M7_CORPUS_BAND_OVERLAP: duplicate band {name}")
            continue
        if lower > upper:
            errors.append(
                "M7_CORPUS_BAND_OVERLAP: "
                f"{name} min_nodes={lower} exceeds max_nodes={upper}"
            )
        ranges[name] = (lower, upper)
        ordered.append((name, lower, upper))

    ordered.sort(key=lambda value: (value[1], value[2], value[0]))
    for index, previous in enumerate(ordered):
        for current in ordered[index + 1 :]:
            if current[1] <= previous[2]:
                errors.append(
                    "M7_CORPUS_BAND_OVERLAP: "
                    f"{previous[0]} [{previous[1]}, {previous[2]}] overlaps "
                    f"{current[0]} [{current[1]}, {current[2]}]"
                )

    # Bands must partition the reachable range, not describe the sample. The first
    # Task 2 rebind produced small [5,10], medium [11,33], large [270,270], which
    # leaves 1-4 and 34-269 belonging to no band at all, so a later graph in those
    # ranges is unclassifiable rather than merely unusual.
    if ordered:
        if ordered[0][1] != 1:
            errors.append(
                "M7_CORPUS_BAND_NOT_CONTIGUOUS: "
                f"lowest band {ordered[0][0]} starts at {ordered[0][1]}, not 1"
            )
        for index in range(len(ordered) - 1):
            previous, current = ordered[index], ordered[index + 1]
            if current[1] != previous[2] + 1:
                errors.append(
                    "M7_CORPUS_BAND_NOT_CONTIGUOUS: "
                    f"{previous[0]} ends at {previous[2]} but {current[0]} starts at "
                    f"{current[1]}; nodes {previous[2] + 1}-{current[1] - 1} "
                    "belong to no band"
                )
    return ranges, binding_value


def _node_family_signature(value: Any) -> tuple[tuple[str, int], ...] | None:
    if not isinstance(value, (list, tuple)):
        return None
    result: list[tuple[str, int]] = []
    for item in value:
        if not isinstance(item, Mapping):
            return None
        class_path = item.get("class_path")
        count = item.get("count")
        if (
            not isinstance(class_path, str)
            or not isinstance(count, int)
            or isinstance(count, bool)
        ):
            return None
        result.append((class_path, count))
    return tuple(result)


def _actual_measurement(graph: Graph) -> tuple[int, int, int, int, tuple[tuple[str, int], ...]]:
    execution_edge_count = sum(edge.kind == "execution" for edge in graph.edges)
    data_edge_count = sum(edge.kind == "data" for edge in graph.edges)
    families = Counter(node.class_path for node in graph.nodes)
    return (
        len(graph.nodes),
        len(graph.edges),
        execution_edge_count,
        data_edge_count,
        tuple(sorted(families.items())),
    )


def _check_measurement(
    row_id: str, row: Mapping[str, Any], graph: Graph, errors: list[str]
) -> tuple[int, int, int, int, tuple[tuple[str, int], ...]]:
    actual = _actual_measurement(graph)
    measured = row.get("measured")
    if not isinstance(measured, Mapping):
        errors.append(f"M7_CORPUS_COUNT_MISMATCH: {row_id} measured must be an object")
        return actual

    fields = (
        ("node_count", actual[0]),
        ("edge_count", actual[1]),
        ("execution_edge_count", actual[2]),
        ("data_edge_count", actual[3]),
    )
    for field, expected in fields:
        observed = measured.get(field)
        if (
            not isinstance(observed, int)
            or isinstance(observed, bool)
            or observed != expected
        ):
            errors.append(
                "M7_CORPUS_COUNT_MISMATCH: "
                f"{row_id} {field} expected {expected}, observed {observed!r}"
            )

    observed_families = _node_family_signature(measured.get("node_families"))
    if observed_families != actual[4]:
        errors.append(
            "M7_CORPUS_NODE_FAMILY_MISMATCH: "
            f"{row_id} expected {list(actual[4])!r}, observed {observed_families!r}"
        )
    return actual


def _nontrivial_execution_sccs(graph: Graph) -> tuple[frozenset[str], ...]:
    """Find non-trivial execution SCCs without recursion depth dependence."""

    node_ids = {node.id for node in graph.nodes}
    adjacency: dict[str, set[str]] = {node_id: set() for node_id in node_ids}
    reverse: dict[str, set[str]] = {node_id: set() for node_id in node_ids}
    for edge in graph.edges:
        if (
            edge.kind == "execution"
            and edge.source_node_id in node_ids
            and edge.target_node_id in node_ids
        ):
            adjacency[edge.source_node_id].add(edge.target_node_id)
            reverse[edge.target_node_id].add(edge.source_node_id)

    visited: set[str] = set()
    finishing_order: list[str] = []
    for root in sorted(node_ids):
        if root in visited:
            continue
        stack: list[tuple[str, bool]] = [(root, False)]
        while stack:
            node_id, expanded = stack.pop()
            if expanded:
                finishing_order.append(node_id)
                continue
            if node_id in visited:
                continue
            visited.add(node_id)
            stack.append((node_id, True))
            for successor in sorted(adjacency[node_id], reverse=True):
                if successor not in visited:
                    stack.append((successor, False))

    visited.clear()
    components: list[frozenset[str]] = []
    for root in reversed(finishing_order):
        if root in visited:
            continue
        component: set[str] = set()
        stack = [root]
        visited.add(root)
        while stack:
            node_id = stack.pop()
            component.add(node_id)
            for predecessor in sorted(reverse[node_id], reverse=True):
                if predecessor not in visited:
                    visited.add(predecessor)
                    stack.append(predecessor)
        if len(component) >= 2:
            components.append(frozenset(component))
    return tuple(sorted(components, key=lambda component: tuple(sorted(component))))


def _dominant_family_share(graph: Graph) -> tuple[str, int, float]:
    """Return the largest node family, its count and its share of the graph."""

    if not graph.nodes:
        return "", 0, 0.0
    families = Counter(node.class_path for node in graph.nodes)
    class_path, count = max(families.items(), key=lambda item: (item[1], item[0]))
    return class_path, count, count / len(graph.nodes)


def _class_suffix(class_path: str) -> str:
    return class_path.rsplit(".", 1)[-1]


def _dimension_exhibited(
    dimension: str,
    graph: Graph,
    node_count: int,
    row_band: str | None,
    band_ranges: Mapping[str, tuple[int, int]],
) -> bool | None:
    """Return a predicate result, or ``None`` for the renderer-only dimension."""

    if dimension == _UNVERIFIABLE_DIMENSION:
        return None
    if dimension == "branching_and_incomparable_outcomes":
        outgoing_pins: dict[str, set[str]] = {node.id: set() for node in graph.nodes}
        for edge in graph.edges:
            if edge.kind == "execution" and edge.source_node_id in outgoing_pins:
                outgoing_pins[edge.source_node_id].add(edge.source_pin_id)
        return any(len(pin_ids) >= 2 for pin_ids in outgoing_pins.values())
    if dimension == "data_provenance_fan_in_fan_out":
        incoming: Counter[str] = Counter()
        outgoing: Counter[str] = Counter()
        for edge in graph.edges:
            if edge.kind == "data":
                incoming[edge.target_node_id] += 1
                outgoing[edge.source_node_id] += 1
        return any(count >= 2 for count in incoming.values()) and any(
            count >= 2 for count in outgoing.values()
        )
    if dimension == "sequence_async_completion_and_synchronization":
        return any(
            _class_suffix(node.class_path) in _SEQUENCE_CLASSES for node in graph.nodes
        )
    if dimension == "call_and_context":
        return any(_class_suffix(node.class_path) in _CALL_CLASSES for node in graph.nodes)
    if dimension == "opaque_unsupported_and_query_budget_boundaries":
        return any(node.semantic_status in _BOUNDARY_STATUSES for node in graph.nodes)
    if dimension == "cycles_and_multiple_sccs":
        return bool(_nontrivial_execution_sccs(graph))
    if dimension == "small_medium_large_scale":
        if not band_ranges or row_band is None:
            return False
        greatest_band, (lower, upper) = max(
            band_ranges.items(), key=lambda item: (item[1][0], item[0])
        )
        return row_band == greatest_band and lower <= node_count <= upper
    return False


def _provider_graphs(
    provider: Any, errors: list[str]
) -> tuple[set[str], dict[str, Graph]]:
    try:
        available = set(provider.list_graph_ids())
    except Exception as error:  # provider boundary is intentionally protocol-based
        errors.append(f"M7_CORPUS_GRAPH_MISSING: provider listing failed: {error}")
        return set(), {}

    loaded: dict[str, Graph] = {}
    for graph_id in sorted(available):
        try:
            loaded[graph_id] = provider.load_graph(graph_id)
        except Exception:
            # Loading is repeated below only for admitted rows; a failed unused
            # graph should not make the whole registry appear to have a gap.
            continue
    return available, loaded


def audit_m7_corpus(manifest: Mapping[str, Any], provider: Any) -> M7CorpusAudit:
    """Audit measured registry rows against a typed-IR graph provider.

    The function treats the manifest as untrusted content.  Malformed rows are
    recorded as stable errors where possible and never escape as exceptions.
    """

    errors: list[str] = []
    gaps: list[str] = []
    if not isinstance(manifest, Mapping):
        return M7CorpusAudit(
            candidate_graph_count=0,
            regression_asset_count=0,
            structurally_verified_dimensions=(),
            declared_unverified_dimensions=(),
            origin_counts=MappingProxyType({}),
            scale_band_counts=MappingProxyType({}),
            observed_max_node_count=0,
            gaps=(),
            errors=("M7_CORPUS_MANIFEST_SHAPE_INVALID: manifest must be an object",),
        )

    regression_rows = _rows(manifest, "regression_assets", errors)
    candidate_rows = _rows(manifest, "candidate_graphs", errors)
    _index_rows(regression_rows, "regression_assets", errors)
    candidate_by_id = _index_rows(candidate_rows, "candidate_graphs", errors)
    band_ranges, binding = _band_definitions(manifest, errors)

    if binding == "provisional_pending_task_2":
        gaps.append(
            "M7_CORPUS_BANDS_PROVISIONAL: numeric boundaries await Task 2 rebind"
        )

    available, loaded = _provider_graphs(provider, errors)
    scale_counts: dict[str, int] = {band: 0 for band in band_ranges}
    observed_max_node_count = 0
    structural_declared: set[str] = set()
    structural_failed: set[str] = set()
    declared_unverified: set[str] = set()
    origin_counts: dict[str, int] = {}
    at_scale_origins: dict[str, set[str]] = {}
    top_band_name = (
        max(band_ranges.items(), key=lambda item: (item[1][0], item[0]))[0]
        if band_ranges
        else None
    )

    for row in candidate_rows:
        if not isinstance(row, Mapping):
            continue
        row_id = row.get("id")
        if not isinstance(row_id, str) or not row_id:
            continue
        if candidate_by_id.get(row_id) is not row:
            # A duplicate ID is audited once, using the first deterministic row.
            continue
        graph_id = row.get("graph_id")
        if not isinstance(graph_id, str) or graph_id not in available:
            errors.append(f"M7_CORPUS_GRAPH_MISSING: {row_id} {graph_id!r}")
            continue
        graph = loaded.get(graph_id)
        if graph is None:
            try:
                graph = provider.load_graph(graph_id)
            except Exception as error:
                errors.append(
                    f"M7_CORPUS_GRAPH_MISSING: {row_id} {graph_id!r}: {error}"
                )
                continue

        node_count, _, _, _, _ = _check_measurement(row_id, row, graph, errors)
        observed_max_node_count = max(observed_max_node_count, node_count)

        band = row.get("band")
        if not isinstance(band, str) or band not in band_ranges:
            errors.append(f"M7_CORPUS_BAND_UNDEFINED: {row_id} {band!r}")
            row_band: str | None = None
        else:
            row_band = band
            lower, upper = band_ranges[band]
            if not lower <= node_count <= upper:
                errors.append(
                    "M7_CORPUS_BAND_MISMATCH: "
                    f"{row_id} band={band} nodes={node_count} range=[{lower}, {upper}]"
                )
            scale_counts[band] += 1

        family, family_count, share = _dominant_family_share(graph)
        if share > _MAX_SINGLE_FAMILY_SHARE:
            detail = (
                f"{row_id} {family} is {family_count}/{len(graph.nodes)} "
                f"({share:.1%}) of the graph, above the {_MAX_SINGLE_FAMILY_SHARE:.0%} "
                "ceiling derived from the real corpus"
            )
            # The top band is what a bounded-scale claim rests on, so a padded graph
            # there is refused rather than merely noted. Lower bands are descriptive.
            if row_band is not None and row_band == top_band_name:
                errors.append(f"M7_CORPUS_GRAPH_PADDED: {detail}")
            else:
                gaps.append(f"M7_CORPUS_GRAPH_FAMILY_CONCENTRATED: {detail}")

        provenance = row.get("provenance")
        origin = provenance.get("origin") if isinstance(provenance, Mapping) else None
        if not isinstance(origin, str) or not origin:
            errors.append(f"M7_CORPUS_MANIFEST_SHAPE_INVALID: {row_id} provenance.origin")
            origin = None
        else:
            origin_counts[origin] = origin_counts.get(origin, 0) + 1
            if origin != "authored_in_project":
                reference = provenance.get("source_reference")
                if not isinstance(reference, str) or not reference:
                    errors.append(
                        "M7_CORPUS_PROVENANCE_UNTRACEABLE: "
                        f"{row_id} origin={origin} requires source_reference"
                    )

        raw_dimensions = row.get("risk_dimensions")
        if not isinstance(raw_dimensions, (list, tuple)):
            errors.append(
                f"M7_CORPUS_VERIFICATION_MODE_INVALID: {row_id} risk_dimensions"
            )
            continue
        for declaration in raw_dimensions:
            if not isinstance(declaration, Mapping):
                errors.append(
                    f"M7_CORPUS_VERIFICATION_MODE_INVALID: {row_id} dimension row"
                )
                continue
            dimension = declaration.get("dimension")
            verification = declaration.get("verification")
            if (
                not isinstance(dimension, str)
                or dimension not in RISK_DIMENSIONS
                or not isinstance(verification, str)
                or verification not in {
                "structural",
                "declared_unverified",
                }
            ):
                errors.append(
                    "M7_CORPUS_VERIFICATION_MODE_INVALID: "
                    f"{row_id} dimension={dimension!r} verification={verification!r}"
                )
                continue

            if verification == "structural" and "unchecked_reason" in declaration:
                errors.append(
                    "M7_CORPUS_VERIFICATION_MODE_INVALID: "
                    f"{row_id} structural {dimension} forbids unchecked_reason"
                )
                structural_failed.add(dimension)
                continue

            if verification == "declared_unverified":
                if dimension in _CHECKABLE_DIMENSIONS:
                    errors.append(
                        "M7_CORPUS_VERIFICATION_MODE_INVALID: "
                        f"{row_id} checkable dimension {dimension} cannot be unverified"
                    )
                    structural_failed.add(dimension)
                else:
                    reason = declaration.get("unchecked_reason")
                    if not isinstance(reason, str) or not reason:
                        errors.append(
                            "M7_CORPUS_VERIFICATION_MODE_INVALID: "
                            f"{row_id} {dimension} requires unchecked_reason"
                        )
                        continue
                    declared_unverified.add(dimension)
                    gaps.append(
                        "M7_CORPUS_DIMENSION_UNVERIFIED: "
                        f"{row_id} {dimension}: {reason!r}"
                    )
                continue

            if dimension == _UNVERIFIABLE_DIMENSION:
                errors.append(
                    "M7_CORPUS_VERIFICATION_MODE_INVALID: "
                    f"{row_id} {dimension} has no structural predicate"
                )
                structural_failed.add(dimension)
                continue

            structural_declared.add(dimension)
            if row_band is not None and row_band == top_band_name and origin is not None:
                at_scale_origins.setdefault(dimension, set()).add(origin)
            exhibited = _dimension_exhibited(
                dimension, graph, node_count, row_band, band_ranges
            )
            if not exhibited:
                structural_failed.add(dimension)
                errors.append(
                    "M7_CORPUS_DIMENSION_NOT_EXHIBITED: "
                    f"{row_id} {dimension}"
                )

    if binding == "bound" and band_ranges:
        top_band, (_, top_max) = max(
            band_ranges.items(), key=lambda item: (item[1][0], item[0])
        )
        if top_max < observed_max_node_count:
            errors.append(
                "M7_CORPUS_BANDS_NOT_REBOUND: "
                f"top band {top_band} max_nodes={top_max} "
                f"observed_max_node_count={observed_max_node_count}"
            )

    structurally_verified = structural_declared - structural_failed

    # Decision of 2026-08-21: the corpus admits both generated and sourced graphs,
    # so coverage at scale has to say which it rests on. A dimension with no
    # top-band representative, and one whose only top-band representatives are
    # authored in this project, are different gaps and neither is coverage.
    for dimension in sorted(structurally_verified):
        origins = at_scale_origins.get(dimension, set())
        if not origins:
            gaps.append(
                "M7_CORPUS_DIMENSION_NOT_AT_SCALE: "
                f"{dimension} has no representative in the top band"
            )
        elif origins == {"authored_in_project"}:
            gaps.append(
                "M7_CORPUS_DIMENSION_SYNTHETIC_AT_SCALE: "
                f"{dimension} is represented at scale only by authored_in_project "
                "graphs, which is measured-scale evidence and not representativeness"
            )

    return M7CorpusAudit(
        candidate_graph_count=len(candidate_rows),
        regression_asset_count=len(regression_rows),
        structurally_verified_dimensions=tuple(sorted(structurally_verified)),
        declared_unverified_dimensions=tuple(sorted(declared_unverified)),
        origin_counts=MappingProxyType(dict(sorted(origin_counts.items()))),
        scale_band_counts=MappingProxyType(dict(sorted(scale_counts.items()))),
        observed_max_node_count=observed_max_node_count,
        gaps=tuple(sorted(set(gaps))),
        errors=tuple(sorted(set(errors))),
    )


__all__ = [
    "M7CorpusAudit",
    "RISK_DIMENSIONS",
    "TypedIRDirectoryProvider",
    "audit_m7_corpus",
    "load_m7_corpus_manifest",
]
