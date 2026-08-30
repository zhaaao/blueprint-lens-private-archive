"""Audits for the M7 ground-truth registry and frozen query list.

The registry deliberately records the independence model instead of inferring it
from a review-status string.  The query audit separately pins the corpus and
recomputes the digest over the exact rows that were frozen before annotation.
Both public audits are pure and fail closed: malformed input is reported as a
stable error rather than escaping as an exception.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any

from ..digests import file_sha256
from .m7_corpus import RISK_DIMENSIONS


_ROOT = Path(__file__).resolve().parents[3]
_CORPUS_PATH = "fixtures/m7/m7-corpus-manifest.v1.json"
_INDEPENDENCE_MODELS = frozenset(
    {"single_source", "independent_review", "independent_rederivation"}
)
_ADJUDICATION_STATES = frozenset(
    {"not_applicable", "pending", "agreed", "adjudicated"}
)
_SLICE_KINDS = frozenset({"execution", "data"})
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
_ENTRY_ID_RE = re.compile(r"^M7-TRUTH-[A-Z0-9-]+$")
_QUERY_ID_RE = re.compile(r"^M7-Q-[A-Z0-9-]+$")


@dataclass(frozen=True, slots=True)
class TruthRegistryAudit:
    """The deterministic result of auditing one registry document."""

    entry_count: int
    gaps: tuple[str, ...]
    errors: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class QueryListAudit:
    """The deterministic result of auditing one frozen query document."""

    query_count: int
    gaps: tuple[str, ...]
    errors: tuple[str, ...]


def _unique_sorted(values: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def _canonical_bytes(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def query_set_digest(queries: Any) -> str:
    """Return SHA-256 over the canonical serialization of query rows alone."""

    return hashlib.sha256(_canonical_bytes(queries)).hexdigest()


def _is_portable_relative_path(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = Path(value)
    return not path.is_absolute() and ".." not in path.parts


def _file_sha256(path: Path) -> str | None:
    try:
        return file_sha256(path)
    except OSError:
        return None


def _registry_entries(document: Any, errors: list[str]) -> list[Any]:
    if not isinstance(document, Mapping):
        errors.append("M7_TRUTH_DOCUMENT_SHAPE_INVALID: root must be an object")
        return []
    if document.get("schema_name") != "blueprint-lens-m7-ground-truth":
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: schema_name must be "
            "'blueprint-lens-m7-ground-truth'"
        )
    if document.get("schema_version") != "1.0.0":
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: schema_version must be '1.0.0'"
        )
    raw_entries = document.get("entries")
    if not isinstance(raw_entries, list):
        errors.append("M7_TRUTH_DOCUMENT_SHAPE_INVALID: entries must be an array")
        return []
    return list(raw_entries)


def _check_registry_entry_shape(
    entry: Mapping[str, Any], index: int, errors: list[str]
) -> str:
    entry_label = entry.get("entry_id", f"entry[{index}]")
    if not isinstance(entry_label, str) or not entry_label:
        entry_label = f"entry[{index}]"

    required = (
        "entry_id",
        "path",
        "sha256",
        "slice_kind",
        "producer",
        "produced_on",
        "adjudication_state",
        "counterpart_entry_id",
        "notes",
    )
    for field in required:
        if field not in entry:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{entry_label} missing {field}"
            )

    allowed = set(required) | {"independence_model", "agrees_with_counterpart"}
    for field in entry:
        if field not in allowed:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{entry_label} has unexpected field {field}"
            )

    entry_id = entry.get("entry_id")
    if not isinstance(entry_id, str) or not _ENTRY_ID_RE.fullmatch(entry_id):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} entry_id is invalid"
        )

    path = entry.get("path")
    if not _is_portable_relative_path(path):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} path must be portable and relative"
        )

    digest = entry.get("sha256")
    if not isinstance(digest, str) or _SHA256_RE.fullmatch(digest) is None:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} sha256 must be lowercase hexadecimal"
        )

    if entry.get("slice_kind") not in _SLICE_KINDS:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} slice_kind is invalid"
        )
    for field in ("producer", "notes"):
        if not isinstance(entry.get(field), str) or not entry[field].strip():
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{entry_label} {field} must be non-empty"
            )
    produced_on = entry.get("produced_on")
    if not isinstance(produced_on, str) or _DATE_RE.fullmatch(produced_on) is None:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} produced_on must be an ISO date"
        )

    state = entry.get("adjudication_state")
    if state not in _ADJUDICATION_STATES:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} adjudication_state is invalid"
        )

    counterpart = entry.get("counterpart_entry_id")
    if counterpart is not None and (
        not isinstance(counterpart, str) or not _ENTRY_ID_RE.fullmatch(counterpart)
    ):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} counterpart_entry_id is invalid"
        )
    agrees = entry.get("agrees_with_counterpart")
    if agrees is not None and not isinstance(agrees, bool):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{entry_label} agrees_with_counterpart must be boolean"
        )
    return entry_label


def _audit_truth_registry(document: Any) -> TruthRegistryAudit:
    errors: list[str] = []
    gaps: list[str] = []
    entries = _registry_entries(document, errors)
    entry_count = len(entries)
    by_id: dict[str, Mapping[str, Any]] = {}

    # Index the whole document before checking counterpart links.  A valid pair
    # may point forward to an entry that appears later in the JSON array.
    seen_ids: set[str] = set()
    for raw_entry in entries:
        if not isinstance(raw_entry, Mapping):
            continue
        entry_id = raw_entry.get("entry_id")
        if not isinstance(entry_id, str) or not entry_id:
            continue
        if entry_id in seen_ids:
            errors.append(
                "M7_TRUTH_DUPLICATE_ENTRY_ID: "
                f"{entry_id} occurs more than once"
            )
        else:
            seen_ids.add(entry_id)
            by_id[entry_id] = raw_entry

    for index, raw_entry in enumerate(entries):
        if not isinstance(raw_entry, Mapping):
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"entries[{index}] must be an object"
            )
            continue
        entry_label = _check_registry_entry_shape(raw_entry, index, errors)

        model = raw_entry.get("independence_model")
        if model not in _INDEPENDENCE_MODELS:
            errors.append(
                "M7_TRUTH_INDEPENDENCE_UNMARKED: "
                f"{entry_label} has independence_model={model!r}"
            )
        if model != "independent_rederivation":
            gaps.append(
                "M7_TRUTH_NOT_INDEPENDENTLY_REDERIVED: "
                f"{entry_label} independence_model={model!r}"
            )

        path_value = raw_entry.get("path")
        digest = raw_entry.get("sha256")
        if _is_portable_relative_path(path_value):
            source = _ROOT / str(path_value)
            actual = _file_sha256(source)
            if actual is None:
                errors.append(
                    "M7_TRUTH_ARTEFACT_MISSING: "
                    f"{entry_label} {path_value}"
                )
            elif isinstance(digest, str) and _SHA256_RE.fullmatch(digest):
                if actual != digest:
                    errors.append(
                        "M7_TRUTH_HASH_DRIFT: "
                        f"{entry_label} {path_value} recorded={digest} actual={actual}"
                    )

        state = raw_entry.get("adjudication_state")
        if model in {"single_source", "independent_review"}:
            if state != "not_applicable":
                errors.append(
                    "M7_TRUTH_ADJUDICATION_INCOHERENT: "
                    f"{entry_label} {model} requires not_applicable"
                )
            if raw_entry.get("counterpart_entry_id") is not None:
                errors.append(
                    "M7_TRUTH_COUNTERPART_INVALID: "
                    f"{entry_label} non-rederivation entry has a counterpart"
                )
        elif model == "independent_rederivation":
            if state not in {"pending", "agreed", "adjudicated"}:
                errors.append(
                    "M7_TRUTH_ADJUDICATION_INCOHERENT: "
                    f"{entry_label} rederivation has state={state!r}"
                )
            counterpart = raw_entry.get("counterpart_entry_id")
            if not isinstance(counterpart, str) or not counterpart:
                errors.append(
                    "M7_TRUTH_COUNTERPART_INVALID: "
                    f"{entry_label} rederivation has no counterpart"
                )
            elif counterpart not in by_id:
                errors.append(
                    "M7_TRUTH_COUNTERPART_INVALID: "
                    f"{entry_label} counterpart {counterpart} is not registered"
                )
            else:
                counterpart_entry = by_id[counterpart]
                if counterpart_entry.get("counterpart_entry_id") != entry_label:
                    errors.append(
                        "M7_TRUTH_COUNTERPART_INVALID: "
                        f"{entry_label} and {counterpart} are not mutually linked"
                    )
                if counterpart_entry.get("independence_model") != (
                    "independent_rederivation"
                ):
                    errors.append(
                        "M7_TRUTH_COUNTERPART_INVALID: "
                        f"{entry_label} counterpart {counterpart} is not a rederivation"
                    )
            if (
                raw_entry.get("agrees_with_counterpart") is False
                and state != "adjudicated"
            ):
                errors.append(
                    "M7_TRUTH_UNADJUDICATED_DISAGREEMENT: "
                    f"{entry_label} disagrees while state={state}"
                )

    return TruthRegistryAudit(
        entry_count=entry_count,
        gaps=_unique_sorted(gaps),
        errors=_unique_sorted(errors),
    )


def audit_truth_registry(document: Any) -> TruthRegistryAudit:
    """Audit a registry document without allowing malformed content to raise."""

    try:
        return _audit_truth_registry(document)
    except Exception as error:  # pragma: no cover - defensive fail-closed boundary
        return TruthRegistryAudit(
            entry_count=0,
            gaps=(),
            errors=(f"M7_TRUTH_DOCUMENT_SHAPE_INVALID: audit failed: {error}",),
        )


def _query_document_shape(document: Any, errors: list[str]) -> list[Any]:
    if not isinstance(document, Mapping):
        errors.append("M7_TRUTH_DOCUMENT_SHAPE_INVALID: root must be an object")
        return []
    if document.get("schema_name") != "blueprint-lens-m7-queries":
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: schema_name must be "
            "'blueprint-lens-m7-queries'"
        )
    if document.get("schema_version") != "1.0.0":
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: schema_version must be '1.0.0'"
        )
    frozen_on = document.get("frozen_on")
    if not isinstance(frozen_on, str) or _DATE_RE.fullmatch(frozen_on) is None:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: frozen_on must be an ISO date"
        )
    pin = document.get("corpus_manifest")
    if not isinstance(pin, Mapping):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus_manifest must be an object"
        )
    else:
        if set(pin) != {"path", "sha256"}:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus_manifest fields are invalid"
            )
        if not _is_portable_relative_path(pin.get("path")):
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus_manifest.path is invalid"
            )
        if not isinstance(pin.get("sha256"), str) or _SHA256_RE.fullmatch(
            pin.get("sha256", "")
        ) is None:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus_manifest.sha256 is invalid"
            )
    stored_digest = document.get("query_set_sha256")
    if not isinstance(stored_digest, str) or _SHA256_RE.fullmatch(stored_digest) is None:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: query_set_sha256 is invalid"
        )
    raw_queries = document.get("queries")
    if not isinstance(raw_queries, list):
        errors.append("M7_TRUTH_DOCUMENT_SHAPE_INVALID: queries must be an array")
        return []
    return list(raw_queries)


def _corpus_candidates(
    corpus: Any, errors: list[str]
) -> tuple[dict[str, Mapping[str, Any]], set[str]]:
    if not isinstance(corpus, Mapping):
        errors.append("M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus must be an object")
        return {}, set()
    raw_rows = corpus.get("candidate_graphs")
    if not isinstance(raw_rows, list):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: corpus candidate_graphs must be an array"
        )
        return {}, set()

    candidates: dict[str, Mapping[str, Any]] = {}
    dimensions: set[str] = set()
    for index, raw_row in enumerate(raw_rows):
        if not isinstance(raw_row, Mapping):
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"corpus candidate_graphs[{index}] must be an object"
            )
            continue
        candidate_id = raw_row.get("id")
        if not isinstance(candidate_id, str) or not candidate_id:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"corpus candidate_graphs[{index}] has no id"
            )
            continue
        if candidate_id in candidates:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"corpus repeats candidate id {candidate_id}"
            )
        else:
            candidates[candidate_id] = raw_row
        raw_dimensions = raw_row.get("risk_dimensions")
        if not isinstance(raw_dimensions, list):
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{candidate_id} risk_dimensions must be an array"
            )
            continue
        for declaration in raw_dimensions:
            if not isinstance(declaration, Mapping):
                errors.append(
                    "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                    f"{candidate_id} has a non-object risk dimension"
                )
                continue
            dimension = declaration.get("dimension")
            if isinstance(dimension, str) and dimension in RISK_DIMENSIONS:
                dimensions.add(dimension)
    return candidates, dimensions


def _query_shape(
    query: Mapping[str, Any], index: int, errors: list[str]
) -> str:
    label_value = query.get("query_id", f"query[{index}]")
    label = label_value if isinstance(label_value, str) and label_value else f"query[{index}]"
    required = {
        "query_id",
        "slice_kind",
        "candidate_id",
        "graph_id",
        "description",
        "risk_dimensions",
        "annotation_scope",
    }
    slice_kind = query.get("slice_kind")
    required |= {"criterion_node_id"} if slice_kind == "execution" else set()
    required |= {"member_guid", "member_name"} if slice_kind == "data" else set()
    if query.get("annotation_scope") == "bounded_region":
        required.add("region_note")
    for field in sorted(required):
        if field not in query:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: " f"{label} missing {field}"
            )
    allowed = {
        "query_id",
        "slice_kind",
        "candidate_id",
        "graph_id",
        "criterion_node_id",
        "member_guid",
        "member_name",
        "description",
        "risk_dimensions",
        "annotation_scope",
        "region_note",
    }
    for field in query:
        if field not in allowed:
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{label} has unexpected field {field}"
            )
    if not isinstance(query.get("query_id"), str) or not _QUERY_ID_RE.fullmatch(
        query.get("query_id", "")
    ):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: " f"{label} query_id is invalid"
        )
    if slice_kind not in _SLICE_KINDS:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: " f"{label} slice_kind is invalid"
        )
    for field in ("candidate_id", "graph_id", "description"):
        if not isinstance(query.get(field), str) or not query[field].strip():
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"{label} {field} must be non-empty"
            )
    dimensions = query.get("risk_dimensions")
    if not isinstance(dimensions, list) or not dimensions:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: " f"{label} risk_dimensions is invalid"
        )
    elif all(isinstance(item, str) for item in dimensions) and len(
        dimensions
    ) != len(set(dimensions)):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{label} risk_dimensions contains duplicates"
        )
    scope = query.get("annotation_scope")
    if scope not in {"whole_graph", "bounded_region"}:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{label} annotation_scope is invalid"
        )
    elif scope == "bounded_region" and (
        not isinstance(query.get("region_note"), str)
        or not query["region_note"].strip()
    ):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{label} bounded_region requires region_note"
        )
    elif scope == "whole_graph" and "region_note" in query:
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{label} whole_graph must not carry region_note"
        )
    if slice_kind == "execution" and (
        not isinstance(query.get("criterion_node_id"), str)
        or not query["criterion_node_id"].strip()
    ):
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"{label} criterion_node_id must be non-empty"
        )
    if slice_kind == "data":
        for field in ("member_guid", "member_name"):
            if not isinstance(query.get(field), str) or not query[field].strip():
                errors.append(
                    "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                    f"{label} {field} must be non-empty"
                )
    return label


def _provider_graphs(provider: Any, errors: list[str]) -> dict[str, Any]:
    try:
        graph_ids = tuple(provider.list_graph_ids())
    except Exception as error:  # pragma: no cover - defensive provider boundary
        errors.append(
            "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
            f"typed-IR provider could not be listed: {error}"
        )
        return {}
    graphs: dict[str, Any] = {}
    for graph_id in sorted(set(graph_ids)):
        try:
            graphs[graph_id] = provider.load_graph(graph_id)
        except Exception:
            continue
    return graphs


def _audit_query_list(
    document: Any, corpus: Any, provider: Any
) -> QueryListAudit:
    errors: list[str] = []
    gaps: list[str] = []
    raw_queries = _query_document_shape(document, errors)
    query_count = len(raw_queries)

    if isinstance(document, Mapping):
        pin = document.get("corpus_manifest")
        if isinstance(pin, Mapping):
            actual = _file_sha256(_ROOT / _CORPUS_PATH)
            if (
                pin.get("path") != _CORPUS_PATH
                or actual is None
                or pin.get("sha256") != actual
            ):
                errors.append(
                    "M7_TRUTH_CORPUS_PIN_DRIFT: "
                    f"expected {_CORPUS_PATH} at recorded digest {actual!r}"
                )
        stored_digest = document.get("query_set_sha256")
        if isinstance(stored_digest, str) and _SHA256_RE.fullmatch(stored_digest):
            try:
                recomputed = query_set_digest(raw_queries)
            except (TypeError, ValueError):
                recomputed = None
            if recomputed is not None and stored_digest != recomputed:
                errors.append(
                    "M7_TRUTH_QUERY_SET_DRIFT: "
                    f"recorded={stored_digest} recomputed={recomputed}"
                )

    candidates, corpus_dimensions = _corpus_candidates(corpus, errors)
    graphs = _provider_graphs(provider, errors)
    reached_dimensions: set[str] = set()
    seen_query_ids: set[str] = set()
    for index, raw_query in enumerate(raw_queries):
        if not isinstance(raw_query, Mapping):
            errors.append(
                "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                f"queries[{index}] must be an object"
            )
            continue
        label = _query_shape(raw_query, index, errors)
        query_id = raw_query.get("query_id")
        if isinstance(query_id, str):
            if query_id in seen_query_ids:
                errors.append(
                    "M7_TRUTH_DOCUMENT_SHAPE_INVALID: "
                    f"duplicate query_id {query_id}"
                )
            seen_query_ids.add(query_id)

        raw_dimensions = raw_query.get("risk_dimensions")
        dimensions = raw_dimensions if isinstance(raw_dimensions, list) else []
        for dimension in dimensions:
            if isinstance(dimension, str) and dimension in RISK_DIMENSIONS:
                reached_dimensions.add(dimension)

        candidate_id = raw_query.get("candidate_id")
        candidate = candidates.get(candidate_id) if isinstance(candidate_id, str) else None
        declared: set[str] = set()
        if candidate is not None:
            declarations = candidate.get("risk_dimensions")
            if isinstance(declarations, list):
                declared = {
                    item.get("dimension")
                    for item in declarations
                    if isinstance(item, Mapping)
                    and isinstance(item.get("dimension"), str)
                }
        for dimension in dimensions:
            if not isinstance(dimension, str) or dimension not in RISK_DIMENSIONS:
                errors.append(
                    "M7_TRUTH_QUERY_DIMENSION_INVALID: "
                    f"{label} names {dimension!r}"
                )
            elif candidate is None or dimension not in declared:
                errors.append(
                    "M7_TRUTH_QUERY_DIMENSION_INVALID: "
                    f"{label} {candidate_id!r} does not declare {dimension}"
                )

        graph_id = raw_query.get("graph_id")
        graph = None
        if (
            candidate is None
            or candidate.get("graph_id") != graph_id
            or not isinstance(graph_id, str)
        ):
            errors.append(
                "M7_TRUTH_QUERY_TARGET_MISSING: "
                f"{label} candidate/graph identity does not resolve"
            )
        else:
            graph = graphs.get(graph_id)
            if graph is None:
                errors.append(
                    "M7_TRUTH_QUERY_TARGET_MISSING: "
                    f"{label} graph {graph_id!r} is absent from typed IR"
                )

        if graph is not None:
            slice_kind = raw_query.get("slice_kind")
            if slice_kind == "execution":
                node_ids = {node.id for node in graph.nodes}
                criterion = raw_query.get("criterion_node_id")
                if not isinstance(criterion, str) or criterion not in node_ids:
                    errors.append(
                        "M7_TRUTH_QUERY_TARGET_MISSING: "
                        f"{label} execution criterion {criterion!r} is absent"
                    )
            elif slice_kind == "data":
                member_guid = raw_query.get("member_guid")
                member_name = raw_query.get("member_name")
                found = any(
                    isinstance(node.symbol, Mapping)
                    and node.symbol.get("guid") == member_guid
                    and node.symbol.get("name") == member_name
                    for node in graph.nodes
                )
                if not found:
                    errors.append(
                        "M7_TRUTH_QUERY_TARGET_MISSING: "
                        f"{label} data member {member_guid!r}/{member_name!r} is absent"
                    )

    for dimension in sorted(corpus_dimensions - reached_dimensions):
        gaps.append(
            "M7_TRUTH_DIMENSION_NO_QUERY: "
            f"{dimension} has no frozen query"
        )
    return QueryListAudit(
        query_count=query_count,
        gaps=_unique_sorted(gaps),
        errors=_unique_sorted(errors),
    )


def audit_query_list(
    document: Any, corpus: Any, provider: Any
) -> QueryListAudit:
    """Audit frozen query rows against the pinned corpus and typed IR."""

    try:
        return _audit_query_list(document, corpus, provider)
    except Exception as error:  # pragma: no cover - defensive fail-closed boundary
        return QueryListAudit(
            query_count=0,
            gaps=(),
            errors=(f"M7_TRUTH_DOCUMENT_SHAPE_INVALID: audit failed: {error}",),
        )


__all__ = ["audit_query_list", "audit_truth_registry", "query_set_digest"]
