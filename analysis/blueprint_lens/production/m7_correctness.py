"""M7 correctness measurement and report verification.

The report compares the accepted M4/M5 slice products with the controller's
ground-truth artefacts.  Task 7 records that the controller and independent
derivation agreed, which is an absence of disagreements rather than a
confirmation that the shared derivation is correct.  The comparison itself is
deliberately small and set-based: it reports every identity that is present
only in the produced slice or only in truth, rather than allowing a
success-only summary.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
import hashlib
import json
from pathlib import Path
from typing import Any

from ..data_slice import compute_member_variable_data_slice
from ..execution_slice import compute_execution_slice
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .m7_adjudication import adjudication_errors


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-m7-correctness-v1.schema.json"
_MEASURED_ON = "2026-08-22"

_LIMITATION_ORDER = (
    "AMBIGUITY_UNTESTED",
    "TRIVIAL_ROWS_IN_THE_DENOMINATOR",
    "ADJUDICATOR_WAS_A_PARTY",
    "INHERITED_TRUTH_NOT_REDERIVED",
    "TOP_BAND_DATA_THIN",
    "SHARED_MISREADING_UNRULED_OUT",
)
_REQUIRED_LIMITATION_STATEMENTS = {
    "TRIVIAL_ROWS_IN_THE_DENOMINATOR": (
        "Eight of the 38 rows contribute two nodes or fewer and one edge or fewer, and "
        "three of them are the queries Round B recorded as not doing what they claim: "
        "M7-Q-C03-EXEC is a criterion with no execution input so its slice is itself, "
        "M7-Q-C09-DATA has 24 Gets and no Set so three of four data rules never fire, and "
        "M7-Q-C09-EXEC stops at depth one behind an opaque call. Those three contribute "
        "27 of the 266 nodes, 10.2 per cent of the node total. An exact-match rate of 38/38 "
        "therefore counts rows that exercise very little, and the aggregate is not a "
        "difficulty-weighted measure."
    ),
    "AMBIGUITY_UNTESTED": (
        "The two derivations agreed, but re-running all 38 under the alternative "
        "reading of the opaque-call ambiguity changes membership on 0 of 38. The "
        "corpus does not exercise the specification's underdetermined regions, "
        "so agreement is evidence about the mechanical core of the rules and "
        "none about the unclear parts."
    ),
    "ADJUDICATOR_WAS_A_PARTY": (
        "The controller produced Round B and also adjudicated. At zero "
        "disagreements nothing turned on it; at one it would have been "
        "disqualifying."
    ),
    "INHERITED_TRUTH_NOT_REDERIVED": (
        "Five ground-truth artefacts inherited from M4 and M5 are "
        "independent_review, not re-derivation — a second party checked an "
        "answer it could see. They are not measured in this report, and their "
        "weaker model must not be read across."
    ),
    "TOP_BAND_DATA_THIN": (
        "The top-band data slice is two nodes because every member in that "
        "generated graph is set exactly once. Top-band data coverage is not "
        "established, whatever the aggregate says."
    ),
    "SHARED_MISREADING_UNRULED_OUT": (
        "Both derivations are language models reading the same frozen text. A "
        "phrasing that misleads one may mislead both identically, and nothing "
        "in this milestone rules that out. Two derivations with one adjudicator "
        "cannot catch a misreading they share."
    ),
}
_REQUIRED_LIMITATION_IDS = frozenset(_LIMITATION_ORDER)


def _unique_sorted(values: Iterable[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def _schema_errors(document: Any) -> tuple[str, ...]:
    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(document, schema)
    except Exception as error:  # pragma: no cover - fail-closed boundary
        return (f"M7_CORRECTNESS_DOCUMENT_SHAPE_INVALID: {error}",)
    return ()


def _fraction(numerator: int, denominator: int) -> dict[str, int]:
    return {"numerator": numerator, "denominator": denominator}


def _measure_side(produced: set[str], truth: set[str]) -> dict[str, Any]:
    true_positive = len(produced & truth)
    false_positive_ids = sorted(produced - truth)
    false_negative_ids = sorted(truth - produced)
    return {
        "true_positive": true_positive,
        "false_positive": len(false_positive_ids),
        "false_negative": len(false_negative_ids),
        "false_positive_ids": false_positive_ids,
        "false_negative_ids": false_negative_ids,
        "precision": _fraction(true_positive, true_positive + len(false_positive_ids)),
        "recall": _fraction(true_positive, true_positive + len(false_negative_ids)),
    }


def measure_one(
    query_id: str,
    slice_kind: str,
    *,
    produced_nodes: set[str],
    produced_edges: set[str],
    truth_nodes: set[str],
    truth_edges: set[str],
) -> dict[str, Any]:
    """Measure one query using only the supplied identity sets."""

    return {
        "query_id": query_id,
        "slice_kind": slice_kind,
        "nodes": _measure_side(set(produced_nodes), set(truth_nodes)),
        "edges": _measure_side(set(produced_edges), set(truth_edges)),
    }


def aggregate_rows(rows: Iterable[Mapping[str, Any]]) -> dict[str, Any]:
    """Recompute aggregate precision, recall and exact-match fractions."""

    row_list = list(rows)
    totals: dict[str, dict[str, int]] = {
        "nodes": {"true_positive": 0, "false_positive": 0, "false_negative": 0},
        "edges": {"true_positive": 0, "false_positive": 0, "false_negative": 0},
    }
    exact_matches = 0
    for row in row_list:
        exact = True
        for side in ("nodes", "edges"):
            values = row[side]
            for field in ("true_positive", "false_positive", "false_negative"):
                totals[side][field] += values[field]
            exact = exact and values["false_positive"] == 0 and values["false_negative"] == 0
        if exact:
            exact_matches += 1

    def rates(side: str) -> dict[str, dict[str, int]]:
        values = totals[side]
        true_positive = values["true_positive"]
        false_positive = values["false_positive"]
        false_negative = values["false_negative"]
        return {
            "precision": _fraction(true_positive, true_positive + false_positive),
            "recall": _fraction(true_positive, true_positive + false_negative),
        }

    return {
        "nodes": rates("nodes"),
        "edges": rates("edges"),
        "exact_match": _fraction(exact_matches, len(row_list)),
    }


def _load_json_object(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"could not read JSON artefact {path}: {error}") from error
    if not isinstance(value, Mapping):
        raise ValueError(f"JSON artefact {path} must be an object")
    return value


def _identity_set(value: Any, label: str) -> set[str]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    if any(not isinstance(item, str) or not item for item in value):
        raise ValueError(f"{label} must contain non-empty identities")
    if len(value) != len(set(value)):
        raise ValueError(f"{label} must not contain duplicate identities")
    return set(value)


def _truth_sets(truth_dir: Path, query_id: str) -> tuple[set[str], set[str]]:
    paths = sorted(truth_dir.rglob(f"{query_id}.ground-truth.v1.json"))
    if len(paths) != 1:
        raise ValueError(
            f"expected exactly one ground-truth artefact for {query_id}, found {len(paths)}"
        )
    document = _load_json_object(paths[0])
    expected = document.get("expected")
    if not isinstance(expected, Mapping):
        raise ValueError(f"ground truth for {query_id} has no expected object")
    node_ids = _identity_set(expected.get("node_ids"), f"{query_id}.expected.node_ids")
    edge_ids = _identity_set(expected.get("edge_ids"), f"{query_id}.expected.edge_ids")
    counts = expected.get("counts")
    if isinstance(counts, Mapping) and (
        counts.get("nodes") != len(node_ids) or counts.get("edges") != len(edge_ids)
    ):
        raise ValueError(f"ground truth counts disagree with identities for {query_id}")
    return node_ids, edge_ids


def _load_typed_documents(typed_ir_dir: Path) -> dict[str, BlueprintDocument]:
    documents: dict[str, BlueprintDocument] = {}
    source_paths: dict[str, Path] = {}
    paths = sorted(typed_ir_dir.rglob("*.json"))
    if not paths:
        raise ValueError(f"typed IR directory contains no JSON documents: {typed_ir_dir}")
    for path in paths:
        document = load_blueprint_lens_v1(path)
        for graph in document.graphs:
            previous = source_paths.get(graph.id)
            if previous is not None and previous != path:
                raise ValueError(f"graph is present in multiple typed IR documents: {graph.id}")
            documents[graph.id] = document
            source_paths[graph.id] = path
    return documents


def _portable_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(_ROOT).as_posix()
    except ValueError:
        return resolved.as_posix()


def _adjudication_basis(
    adjudication_path: Path, query_ids: set[str]
) -> dict[str, Any]:
    document = _load_json_object(adjudication_path)
    errors = adjudication_errors(document)
    if errors:
        raise ValueError("adjudication record is internally invalid: " + "; ".join(errors))
    counts = document.get("counts")
    pairs = document.get("pairs")
    if not isinstance(counts, Mapping) or not isinstance(pairs, list):
        raise ValueError("adjudication record has no counts or pairs")
    pair_ids: set[str] = set()
    for pair in pairs:
        if not isinstance(pair, Mapping) or not isinstance(pair.get("query_id"), str):
            raise ValueError("adjudication pair has an invalid query_id")
        pair_id = pair["query_id"]
        if pair_id in pair_ids:
            raise ValueError(f"adjudication repeats query_id: {pair_id}")
        pair_ids.add(pair_id)
        if pair.get("node_agreement") is not True or pair.get("edge_agreement") is not True:
            raise ValueError(
                f"adjudicated truth is not an agreed pair for {pair_id}; "
                "the correctness harness cannot silently choose a side"
            )
    if pair_ids != query_ids:
        missing = sorted(query_ids - pair_ids)
        extra = sorted(pair_ids - query_ids)
        raise ValueError(
            f"adjudication/query coverage differs (missing={missing!r}, extra={extra!r})"
        )
    if counts.get("pairs") != len(pairs) or counts.get("agreed") != len(pairs):
        raise ValueError("adjudication counts do not describe its agreed pairs")
    return {
        "adjudication_path": _portable_path(adjudication_path),
        "adjudication_sha256": hashlib.sha256(adjudication_path.read_bytes()).hexdigest(),
        "pairs_agreed": counts["agreed"],
        "pairs_total": counts["pairs"],
    }


def _query_measurement(
    query: Mapping[str, Any],
    typed_documents: Mapping[str, BlueprintDocument],
    truth_dir: Path,
) -> dict[str, Any]:
    query_id = query.get("query_id")
    slice_kind = query.get("slice_kind")
    graph_id = query.get("graph_id")
    if not isinstance(query_id, str) or not query_id:
        raise ValueError("query_id must be a non-empty string")
    if slice_kind not in {"execution", "data"}:
        raise ValueError(f"unsupported slice_kind for {query_id}: {slice_kind!r}")
    if not isinstance(graph_id, str) or graph_id not in typed_documents:
        raise ValueError(f"typed IR has no graph for {query_id}: {graph_id!r}")
    document = typed_documents[graph_id]
    if slice_kind == "execution":
        criterion_node_id = query.get("criterion_node_id")
        if not isinstance(criterion_node_id, str) or not criterion_node_id:
            raise ValueError(f"execution criterion is missing for {query_id}")
        produced = compute_execution_slice(document, criterion_node_id)
    else:
        member_guid = query.get("member_guid")
        if not isinstance(member_guid, str) or not member_guid:
            raise ValueError(f"data member GUID is missing for {query_id}")
        produced = compute_member_variable_data_slice(document, graph_id, member_guid)
    if produced.graph_id != graph_id:
        raise ValueError(f"slice graph differs from query for {query_id}")
    truth_nodes, truth_edges = _truth_sets(truth_dir, query_id)
    return measure_one(
        query_id,
        slice_kind,
        produced_nodes=set(produced.node_ids),
        produced_edges=set(produced.edge_ids),
        truth_nodes=truth_nodes,
        truth_edges=truth_edges,
    )


def build_correctness_report(
    queries: Iterable[Mapping[str, Any]],
    truth_dir: str | Path,
    typed_ir_dir: str | Path,
    adjudication_path: str | Path,
) -> dict[str, Any]:
    """Run the accepted kernels and build the canonical M7 correctness report."""

    query_list = list(queries)
    if not query_list:
        raise ValueError("correctness report requires at least one query")
    query_ids = [query.get("query_id") for query in query_list]
    if any(not isinstance(query_id, str) or not query_id for query_id in query_ids):
        raise ValueError("every correctness query requires a non-empty query_id")
    if len(query_ids) != len(set(query_ids)):
        raise ValueError("correctness queries must have unique query_id values")

    truth_root = Path(truth_dir)
    typed_root = Path(typed_ir_dir)
    basis = _adjudication_basis(Path(adjudication_path), set(query_ids))
    typed_documents = _load_typed_documents(typed_root)
    rows = [
        _query_measurement(query, typed_documents, truth_root)
        for query in sorted(query_list, key=lambda item: item["query_id"])
    ]
    return {
        "schema_name": "blueprint-lens-m7-correctness",
        "schema_version": "1.0.0",
        "measured_on": _MEASURED_ON,
        "truth_basis": basis,
        "limitations": [
            {"id": limitation_id, "statement": _REQUIRED_LIMITATION_STATEMENTS[limitation_id]}
            for limitation_id in _LIMITATION_ORDER
        ],
        "rows": rows,
        "aggregate": aggregate_rows(rows),
    }


def _check_rows(document: Mapping[str, Any], errors: list[str]) -> None:
    rows = document["rows"]
    query_ids = [row["query_id"] for row in rows]
    if len(query_ids) != len(set(query_ids)):
        errors.append("M7_CORRECTNESS_DOCUMENT_SHAPE_INVALID: rows repeat a query_id")
    for index, row in enumerate(rows):
        for side in ("nodes", "edges"):
            values = row[side]
            false_positive_ids = values["false_positive_ids"]
            false_negative_ids = values["false_negative_ids"]
            if (
                values["false_positive"] != len(false_positive_ids)
                or values["false_negative"] != len(false_negative_ids)
                or false_positive_ids != sorted(set(false_positive_ids))
                or false_negative_ids != sorted(set(false_negative_ids))
            ):
                errors.append(
                    "M7_CORRECTNESS_IDENTITY_COUNT_MISMATCH: "
                    f"rows[{index}].{side} counts do not match listed identities"
                )
            expected_precision = _fraction(
                values["true_positive"],
                values["true_positive"] + values["false_positive"],
            )
            expected_recall = _fraction(
                values["true_positive"],
                values["true_positive"] + values["false_negative"],
            )
            if (
                values["precision"] != expected_precision
                or values["recall"] != expected_recall
            ):
                errors.append(
                    "M7_CORRECTNESS_AGGREGATE_MISMATCH: "
                    f"rows[{index}].{side} rates do not match row counts"
                )


def correctness_errors(document: Any) -> tuple[str, ...]:
    """Return internal report errors without reading external artefacts."""

    shape_errors = _schema_errors(document)
    if shape_errors:
        return shape_errors
    try:
        errors: list[str] = []
        limitations = document["limitations"]
        limitation_ids = [row["id"] for row in limitations]
        if (
            len(limitation_ids) != len(set(limitation_ids))
            or not _REQUIRED_LIMITATION_IDS.issubset(limitation_ids)
            or any(
                row["statement"] != _REQUIRED_LIMITATION_STATEMENTS.get(row["id"], row["statement"])
                for row in limitations
                if row["id"] in _REQUIRED_LIMITATION_IDS
            )
        ):
            errors.append(
                "M7_CORRECTNESS_LIMITATIONS_MISSING: "
                "the report must carry all required non-softened limitation rows"
            )

        _check_rows(document, errors)
        expected_aggregate = aggregate_rows(document["rows"])
        if document["aggregate"] != expected_aggregate:
            errors.append(
                "M7_CORRECTNESS_AGGREGATE_MISMATCH: aggregate does not match the rows"
            )
        return _unique_sorted(errors)
    except Exception as error:  # pragma: no cover - schema is the normal guard
        return (f"M7_CORRECTNESS_DOCUMENT_SHAPE_INVALID: {error}",)


def _measurement_view(document: Mapping[str, Any]) -> dict[str, Any]:
    return {
        field: document[field]
        for field in (
            "schema_name",
            "schema_version",
            "measured_on",
            "truth_basis",
            "rows",
            "aggregate",
        )
    }


def verify_correctness_report(
    document: Any,
    queries: Iterable[Mapping[str, Any]],
    truth_dir: str | Path,
    typed_ir_dir: str | Path,
    adjudication_path: str | Path,
) -> tuple[str, ...]:
    """Verify internal coherence and re-run the measurement against artefacts."""

    shape_errors = _schema_errors(document)
    if shape_errors:
        return shape_errors
    try:
        expected = build_correctness_report(
            queries, truth_dir, typed_ir_dir, adjudication_path
        )
    except Exception as error:
        return (f"M7_CORRECTNESS_DRIFT: fresh measurement failed: {error}",)
    if _measurement_view(document) != _measurement_view(expected):
        return (
            "M7_CORRECTNESS_DRIFT: report measurement differs from a fresh "
            "kernel/truth comparison",
        )
    return correctness_errors(document)


__all__ = [
    "aggregate_rows",
    "build_correctness_report",
    "correctness_errors",
    "measure_one",
    "verify_correctness_report",
]
