"""Deterministic selection and verification for the M7 query supplement.

The supplement is deliberately derived from the admitted corpus rather than
from an annotation result.  This module keeps that derivation small and
explicit: execution rows select the deepest supported predecessor chain and
data rows select the member with the most Set nodes.
"""

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from ..raw_probe import Graph, Node, Pin
from ..schema_validation import SchemaValidationError, validate_instance
from .m7_corpus import TypedIRDirectoryProvider
from .m7_truth import query_set_digest


_ROOT = Path(__file__).resolve().parents[3]
_CORPUS_PATH = "fixtures/m7/m7-corpus-manifest.v1.json"
_ORIGINAL_QUERY_PATH = "fixtures/m7/m7-queries.v1.json"
_SUPPLEMENT_SCHEMA_PATH = (
    _ROOT / "schemas" / "blueprint-lens-m7-query-supplement-v1.schema.json"
)
_ORIGINAL_QUERY_SET_SHA256 = (
    "8279ad3638c564f98eda8072d6f4bb769b576fb3589177b5a74dc7b7d1d75cf4"
)
_RISK_DIMENSIONS = frozenset(
    {
        "source_traceability_and_progressive_disclosure",
        "branching_and_incomparable_outcomes",
        "data_provenance_fan_in_fan_out",
        "sequence_async_completion_and_synchronization",
        "call_and_context",
        "opaque_unsupported_and_query_budget_boundaries",
        "cycles_and_multiple_sccs",
        "small_medium_large_scale",
    }
)
_IMPLEMENTED_RULE = {
    "execution": "longest_backward_supported_execution_chain",
    "execution_tie_break": "node_id",
    "data": "member_guid_with_most_sets",
    "data_tie_break": "member_guid",
    "consults_outcomes": False,
}


def _sha256(path: Path) -> str | None:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def _node_index(graph: Graph) -> dict[str, Node]:
    return {node.id: node for node in graph.nodes}


def _pin_index(graph: Graph) -> dict[str, Pin]:
    return {pin.id: pin for node in graph.nodes for pin in node.pins}


def _supported_execution_predecessors(graph: Graph) -> dict[str, tuple[str, ...]]:
    """Index supported execution predecessors for every graph node."""

    nodes = _node_index(graph)
    pins = _pin_index(graph)
    predecessors: dict[str, set[str]] = defaultdict(set)
    for edge in graph.edges:
        if edge.kind != "execution":
            continue
        source = nodes.get(edge.source_node_id)
        target = nodes.get(edge.target_node_id)
        source_pin = pins.get(edge.source_pin_id)
        target_pin = pins.get(edge.target_pin_id)
        if source is None or target is None or source_pin is None or target_pin is None:
            continue
        if (
            source_pin.direction != "output"
            or target_pin.direction != "input"
            or source_pin.kind != "execution"
            or target_pin.kind != "execution"
        ):
            continue
        if source.semantic_status == "supported":
            predecessors[target.id].add(source.id)
    return {
        node_id: tuple(sorted(source_ids))
        for node_id, source_ids in predecessors.items()
    }


def _backward_chain(graph: Graph, node_id: str) -> tuple[str, ...]:
    nodes = _node_index(graph)
    if node_id not in nodes:
        raise KeyError(node_id)
    predecessors = _supported_execution_predecessors(graph)
    chain = [node_id]
    seen = {node_id}
    current = node_id
    while predecessors.get(current):
        predecessor = predecessors[current][0]
        if predecessor in seen:
            break
        chain.append(predecessor)
        seen.add(predecessor)
        current = predecessor
    return tuple(chain)


def backward_supported_depth(graph: Graph, node_id: str) -> int:
    """Return deterministic supported-execution depth before ``node_id``.

    Each step chooses the lexicographically first supported execution
    predecessor.  A repeated node terminates traversal, making the function
    total for cyclic execution graphs.
    """

    return len(_backward_chain(graph, node_id)) - 1


def _has_execution_input(node: Node) -> bool:
    return any(pin.direction == "input" and pin.kind == "execution" for pin in node.pins)


def _candidate_rows(corpus: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    if not isinstance(corpus, Mapping):
        raise ValueError("corpus must be an object")
    rows = corpus.get("candidate_graphs")
    if not isinstance(rows, list):
        raise ValueError("corpus candidate_graphs must be an array")
    candidates: list[Mapping[str, Any]] = []
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, Mapping):
            raise ValueError("corpus candidate_graphs contains a non-object row")
        candidate_id = row.get("id")
        graph_id = row.get("graph_id")
        if not isinstance(candidate_id, str) or not candidate_id:
            raise ValueError("corpus candidate graph has no id")
        if candidate_id in seen:
            raise ValueError(f"duplicate candidate id {candidate_id}")
        if not isinstance(graph_id, str) or not graph_id:
            raise ValueError(f"{candidate_id} has no graph_id")
        seen.add(candidate_id)
        candidates.append(row)
    return sorted(candidates, key=lambda row: str(row["id"]))


def _risk_dimensions(row: Mapping[str, Any]) -> list[str]:
    declarations = row.get("risk_dimensions")
    if not isinstance(declarations, list):
        raise ValueError(f"{row.get('id', '<candidate>')} risk_dimensions must be an array")
    result: list[str] = []
    for declaration in declarations:
        if not isinstance(declaration, Mapping):
            raise ValueError(f"{row.get('id', '<candidate>')} has an invalid risk dimension")
        dimension = declaration.get("dimension")
        if not isinstance(dimension, str) or dimension not in _RISK_DIMENSIONS:
            raise ValueError(f"{row.get('id', '<candidate>')} has an invalid risk dimension")
        if dimension not in result:
            result.append(dimension)
    if not result:
        raise ValueError(f"{row.get('id', '<candidate>')} has no risk dimensions")
    return result


def _execution_criterion(graph: Graph) -> tuple[str, int]:
    candidates = [node for node in graph.nodes if _has_execution_input(node)]
    if not candidates:
        raise ValueError(f"{graph.id} has no execution-input criterion")
    ranked = sorted(
        (
            -backward_supported_depth(graph, node.id),
            node.id,
            node,
        )
        for node in candidates
    )
    _, node_id, _ = ranked[0]
    return node_id, -ranked[0][0]


def _data_member(graph: Graph) -> tuple[str, str]:
    counts: dict[str, int] = defaultdict(int)
    names: dict[str, set[str]] = defaultdict(set)
    for node in graph.nodes:
        symbol = node.symbol
        if not isinstance(symbol, Mapping):
            continue
        guid = symbol.get("guid")
        name = symbol.get("name")
        if symbol.get("access") != "set":
            continue
        if not isinstance(guid, str) or not guid:
            continue
        counts[guid] += 1
        if isinstance(name, str) and name:
            names[guid].add(name)
    if not counts:
        raise ValueError(f"{graph.id} has no Set member")
    guid = min(counts, key=lambda value: (-counts[value], value))
    member_names = sorted(names.get(guid, ()))
    if not member_names:
        raise ValueError(f"{graph.id} Set member {guid} has no name")
    return guid, member_names[0]


def select_supplementary_queries(
    corpus: Mapping[str, Any], provider: TypedIRDirectoryProvider
) -> list[dict[str, Any]]:
    """Derive one execution and one data query for every admitted graph."""

    queries: list[dict[str, Any]] = []
    for row in _candidate_rows(corpus):
        candidate_id = str(row["id"])
        graph_id = str(row["graph_id"])
        graph = provider.load_graph(graph_id)
        dimensions = _risk_dimensions(row)
        query_suffix = candidate_id.removeprefix("M7-")

        criterion_node_id, depth = _execution_criterion(graph)
        queries.append(
            {
                "annotation_scope": "whole_graph",
                "backward_chain_depth": depth,
                "candidate_id": candidate_id,
                "criterion_node_id": criterion_node_id,
                "description": (
                    "Rule-selected execution criterion: the node with the "
                    "longest backward supported-execution chain."
                ),
                "graph_id": graph_id,
                "query_id": f"M7-Q-S-{query_suffix}-EXEC",
                "risk_dimensions": dimensions,
                "slice_kind": "execution",
            }
        )

        member_guid, member_name = _data_member(graph)
        queries.append(
            {
                "annotation_scope": "whole_graph",
                "candidate_id": candidate_id,
                "description": (
                    "Rule-selected data member: the member GUID with the most "
                    "Set nodes."
                ),
                "graph_id": graph_id,
                "member_guid": member_guid,
                "member_name": member_name,
                "query_id": f"M7-Q-S-{query_suffix}-DATA",
                "risk_dimensions": dimensions,
                "slice_kind": "data",
            }
        )
    return queries


def _schema_shape_errors(document: Any) -> list[str]:
    if not _SUPPLEMENT_SCHEMA_PATH.is_file():
        return [
            "M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: supplement schema is missing"
        ]
    try:
        schema = json.loads(_SUPPLEMENT_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(document, schema)
    except (OSError, UnicodeError, json.JSONDecodeError, SchemaValidationError, TypeError, ValueError) as error:
        return [f"M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: {error}"]
    return []


def _rule_is_implemented(rule: Any) -> bool:
    if not isinstance(rule, Mapping):
        return False
    for key, expected in _IMPLEMENTED_RULE.items():
        if rule.get(key) != expected:
            return False
    return isinstance(rule.get("note"), str) and bool(rule["note"].strip())


def _original_query_digest() -> str | None:
    try:
        document = json.loads((_ROOT / _ORIGINAL_QUERY_PATH).read_text(encoding="utf-8"))
        queries = document.get("queries")
        if not isinstance(queries, list):
            return None
        return query_set_digest(queries)
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError, ValueError):
        return None


def _unique_sorted(errors: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(errors)))


def verify_supplement(
    document: Any,
    corpus: Mapping[str, Any],
    provider: TypedIRDirectoryProvider,
) -> tuple[str, ...]:
    """Verify a supplement's shape, freeze pins, rule, and fresh derivation."""

    errors = _schema_shape_errors(document)
    if errors:
        return _unique_sorted(errors)
    if not isinstance(document, Mapping):  # defensive; schema already checked it
        return ("M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: root must be an object",)

    if not _rule_is_implemented(document.get("selection_rule")):
        errors.append(
            "M7_SUPPLEMENT_RULE_INVALID: recorded selection_rule is not implemented"
        )

    supplements = document.get("supplements")
    if not isinstance(supplements, Mapping):
        errors.append(
            "M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: supplements must be an object"
        )
    else:
        if (
            supplements.get("path") != _ORIGINAL_QUERY_PATH
            or supplements.get("query_set_sha256") != _ORIGINAL_QUERY_SET_SHA256
        ):
            errors.append(
                "M7_SUPPLEMENT_DRIFT: original query freeze pin does not match"
            )
        actual_original_digest = _original_query_digest()
        if actual_original_digest != _ORIGINAL_QUERY_SET_SHA256:
            errors.append(
                "M7_SUPPLEMENT_DRIFT: original query file no longer matches its freeze"
            )

    corpus_manifest = document.get("corpus_manifest")
    actual_corpus_digest = _sha256(_ROOT / _CORPUS_PATH)
    if (
        not isinstance(corpus_manifest, Mapping)
        or corpus_manifest.get("path") != _CORPUS_PATH
        or actual_corpus_digest is None
        or corpus_manifest.get("sha256") != actual_corpus_digest
    ):
        errors.append("M7_SUPPLEMENT_DRIFT: corpus manifest pin does not match")

    queries = document.get("queries")
    if not isinstance(queries, list):
        errors.append(
            "M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: queries must be an array"
        )
        return _unique_sorted(errors)

    try:
        actual_query_digest = query_set_digest(queries)
    except (TypeError, ValueError) as error:
        errors.append(f"M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: query digest failed: {error}")
    else:
        if document.get("query_set_sha256") != actual_query_digest:
            errors.append(
                "M7_SUPPLEMENT_DRIFT: supplement query_set_sha256 does not match queries"
            )

    try:
        expected = select_supplementary_queries(corpus, provider)
    except Exception as error:  # provider and corpus are untrusted boundaries
        errors.append(f"M7_SUPPLEMENT_DOCUMENT_SHAPE_INVALID: selection failed: {error}")
    else:
        if queries != expected:
            errors.append(
                "M7_SUPPLEMENT_DRIFT: queries differ from a fresh rule application"
            )

    return _unique_sorted(errors)


__all__ = [
    "backward_supported_depth",
    "select_supplementary_queries",
    "verify_supplement",
]
