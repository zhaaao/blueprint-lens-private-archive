"""M7 Task 9 measurement, layout-oracle and telemetry evidence helpers.

This module keeps the three measurements deliberately separate.  Python timing
uses the accepted production and slicing kernels; layout numbers are read from
the retained R1 geometry oracles; UE timing is represented by a sealed capture
record.  None of these paths turns target geometry into renderer output
or a drawing metric into a comprehension measure.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
import hashlib
import json
import math
import platform
from pathlib import Path
import statistics
import tempfile
import time
from typing import Any

from ..data_slice import compute_member_variable_data_slice
from ..execution_slice import compute_execution_slice
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import validate_instance
from .m7_export import _typed_name
from .pipeline import PipelineItem, build_batch
from .project_documents import ProductionManifestProvider


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-m7-measurement-v1.schema.json"
_MEASURED_ON = "2026-08-23"
_DEFAULT_REPEATS = 3

_M7_EXPORT_ROOT = _ROOT / "artifacts" / "m7" / "export" / "run1"
_M7_BATCH_RESULT = _M7_EXPORT_ROOT / "batch-result.v1.json"
_M7_TYPED_IR_ROOT = _M7_EXPORT_ROOT / "typed-ir"
_M7_QUERIES = _ROOT / "fixtures" / "m7" / "m7-queries.v1.json"
_M7_SUPPLEMENT = _ROOT / "fixtures" / "m7" / "m7-queries-supplement.v1.json"

# These are explicit paths, rather than a glob, because the report must be
# checkable after a new R1 artefact is added.  They are all retained geometry
# oracles and intentionally none is under artifacts/m7/.
_ORACLE_SOURCES: tuple[str, ...] = (
    "artifacts/r1/lc4-async-visual-candidates/lc4-async-geometry-oracle.v1.json",
    "artifacts/r1/lc4-async-visual-candidates/lc4-async-partial-order-join-selected-geometry-oracle.v1.json",
    "artifacts/r1/lc4-async-visual-candidates/lc4-async-spatial-redraw-geometry-oracle.v1.json",
    "artifacts/r1/lc4-sequence-visual-candidates/lc4-sequence-disclosure-rail-geometry-oracle.v1.json",
    "artifacts/r1/lc5-visual-candidates/lc5-geometry-oracle.v1.json",
    "artifacts/r1/lc5-visual-candidates/lc5-typed-portal-bridge-selected-geometry-oracle.v1.json",
    "artifacts/r1/lc6-four-track-selected/lc6-four-track-selected-oracle.json",
    "artifacts/r1/lc6-graph-first-candidates/lc6-graph-first-oracle.json",
    "artifacts/r1/lc6-visual-candidates/lc6-geometry-oracle.json",
    "artifacts/r1/lc7-a3-selected/targets/lc7-a3-selected-oracle.json",
    "artifacts/r1/lc7-static-scc-adaptive-layout-v1/lc7-adaptive-layout-oracle.json",
    "artifacts/r1/lc7-static-scc-visual-candidates-v6/lc7-visual-oracle.json",
)

_REQUIRED_LIMITATIONS: Mapping[str, str] = {
    "LAYOUT_NOT_MEASURED_ON_M7_CORPUS": (
        "No retained product lays out any M7 corpus graph. These layout metrics "
        "cover the frozen R1 LC fixtures only; the corpus used by the M7 "
        "correctness measurement is not the corpus these metrics describe."
    ),
    "LAYOUT_IS_TARGET_GEOMETRY": (
        "The retained oracles are computed in Python from intended layout "
        "geometry. They are not renderer output, and their metrics do not "
        "establish what Slate painted."
    ),
    "METRICS_ARE_NOT_COMPREHENSION": (
        "Overlap, crossing, truncation, density and bend counts describe a "
        "drawing. They say nothing about whether a reader understood it, and "
        "no human was asked."
    ),
    "TIMING_IS_ONE_MACHINE": (
        "Every timing figure comes from one machine and one configuration, "
        "recorded beside the figure. It is not a portability or scalability "
        "claim."
    ),
    "RENDER_TIMING_SCOPE": (
        "The sealed UE render timing covers the M7 corpus execution query on "
        "/Game/M7Corpus/BP_M7_EngineSample.BP_M7_EngineSample:EventGraph only. "
        "It does not cover any other M7 graph or the M7 corpus as a whole; the "
        "retained record measures this graph's M6 export, explanation, baseline "
        "projection, layout, render and packet stages."
        "The capture was driven interactively through the Editor rather than by "
        "the automation framework, so its log carries no Automation RunTests, no "
        "Test Completed and no Received StopTestSession line, and there are no "
        "discovered/succeeded/failed counts to reconcile the process exit against. "
        "It is a real capture with a weaker evidence trail than an automation run."
    ),
}


def _unique_sorted(values: Iterable[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def _schema_errors(document: Any) -> tuple[str, ...]:
    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(document, schema)
    except Exception as error:  # pragma: no cover - fail-closed boundary
        return (f"M7_MEASUREMENT_DOCUMENT_SHAPE_INVALID: {error}",)
    return ()


def _portable(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(_ROOT).as_posix()
    except ValueError:
        return resolved.as_posix()


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def measurement_environment(*, repeats: int = _DEFAULT_REPEATS) -> dict[str, Any]:
    """Return the machine/configuration block attached to every timing figure."""

    if repeats < 2:
        raise ValueError("M7_MEASUREMENT_DISTRIBUTION_INVALID: repeats must be at least 2")
    return {
        "platform": platform.platform(),
        "processor": platform.processor() or platform.machine(),
        "python_version": platform.python_version(),
        "repeats": repeats,
        "repeat_reason": (
            "Three complete-batch repeats expose minimum/median/maximum spread "
            "without pretending that one duration is representative."
        ),
    }


def _distribution(samples_ms: Sequence[float], repeats: int | None = None) -> dict[str, Any]:
    values = [float(value) for value in samples_ms]
    repeat_count = len(values) if repeats is None else repeats
    if repeat_count < 2 or len(values) != repeat_count:
        raise ValueError("M7_MEASUREMENT_DISTRIBUTION_INVALID: at least two samples are required")
    if not values or any(not math.isfinite(value) or value < 0.0 for value in values):
        raise ValueError("M7_MEASUREMENT_DISTRIBUTION_INVALID: durations must be finite and non-negative")
    ordered = sorted(values)
    return {
        "minimum_ms": round(ordered[0], 6),
        "median_ms": round(float(statistics.median(ordered)), 6),
        "maximum_ms": round(ordered[-1], 6),
        "repeats": repeat_count,
    }


def _time_repeated(callback: Any, repeats: int) -> dict[str, Any]:
    samples: list[float] = []
    for _ in range(repeats):
        started = time.perf_counter_ns()
        callback()
        samples.append((time.perf_counter_ns() - started) / 1_000_000.0)
    return _distribution(samples)


def _load_queries() -> list[dict[str, Any]]:
    queries: list[dict[str, Any]] = []
    seen: set[str] = set()
    for path in (_M7_QUERIES, _M7_SUPPLEMENT):
        value = json.loads(path.read_text(encoding="utf-8"))
        rows = value.get("queries")
        if not isinstance(rows, list):
            raise ValueError(f"M7_MEASUREMENT_QUERY_SHAPE_INVALID: {path}")
        for row in rows:
            if not isinstance(row, dict) or not isinstance(row.get("query_id"), str):
                raise ValueError(f"M7_MEASUREMENT_QUERY_SHAPE_INVALID: {path}")
            if row["query_id"] in seen:
                raise ValueError(f"M7_MEASUREMENT_QUERY_DUPLICATE: {row['query_id']}")
            seen.add(row["query_id"])
            queries.append(row)
    return sorted(queries, key=lambda row: row["query_id"])


def _typed_documents() -> dict[str, BlueprintDocument]:
    documents: dict[str, BlueprintDocument] = {}
    for path in sorted(_M7_TYPED_IR_ROOT.glob("*.blueprint-lens-v1.json")):
        document = load_blueprint_lens_v1(path)
        for graph in document.graphs:
            documents[graph.id] = document
    if not documents:
        raise ValueError("M7_MEASUREMENT_TYPED_IR_MISSING: no typed-IR documents")
    return documents


def _build_items(output_dir: Path) -> tuple[PipelineItem, ...]:
    provider = ProductionManifestProvider(_M7_BATCH_RESULT)
    items: list[PipelineItem] = []
    for asset_id in provider.list_asset_ids():
        fixture = provider.load(asset_id)
        items.append(
            PipelineItem(
                asset_id=asset_id,
                blueprint_object_path=asset_id,
                raw_path=fixture.raw_path,
                typed_ir_path=output_dir / _typed_name(asset_id),
            )
        )
    return tuple(items)


def _slice_batch(documents: Mapping[str, BlueprintDocument], queries: Sequence[Mapping[str, Any]]) -> None:
    for query in queries:
        graph_id = query["graph_id"]
        document = documents[graph_id]
        if query["slice_kind"] == "execution":
            compute_execution_slice(document, query["criterion_node_id"])
        else:
            compute_member_variable_data_slice(document, graph_id, query["member_guid"])


def measure_python_timings(*, repeats: int = _DEFAULT_REPEATS) -> tuple[dict[str, Any], ...]:
    """Measure the accepted kernels over the complete frozen M7 query set."""

    environment = measurement_environment(repeats=repeats)
    queries = _load_queries()
    documents = _typed_documents()
    batch_asset_count = len(ProductionManifestProvider(_M7_BATCH_RESULT).list_asset_ids())
    with tempfile.TemporaryDirectory(prefix="m7-measurement-") as temporary:
        output_dir = Path(temporary)
        typed_ir_distribution = _time_repeated(
            lambda: build_batch(_build_items(output_dir)), repeats
        )
    execution_queries = [row for row in queries if row["slice_kind"] == "execution"]
    data_queries = [row for row in queries if row["slice_kind"] == "data"]
    execution_distribution = _time_repeated(
        lambda: _slice_batch(documents, execution_queries), repeats
    )
    data_distribution = _time_repeated(
        lambda: _slice_batch(documents, data_queries), repeats
    )
    common = {
        "environment": environment,
        "source": {
            "typed_ir_build": [
                "analysis/blueprint_lens/production/pipeline.py",
                "analysis/blueprint_lens/production/m7_export.py",
            ],
            "execution_slice": ["analysis/blueprint_lens/execution_slice.py"],
            "data_slice": ["analysis/blueprint_lens/data_slice.py"],
        },
    }
    return (
        {
            "stage": "typed_ir_build",
            "description": "raw-to-typed transform through build_batch over the captured M7 assets",
            "measured_over": f"{batch_asset_count} assets in artifacts/m7/export/run1/batch-result.v1.json",
            "distribution": typed_ir_distribution,
            "environment": dict(environment),
            "source": common["source"]["typed_ir_build"],
        },
        {
            "stage": "execution_slice",
            "description": "compute_execution_slice over all frozen and rule-selected execution queries",
            "measured_over": f"{len(execution_queries)} execution queries",
            "distribution": execution_distribution,
            "environment": dict(environment),
            "source": common["source"]["execution_slice"],
        },
        {
            "stage": "data_slice",
            "description": "compute_member_variable_data_slice over all frozen and rule-selected data queries",
            "measured_over": f"{len(data_queries)} data queries",
            "distribution": data_distribution,
            "environment": dict(environment),
            "source": common["source"]["data_slice"],
        },
    )


def _observation_rows(source: str, value: Mapping[str, Any]) -> list[tuple[str, Mapping[str, Any]]]:
    rows: list[tuple[str, Mapping[str, Any]]] = []
    states = value.get("states")
    if isinstance(states, list):
        for index, state in enumerate(states):
            if isinstance(state, Mapping):
                rows.append((f"{source}#states[{index}]", state))
    checks = value.get("checks")
    if isinstance(checks, Mapping):
        if any(key in checks for key in ("pass", "canvas", "route_count", "text_overlap_pairs")):
            rows.append((f"{source}#checks", value))
        else:
            for key, item in checks.items():
                if isinstance(item, Mapping):
                    rows.append((f"{source}#checks.{key}", item))
    if not rows and isinstance(value, Mapping):
        rows.append((source, value))
    return rows


def _read_oracle_observations() -> list[tuple[str, Mapping[str, Any]]]:
    observations: list[tuple[str, Mapping[str, Any]]] = []
    for relative in _ORACLE_SOURCES:
        path = _ROOT / relative
        value = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(value, Mapping):
            raise ValueError(f"M7_MEASUREMENT_ORACLE_INVALID: {relative}")
        observations.extend(_observation_rows(relative, value))
    return observations


def _check_value(row: Mapping[str, Any], key: str) -> Any:
    checks = row.get("checks")
    if isinstance(checks, Mapping) and key in checks:
        return checks[key]
    return row.get(key)


def _array_count(value: Any) -> int | None:
    if isinstance(value, list):
        return len(value)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return None


def _metric_value(metric: str, observations: Sequence[tuple[str, Mapping[str, Any]]]) -> tuple[dict[str, Any], list[str]]:
    sources: set[str] = set()
    overlap_values: list[int] = []
    density_values: list[float] = []
    bend_values: list[int] = []
    route_values: list[int] = []
    disclosure_states: set[str] = set()
    fallback_validation_errors = 0
    for label, row in observations:
        checks = row.get("checks") if isinstance(row.get("checks"), Mapping) else row
        if not isinstance(checks, Mapping):
            continue
        if metric == "overlap":
            count: int | None = None
            for key in ("major_region_overlap_count", "text_text_overlap_count", "route_text_collision_count"):
                candidate = _check_value(row, key)
                if isinstance(candidate, int) and not isinstance(candidate, bool):
                    count = (count or 0) + candidate
            if count is None:
                counts = [
                    _array_count(_check_value(row, key))
                    for key in ("text_overlap_pairs", "text_text_overlaps", "route_text_collision_pairs", "text_node_overlap_ids")
                ]
                present = [value for value in counts if value is not None]
                count = sum(present) if present else None
            if count is not None:
                overlap_values.append(count)
                sources.add(label.split("#", 1)[0])
        elif metric == "density":
            canvas = row.get("canvas")
            if isinstance(canvas, list) and len(canvas) == 2 and all(isinstance(v, (int, float)) for v in canvas):
                route_count = _check_value(row, "relation_route_count")
                if route_count is None:
                    route_count = _check_value(row, "route_count")
                if isinstance(route_count, int) and not isinstance(route_count, bool):
                    area = float(canvas[0]) * float(canvas[1])
                    if area > 0:
                        density_values.append(route_count / area)
                        route_values.append(route_count)
                        sources.add(label.split("#", 1)[0])
        elif metric == "bend":
            geometry = row.get("geometry")
            if isinstance(geometry, Mapping):
                bend_counts = geometry.get("bend_counts")
                if isinstance(bend_counts, Mapping):
                    values = [value for value in bend_counts.values() if isinstance(value, int) and not isinstance(value, bool)]
                    if values:
                        bend_values.extend(values)
                        sources.add(label.split("#", 1)[0])
        elif metric == "disclosure_activation":
            # Only LC6 oracle check identities carry the disclosure state. A/B
            # variants, responsive condition IDs and LC5 condition IDs are not
            # disclosure activations.
            for marker in ("NEUTRAL", "CORE_SELECTED", "QUERY_SELECTED"):
                if f"__{marker}__" in label:
                    disclosure_states.add(marker)
                    sources.add(label.split("#", 1)[0])
        elif metric == "fallback_activation":
            errors = _check_value(row, "fallback_errors")
            count = _array_count(errors)
            if count is not None:
                fallback_validation_errors += count
                sources.add(label.split("#", 1)[0])
    if metric == "overlap":
        return {
            "status": "recorded",
            "definition": "per-oracle overlap findings, summing explicit overlap fields once per observation",
            "observations": len(overlap_values),
            "total": sum(overlap_values),
            "minimum": min(overlap_values) if overlap_values else None,
            "median": statistics.median(overlap_values) if overlap_values else None,
            "maximum": max(overlap_values) if overlap_values else None,
        }, sorted(sources)
    if metric == "density":
        return {
            "status": "recorded",
            "definition": "relation routes per logical canvas pixel squared where both fields are present",
            "observations": len(density_values),
            "minimum": min(density_values) if density_values else None,
            "median": statistics.median(density_values) if density_values else None,
            "maximum": max(density_values) if density_values else None,
            "route_count_total": sum(route_values),
        }, sorted(sources)
    if metric == "bend":
        return {
            "status": "recorded",
            "definition": "individual route bend counts from geometry.bend_counts",
            "observations": len(bend_values),
            "total_bends": sum(bend_values),
            "minimum": min(bend_values) if bend_values else None,
            "median": statistics.median(bend_values) if bend_values else None,
            "maximum": max(bend_values) if bend_values else None,
        }, sorted(sources)
    if metric == "disclosure_activation":
        return {
            "status": "recorded",
            "definition": "distinct non-neutral disclosure state markers carried by the oracles",
            "activation_count": len(disclosure_states - {"NEUTRAL"}),
            "states": sorted(disclosure_states),
        }, sorted(sources)
    if metric == "fallback_activation":
        # The retained oracles expose fallback validation errors, not activation
        # events.  Keep that distinction explicit rather than converting zero
        # validation errors into a false claim that fallback was never activated.
        return {
            "status": "not_recorded",
            "activation_count": None,
            "validation_error_count": fallback_validation_errors,
            "reason": "the retained geometry oracles do not encode fallback activation events",
        }, sorted(sources) or list(_ORACLE_SOURCES)
    if metric in {"crossing", "truncation"}:
        return {
            "status": "not_recorded",
            "count": None,
            "reason": f"the retained geometry oracles do not encode {metric} counts",
        }, list(_ORACLE_SOURCES)
    raise ValueError(f"unknown layout metric: {metric}")


def build_layout_metrics() -> tuple[dict[str, Any], ...]:
    """Compute layout disclosures from explicit frozen R1 geometry oracles."""

    observations = _read_oracle_observations()
    metrics: list[dict[str, Any]] = []
    for metric in (
        "overlap",
        "crossing",
        "truncation",
        "density",
        "bend",
        "disclosure_activation",
        "fallback_activation",
    ):
        value, sources = _metric_value(metric, observations)
        metrics.append(
            {
                "metric": metric,
                "interpretation": "target geometry only; establishes nothing about comprehension",
                "sources": sources,
                "value": value,
            }
        )
    return tuple(metrics)


def _telemetry_row(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    events = [json.loads(line) for line in text.splitlines() if line]
    record_type = next(
        (
            event["record_type"]
            for event in events
            if isinstance(event.get("record_type"), str)
        ),
        "BlueprintLens.M6.MeasuredTelemetry",
    )
    evidence_role = next(
        (
            event["evidence_role"]
            for event in events
            if isinstance(event.get("evidence_role"), str)
        ),
        None,
    )
    scope = next(
        (
            event["scope"]
            for event in events
            if isinstance(event.get("scope"), str)
        ),
        None,
    )
    stages = [
        event.get("payload", {}).get("stage")
        for event in events
        if event.get("event_type") == "stage_result"
    ]
    durations = [
        event.get("duration_ms")
        for event in events
        if "duration_ms" in event
    ]
    sealed = bool(events and events[-1].get("event_type") == "record_sealed")
    stage_durations = {
        event.get("payload", {}).get("stage"): event.get("duration_ms")
        for event in events
        if event.get("event_type") == "stage_result"
        and isinstance(event.get("payload", {}).get("stage"), str)
        and isinstance(event.get("duration_ms"), (int, float))
    }
    row = {
        "path": _portable(path),
        "sha256": _sha256(path),
        "record_type": record_type,
        "sealed": sealed,
        "stages": stages,
        "event_types": sorted({event.get("event_type") for event in events if isinstance(event.get("event_type"), str)}),
        "stage_durations_ms": stage_durations,
        "duration_count": len(durations),
        "duration_ms_present": bool(durations),
        "event_count": len(events),
    }
    if evidence_role is not None:
        row["evidence_role"] = evidence_role
    if scope is not None:
        row["scope"] = scope
    return row


def build_measurement_report(
    *,
    telemetry_paths: Sequence[str | Path],
    repeats: int = _DEFAULT_REPEATS,
    measured_on: str = _MEASURED_ON,
) -> dict[str, Any]:
    """Build a canonical report from fresh Python timings and sealed telemetry."""

    environment = measurement_environment(repeats=repeats)
    telemetry = [_telemetry_row(Path(path)) for path in telemetry_paths]
    return {
        "schema_name": "blueprint-lens-m7-measurement",
        "schema_version": "1.0.0",
        "measured_on": measured_on,
        "environment": environment,
        "timings": list(measure_python_timings(repeats=repeats)),
        "layout_metrics": list(build_layout_metrics()),
        "telemetry": telemetry,
        "limitations": [
            {"id": limitation_id, "statement": statement}
            for limitation_id, statement in _REQUIRED_LIMITATIONS.items()
        ],
        "gaps": [
            {
                "id": "M7_MEASUREMENT_LAYOUT_FIELDS_ABSENT",
                "statement": (
                    "Crossing, truncation and fallback activation are not encoded "
                    "by the retained oracles; their values remain not_recorded."
                ),
            },
            {
                "id": "M7_MEASUREMENT_UE_RECORD_SCOPE",
                "statement": (
                    "The retained UE record is a sealed M7 corpus execution capture "
                    "for /Game/M7Corpus/BP_M7_EngineSample.BP_M7_EngineSample:EventGraph. "
                    "It covers one graph only; it is not a corpus-wide render "
                    "distribution."
                ),
            },
            {
                "id": "M7_MEASUREMENT_RENDER_GRAPH_SCOPE",
                "statement": (
                    "The real UE capture covers the M7 corpus execution query on "
                    "/Game/M7Corpus/BP_M7_EngineSample.BP_M7_EngineSample:EventGraph "
                    "only; no timing was retained for the other M7 graphs."
                ),
            },
        ],
    }


def measurement_errors(document: Any) -> tuple[str, ...]:
    """Return stable errors for malformed, under-specified or drifting reports."""

    # These are semantic contract failures with stable public codes.  Check them
    # before the stricter JSON-schema boundary so a deliberately incomplete
    # environment/distribution/source is diagnosed as the thing it is, rather
    # than being collapsed into a generic document-shape error.
    if isinstance(document, Mapping):
        semantic_errors: list[str] = []
        environment = document.get("environment")
        if isinstance(environment, Mapping) and any(
            not isinstance(environment.get(field), str) or not environment[field].strip()
            for field in ("platform", "processor", "python_version")
        ):
            semantic_errors.append(
                "M7_MEASUREMENT_ENVIRONMENT_MISSING: environment lacks platform, processor or python_version"
            )
        timings = document.get("timings")
        if isinstance(timings, list):
            for index, timing in enumerate(timings):
                if not isinstance(timing, Mapping):
                    continue
                distribution = timing.get("distribution")
                if isinstance(distribution, Mapping):
                    required = {"minimum_ms", "median_ms", "maximum_ms", "repeats"}
                    if not required.issubset(distribution) or not isinstance(distribution.get("repeats"), int) or distribution.get("repeats", 0) < 2:
                        semantic_errors.append(
                            f"M7_MEASUREMENT_DISTRIBUTION_INVALID: timings[{index}]"
                        )
        layout_metrics = document.get("layout_metrics")
        if isinstance(layout_metrics, list):
            for index, metric in enumerate(layout_metrics):
                if isinstance(metric, Mapping) and metric.get("sources") == []:
                    semantic_errors.append(
                        f"M7_MEASUREMENT_SOURCE_MISSING: layout_metrics[{index}]"
                    )
        limitations = document.get("limitations")
        if isinstance(limitations, list):
            limitation_ids = {
                row.get("id")
                for row in limitations
                if isinstance(row, Mapping)
            }
            if not set(_REQUIRED_LIMITATIONS).issubset(limitation_ids):
                semantic_errors.append(
                    "M7_MEASUREMENT_LIMITATIONS_MISSING: required limitation rows are absent or softened"
                )
        if semantic_errors:
            return _unique_sorted(semantic_errors)

    shape_errors = _schema_errors(document)
    if shape_errors:
        return shape_errors
    errors: list[str] = []
    try:
        environment = document["environment"]
        for field in ("platform", "processor", "python_version"):
            if not isinstance(environment.get(field), str) or not environment[field].strip():
                errors.append(f"M7_MEASUREMENT_ENVIRONMENT_MISSING: {field}")
        for index, timing in enumerate(document["timings"]):
            distribution = timing["distribution"]
            required = {"minimum_ms", "median_ms", "maximum_ms", "repeats"}
            if not required.issubset(distribution) or distribution["repeats"] < 2:
                errors.append(f"M7_MEASUREMENT_DISTRIBUTION_INVALID: timings[{index}]")
                continue
            values = [distribution[field] for field in ("minimum_ms", "median_ms", "maximum_ms")]
            if any(not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 for value in values):
                errors.append(f"M7_MEASUREMENT_DISTRIBUTION_INVALID: timings[{index}]")
            elif not values[0] <= values[1] <= values[2]:
                errors.append(f"M7_MEASUREMENT_DISTRIBUTION_INVALID: timings[{index}]")
            row_environment = timing.get("environment")
            if not isinstance(row_environment, Mapping) or any(
                not isinstance(row_environment.get(field), str) or not row_environment[field].strip()
                for field in ("platform", "processor", "python_version")
            ):
                errors.append(f"M7_MEASUREMENT_ENVIRONMENT_MISSING: timings[{index}]")
        for index, metric in enumerate(document["layout_metrics"]):
            sources = metric.get("sources")
            if not isinstance(sources, list) or not sources:
                errors.append(f"M7_MEASUREMENT_SOURCE_MISSING: layout_metrics[{index}]")
            else:
                for source in sources:
                    if not isinstance(source, str) or not source or "artifacts/m7/" in source or not (_ROOT / source).is_file():
                        errors.append(f"M7_MEASUREMENT_SOURCE_MISSING: layout_metrics[{index}]")
                        break
        for index, row in enumerate(document["telemetry"]):
            path = _ROOT / row["path"]
            events: list[dict[str, Any]] = []
            if path.is_file():
                try:
                    events = [
                        json.loads(line)
                        for line in path.read_text(encoding="utf-8").splitlines()
                        if line
                    ]
                except (OSError, json.JSONDecodeError):
                    events = []
            if (
                not path.is_file()
                or _sha256(path) != row["sha256"]
                or row.get("sealed") is not True
                or row.get("duration_ms_present") is not True
                or not isinstance(row.get("duration_count"), int)
                or row["duration_count"] < 1
            ):
                errors.append(f"M7_MEASUREMENT_TELEMETRY_DRIFT: telemetry[{index}]")
            durations = [
                event["duration_ms"]
                for event in events
                if isinstance(event, Mapping)
                and isinstance(event.get("duration_ms"), (int, float))
            ]
            run_ids = {
                event.get("run_id")
                for event in events
                if isinstance(event, Mapping)
            }
            placeholder_digests = any(
                isinstance(event.get("semantic_sha256"), str)
                and len(set(event["semantic_sha256"])) == 1
                for event in events
                if isinstance(event, Mapping)
            )
            if durations and (
                len(set(durations)) <= 1
                or "measured-run" in run_ids
                or placeholder_digests
            ):
                errors.append(f"M7_MEASUREMENT_TELEMETRY_FIXTURE: telemetry[{index}]")
        limitation_ids = [row["id"] for row in document["limitations"]]
        if (
            len(limitation_ids) != len(set(limitation_ids))
            or not set(_REQUIRED_LIMITATIONS).issubset(limitation_ids)
            or any(
                row["statement"] != _REQUIRED_LIMITATIONS[row["id"]]
                for row in document["limitations"]
                if row["id"] in _REQUIRED_LIMITATIONS
            )
        ):
            errors.append("M7_MEASUREMENT_LIMITATIONS_MISSING: required limitation rows are absent or softened")
    except Exception as error:  # pragma: no cover - schema is the normal guard
        errors.append(f"M7_MEASUREMENT_DOCUMENT_SHAPE_INVALID: {error}")
    return _unique_sorted(errors)


def verify_measurement_report(document: Any) -> tuple[str, ...]:
    """Verify the report's internal shape and retained-file bindings."""

    return measurement_errors(document)


__all__ = [
    "build_layout_metrics",
    "build_measurement_report",
    "measure_python_timings",
    "measurement_environment",
    "measurement_errors",
    "verify_measurement_report",
]
