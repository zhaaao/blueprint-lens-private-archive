"""Build and fail-closed validate the bounded LC4 asynchronous truth profile."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Callable, Mapping, Sequence

from .schema_validation import validate_json_file


PROFILE_ID = "LC4_ASYNC_TWO_DELAY_BARRIER_V1"
RULES_VERSION = "async_two_delay_barrier_v1"
PROFILE_FORMAT = "blueprint-lens-async-profile"
PROFILE_VERSION = "1.0.0"

SOURCE_BINDING_INVALID = "LC4_ASYNC_SOURCE_BINDING_INVALID"
COMPILER_LINKAGE_INVALID = "LC4_ASYNC_COMPILER_LINKAGE_INVALID"
TRACE_IDENTITY_INVALID = "LC4_ASYNC_TRACE_IDENTITY_INVALID"
TRACE_ORDER_INVALID = "LC4_ASYNC_TRACE_ORDER_INVALID"
TRACE_SCHEDULE_INVALID = "LC4_ASYNC_TRACE_SCHEDULE_INVALID"
TRACE_COMPLETENESS_INVALID = "LC4_ASYNC_TRACE_COMPLETENESS_INVALID"
BARRIER_INVALID = "LC4_ASYNC_BARRIER_INVALID"
CAUSAL_RELATION_INVALID = "LC4_ASYNC_CAUSAL_RELATION_INVALID"
INCOMPARABILITY_INVALID = "LC4_ASYNC_INCOMPARABILITY_INVALID"
COUNT_MISMATCH = "LC4_ASYNC_COUNT_MISMATCH"

_VARIANTS = ("A_FIRST", "B_FIRST")
_RUNS = ("run1", "run2")
_PARTICIPANTS = ("A", "B")
_EXPECTED_COMPLETION = {
    "A_FIRST": ("A", "B"),
    "B_FIRST": ("B", "A"),
}
_EXPECTED_EVENT_COUNTS = Counter(
    {
        "trace_boundary": 2,
        "invocation_started": 1,
        "launch": 2,
        "completion": 2,
        "barrier_arrival": 2,
        "barrier_release": 1,
        "criterion": 1,
    }
)
_RELATION_COUNTS = Counter(
    {
        "launch_order": 1,
        "continuation_of": 2,
        "local_resume_order": 2,
        "participant_of": 2,
        "barrier_waits_for": 2,
        "barrier_release": 1,
        "criterion_after_release": 1,
    }
)


class LC4AsyncError(ValueError):
    """A stable, fail-closed LC4-ASYNC diagnostic."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


class LC4AsyncArtifactError(ValueError):
    """Raised when the async artifact publication Gate cannot close."""


def _fail(code: str, message: str) -> None:
    raise LC4AsyncError(code, message)


def _object(value: Any, code: str, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        _fail(code, f"{context} must be an object")
    return value


def _array(value: Any, code: str, context: str) -> list[Any]:
    if not isinstance(value, list):
        _fail(code, f"{context} must be an array")
    return value


def _non_empty(value: Any, code: str, context: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(code, f"{context} must be a non-empty string")
    return value


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    return _sha256_bytes(path.read_bytes())


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise LC4AsyncArtifactError(f"expected JSON object: {path}")
    return value


def _canonical_json(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(_canonical_json(value))


@dataclass(frozen=True, slots=True)
class CompilerContinuation:
    node_id: str
    source_guid: str
    latent_uuid: int
    resume_code_offsets: tuple[int, ...]
    source_match: bool
    debug_match: bool


@dataclass(frozen=True, slots=True)
class CompilerAudit:
    blueprint_asset_path: str
    source_asset_sha256: str
    source_compile_sha256: str
    compile_hash_basis: str
    continuations: tuple[CompilerContinuation, ...]
    schedule_variant: str
    overlay_sha256: str
    active_state_sha256: str


@dataclass(frozen=True, slots=True)
class AsyncProducts:
    evidence_dir: Path
    asset_path: Path
    asset_sha256: str
    sources: Mapping[str, Mapping[str, Any]]
    compiler_audits: Mapping[str, CompilerAudit]
    compiler_audit_texts: Mapping[str, str]
    traces: tuple[Mapping[str, Any], ...]
    trace_product_keys: tuple[str, ...]
    reviewed_ground_truth: Mapping[str, Any]
    files: Mapping[str, Mapping[str, str]]


def parse_async_compiler_audit(text: str) -> CompilerAudit:
    """Parse the independent compiler linkage record without inferring runtime order."""

    scalar: dict[str, str] = {}
    continuations: list[CompilerContinuation] = []
    count_row: tuple[int, int] | None = None
    for line_number, line in enumerate(text.splitlines(), start=1):
        fields = line.split("\t")
        record = fields[0] if fields else ""
        try:
            if record == "FORMAT" and fields[1:] == [
                "blueprint-lens-async-compiler-linkage",
                "1.0.0",
            ]:
                scalar["format"] = "1.0.0"
            elif record in {
                "BLUEPRINT",
                "SOURCE_ASSET_SHA256",
                "SOURCE_COMPILE_SHA256",
                "COMPILE_HASH_BASIS",
                "ACTIVE_STATE_SHA256",
            } and len(fields) == 2:
                scalar[record] = fields[1]
            elif record == "SCHEDULE_OVERLAY" and len(fields) == 3:
                scalar[record] = fields[1]
                scalar["OVERLAY_SHA256"] = fields[2]
            elif record == "CONTINUATION" and len(fields) == 7:
                attributes = {}
                for field in fields[2:]:
                    name, separator, value = field.partition("=")
                    if not separator:
                        raise ValueError(f"malformed attribute {field!r}")
                    attributes[name] = value
                continuations.append(
                    CompilerContinuation(
                        node_id=fields[1],
                        source_guid=attributes["SOURCE_GUID"],
                        latent_uuid=int(attributes["LATENT_UUID"]),
                        resume_code_offsets=tuple(
                            int(item)
                            for item in attributes["RESUME_CODE_OFFSETS"].split(",")
                            if item
                        ),
                        source_match=attributes["SOURCE_MATCH"] == "1",
                        debug_match=attributes["DEBUG_MATCH"] == "1",
                    )
                )
            elif record == "COUNTS" and len(fields) == 3:
                count_row = (int(fields[1]), int(fields[2]))
            else:
                raise ValueError(f"unexpected {record!r} record shape")
        except (KeyError, ValueError) as error:
            _fail(
                COMPILER_LINKAGE_INVALID,
                f"malformed compiler audit row {line_number}: {error}",
            )
    required = {
        "format",
        "BLUEPRINT",
        "SOURCE_ASSET_SHA256",
        "SOURCE_COMPILE_SHA256",
        "COMPILE_HASH_BASIS",
        "SCHEDULE_OVERLAY",
        "OVERLAY_SHA256",
        "ACTIVE_STATE_SHA256",
    }
    if set(scalar) != required:
        _fail(COMPILER_LINKAGE_INVALID, "compiler audit header is incomplete")
    if count_row != (len(continuations), len(continuations)) or len(continuations) != 2:
        _fail(COMPILER_LINKAGE_INVALID, "compiler continuation counts do not reconcile")
    if len({item.node_id for item in continuations}) != len(continuations):
        _fail(COMPILER_LINKAGE_INVALID, "compiler continuation identity is duplicated")
    if any(not item.source_match or not item.debug_match for item in continuations):
        _fail(COMPILER_LINKAGE_INVALID, "compiler continuation is not source/debug matched")
    return CompilerAudit(
        blueprint_asset_path=scalar["BLUEPRINT"],
        source_asset_sha256=scalar["SOURCE_ASSET_SHA256"],
        source_compile_sha256=scalar["SOURCE_COMPILE_SHA256"],
        compile_hash_basis=scalar["COMPILE_HASH_BASIS"],
        continuations=tuple(continuations),
        schedule_variant=scalar["SCHEDULE_OVERLAY"],
        overlay_sha256=scalar["OVERLAY_SHA256"],
        active_state_sha256=scalar["ACTIVE_STATE_SHA256"],
    )


def load_async_products(evidence_dir: str | Path, asset_path: str | Path) -> AsyncProducts:
    """Load the retained source, audit, trace and reviewed truth products."""

    evidence = Path(evidence_dir).resolve()
    asset = Path(asset_path).resolve()
    ground_truth_path = evidence / "reviewed-ground-truth.v1.json"
    ground_truth = _load_json(ground_truth_path)
    sources: dict[str, Mapping[str, Any]] = {}
    audits: dict[str, CompilerAudit] = {}
    audit_texts: dict[str, str] = {}
    traces: list[Mapping[str, Any]] = []
    trace_keys: list[str] = []
    files: dict[str, Mapping[str, str]] = {}
    for run in _RUNS:
        run_dir = evidence / run
        for variant in _VARIANTS:
            key = f"{run}/{variant}"
            source_path = run_dir / f"{variant}.async-source.json"
            audit_path = run_dir / f"{variant}.async-compiler-linkage.tsv"
            trace_matches = sorted(run_dir.glob(f"*.{variant}.async-trace.json"))
            if len(trace_matches) != 1:
                raise LC4AsyncArtifactError(
                    f"expected one {key} trace, found {len(trace_matches)}"
                )
            trace_path = trace_matches[0]
            source = _load_json(source_path)
            audit_text = audit_path.read_text(encoding="utf-8")
            sources[key] = source
            audit_texts[key] = audit_text
            audits[key] = parse_async_compiler_audit(audit_text)
            traces.append(_load_json(trace_path))
            trace_keys.append(key)
            files[key] = {
                "source_file": source_path.relative_to(evidence).as_posix(),
                "source_sha256": _sha256_file(source_path),
                "compiler_audit_file": audit_path.relative_to(evidence).as_posix(),
                "compiler_audit_sha256": _sha256_file(audit_path),
                "trace_file": trace_path.relative_to(evidence).as_posix(),
                "trace_sha256": _sha256_file(trace_path),
            }
    return AsyncProducts(
        evidence_dir=evidence,
        asset_path=asset,
        asset_sha256=_sha256_file(asset),
        sources=sources,
        compiler_audits=audits,
        compiler_audit_texts=audit_texts,
        traces=tuple(traces),
        trace_product_keys=tuple(trace_keys),
        reviewed_ground_truth=ground_truth,
        files=files,
    )


def _ground_truth_section(products: AsyncProducts, name: str) -> Mapping[str, Any]:
    return _object(
        products.reviewed_ground_truth.get(name),
        SOURCE_BINDING_INVALID,
        f"reviewed ground truth {name}",
    )


def _validate_source_products(products: AsyncProducts) -> Mapping[str, Any]:
    review = products.reviewed_ground_truth
    if review.get("format") != "blueprint-lens-lc4-async-reviewed-ground-truth" or review.get(
        "format_version"
    ) != "1.0.0":
        _fail(SOURCE_BINDING_INVALID, "reviewed ground truth is not v1")
    if review.get("review", {}).get("status") != "reviewed_for_schema_gate":
        _fail(SOURCE_BINDING_INVALID, "ground truth was not reviewed for schema entry")
    truth = _ground_truth_section(products, "source_truth")
    adjudication = _ground_truth_section(products, "product_adjudication")
    if products.asset_sha256 != truth.get("asset_sha256"):
        _fail(SOURCE_BINDING_INVALID, "asset hash differs from reviewed truth")

    canonical: Mapping[str, Any] | None = None
    for key in (f"{run}/{variant}" for run in _RUNS for variant in _VARIANTS):
        run, variant = key.split("/")
        source = _object(products.sources.get(key), SOURCE_BINDING_INVALID, key)
        audit = products.compiler_audits.get(key)
        if audit is None:
            _fail(COMPILER_LINKAGE_INVALID, f"compiler audit is absent for {key}")
        if source.get("format") != "blueprint-lens-async-source" or source.get(
            "format_version"
        ) != "1.0.0":
            _fail(SOURCE_BINDING_INVALID, f"source product is not v1: {key}")
        provenance = _object(
            source.get("provenance"), SOURCE_BINDING_INVALID, f"{key} provenance"
        )
        overlay = _object(
            provenance.get("schedule_overlay"),
            SOURCE_BINDING_INVALID,
            f"{key} schedule overlay",
        )
        variant_truth = _object(
            adjudication.get(variant), SOURCE_BINDING_INVALID, f"{variant} adjudication"
        )
        if overlay.get("schedule_variant") != variant:
            _fail(SOURCE_BINDING_INVALID, f"source schedule differs for {key}")
        expected_hashes = {
            "source_sha256": products.files[key]["source_sha256"],
            "compiler_audit_sha256": products.files[key]["compiler_audit_sha256"],
            "compile_sha256": provenance.get("compile_sha256"),
            "overlay_sha256": overlay.get("overlay_sha256"),
            "active_state_sha256": provenance.get("active_state_sha256"),
        }
        if any(variant_truth.get(name) != value for name, value in expected_hashes.items()):
            _fail(SOURCE_BINDING_INVALID, f"reviewed product hashes differ for {key}")
        if (
            provenance.get("asset_sha256") != products.asset_sha256
            or source.get("blueprint_asset_path") != truth.get("asset_path")
            or source.get("graph_id") != truth.get("graph_id")
            or source.get("sequence_node_id") != truth.get("sequence_node_id")
            or source.get("criterion_node_id") != truth.get("criterion", {}).get("node_id")
        ):
            _fail(SOURCE_BINDING_INVALID, f"source identity differs for {key}")

        launches = _array(source.get("launches"), SOURCE_BINDING_INVALID, f"{key} launches")
        continuations = _array(
            source.get("continuations"), SOURCE_BINDING_INVALID, f"{key} continuations"
        )
        participants = _array(
            source.get("participants"), BARRIER_INVALID, f"{key} participants"
        )
        barrier = _object(source.get("barrier"), BARRIER_INVALID, f"{key} barrier")
        if [item.get("participant_id") for item in launches] != list(_PARTICIPANTS):
            _fail(BARRIER_INVALID, f"launch participant binding differs for {key}")
        if [item.get("ordinal") for item in launches] != [0, 1]:
            _fail(SOURCE_BINDING_INVALID, f"launch ordinals differ for {key}")
        if [item.get("participant_id") for item in participants] != list(_PARTICIPANTS):
            _fail(BARRIER_INVALID, f"declared participants differ for {key}")
        if [item.get("participant_id") for item in barrier.get("arrival_call_sites", [])] != list(
            _PARTICIPANTS
        ):
            _fail(BARRIER_INVALID, f"barrier arrival sites differ for {key}")
        if (
            barrier.get("barrier_object_identity") != "FBlueprintLensAsyncBarrierState"
            or barrier.get("reset_policy") != "explicit_only"
            or barrier.get("cancel_policy") != "closes_invocation"
            or barrier.get("single_fire_guarantee") is not True
        ):
            _fail(BARRIER_INVALID, f"barrier lifecycle differs for {key}")
        truth_launches = truth.get("launches", [])
        for source_launch, truth_launch in zip(launches, truth_launches, strict=True):
            if any(
                source_launch.get(source_name) != truth_launch.get(truth_name)
                for source_name, truth_name in (
                    ("participant_id", "participant_id"),
                    ("ordinal", "ordinal"),
                    ("launch_node_id", "launch_node_id"),
                    ("source_pin_id", "source_pin_id"),
                )
            ):
                _fail(SOURCE_BINDING_INVALID, f"reviewed launch differs for {key}")
        truth_continuations = truth.get("continuations", [])
        for continuation, truth_continuation in zip(
            continuations, truth_continuations, strict=True
        ):
            if any(
                continuation.get(name) != truth_continuation.get(name)
                for name in (
                    "continuation_id",
                    "node_id",
                    "source_node_guid",
                    "latent_uuid",
                    "resume_code_offsets",
                )
            ):
                _fail(COMPILER_LINKAGE_INVALID, f"reviewed continuation differs for {key}")
        if source.get("criterion") != truth.get("criterion"):
            _fail(SOURCE_BINDING_INVALID, f"reviewed criterion differs for {key}")

        if (
            audit.blueprint_asset_path != source.get("blueprint_asset_path")
            or audit.source_asset_sha256 != products.asset_sha256
            or audit.source_compile_sha256 != provenance.get("compile_sha256")
            or audit.compile_hash_basis != provenance.get("compile_hash_basis")
            or audit.schedule_variant != variant
            or audit.overlay_sha256 != overlay.get("overlay_sha256")
            or audit.active_state_sha256 != provenance.get("active_state_sha256")
        ):
            _fail(COMPILER_LINKAGE_INVALID, f"compiler/source binding differs for {key}")
        compiler_by_node = {item.node_id: item for item in audit.continuations}
        if set(compiler_by_node) != {str(item.get("node_id", "")) for item in continuations}:
            _fail(COMPILER_LINKAGE_INVALID, f"compiler continuation coverage differs for {key}")
        for continuation in continuations:
            compiler = compiler_by_node[str(continuation["node_id"])]
            if (
                compiler.source_guid != continuation.get("source_node_guid")
                or compiler.latent_uuid != continuation.get("latent_uuid")
                or list(compiler.resume_code_offsets)
                != continuation.get("resume_code_offsets")
            ):
                _fail(COMPILER_LINKAGE_INVALID, f"compiler linkage differs for {key}")
        bounds = _object(source.get("trace_bounds"), SOURCE_BINDING_INVALID, "trace bounds")
        if (
            bounds.get("fixed_world_delta_seconds") != 0.05
            or bounds.get("deadline_ticks") != 8
            or bounds.get("trace_capacity") != 64
        ):
            _fail(SOURCE_BINDING_INVALID, f"trace bounds differ for {key}")

        invariant = deepcopy(dict(source))
        invariant["provenance"] = deepcopy(dict(provenance))
        invariant["provenance"].pop("schedule_overlay", None)
        invariant["provenance"].pop("compile_sha256", None)
        invariant["provenance"].pop("active_state_sha256", None)
        for item in invariant["continuations"]:
            item.pop("duration", None)
        if canonical is None:
            canonical = invariant
        elif invariant != canonical:
            _fail(SOURCE_BINDING_INVALID, f"source-invariant facts drift in {key}")

    for variant in _VARIANTS:
        if products.files[f"run1/{variant}"]["source_sha256"] != products.files[
            f"run2/{variant}"
        ]["source_sha256"]:
            _fail(SOURCE_BINDING_INVALID, f"repeated {variant} source is not deterministic")
        if products.files[f"run1/{variant}"]["compiler_audit_sha256"] != products.files[
            f"run2/{variant}"
        ]["compiler_audit_sha256"]:
            _fail(
                COMPILER_LINKAGE_INVALID,
                f"repeated {variant} compiler audit is not deterministic",
            )
    if canonical is None:
        _fail(SOURCE_BINDING_INVALID, "no source products were loaded")
    return canonical


def _events_of(trace: Mapping[str, Any], kind: str) -> list[Mapping[str, Any]]:
    return [item for item in trace["events"] if item.get("event_kind") == kind]


def _validate_trace(
    trace: Mapping[str, Any],
    product_key: str,
    source: Mapping[str, Any],
) -> dict[str, Any]:
    variant = product_key.split("/")[1]
    if trace.get("format") != "blueprint-lens-async-trace" or trace.get(
        "format_version"
    ) != "1.0.0":
        _fail(TRACE_COMPLETENESS_INVALID, f"trace is not v1: {product_key}")
    if trace.get("schedule_variant") != variant:
        _fail(TRACE_SCHEDULE_INVALID, f"trace schedule differs for {product_key}")
    for name in ("run_id", "trace_id", "instance_id", "invocation_id"):
        _non_empty(trace.get(name), TRACE_IDENTITY_INVALID, f"{product_key} {name}")
    events = [
        _object(item, TRACE_COMPLETENESS_INVALID, f"{product_key} event")
        for item in _array(trace.get("events"), TRACE_COMPLETENESS_INVALID, "events")
    ]
    if not events:
        _fail(TRACE_COMPLETENESS_INVALID, f"trace is empty: {product_key}")
    event_ids = [str(item.get("event_id", "")) for item in events]
    if any(not value for value in event_ids) or len(event_ids) != len(set(event_ids)):
        _fail(TRACE_IDENTITY_INVALID, f"event identity is absent or duplicated: {product_key}")
    for event in events:
        if any(
            event.get(name) != trace.get(name)
            for name in ("trace_id", "instance_id", "invocation_id")
        ):
            _fail(TRACE_IDENTITY_INVALID, f"event/header identity differs: {product_key}")
    observation_indices = [item.get("observation_index") for item in events]
    if observation_indices != list(range(len(events))):
        _fail(TRACE_ORDER_INVALID, f"observation indices are not contiguous: {product_key}")
    world_ticks = [item.get("world_tick") for item in events]
    if any(not isinstance(value, int) or isinstance(value, bool) for value in world_ticks):
        _fail(TRACE_ORDER_INVALID, f"world tick is invalid: {product_key}")
    if world_ticks != sorted(world_ticks):
        _fail(TRACE_ORDER_INVALID, f"world ticks move backwards: {product_key}")
    if (
        trace.get("recording_limit") != source.get("trace_bounds", {}).get("trace_capacity")
        or trace.get("dropped_event_count") != 0
        or trace.get("close_reason") != "complete"
        or trace.get("complete") is not True
    ):
        _fail(TRACE_COMPLETENESS_INVALID, f"trace is not complete: {product_key}")
    deadline = source.get("trace_bounds", {}).get("deadline_ticks")
    if not isinstance(deadline, int) or world_ticks[-1] > deadline:
        _fail(TRACE_COMPLETENESS_INVALID, f"trace exceeds deadline: {product_key}")
    kinds = Counter(str(item.get("event_kind", "")) for item in events)
    if kinds != _EXPECTED_EVENT_COUNTS:
        barrier_inventory_changed = (
            kinds.get("barrier_arrival", 0) != 2
            or kinds.get("barrier_release", 0) != 1
            or any(
                kinds.get(name, 0)
                for name in ("duplicate_arrival", "unknown_arrival", "reset", "cancelled")
            )
        )
        code = BARRIER_INVALID if barrier_inventory_changed else TRACE_COMPLETENESS_INVALID
        _fail(code, f"event inventory differs for {product_key}: {dict(kinds)}")
    boundaries = _events_of(trace, "trace_boundary")
    if (
        events[0] is not boundaries[0]
        or boundaries[0].get("boundary_phase") != "open"
        or events[-1] is not boundaries[-1]
        or boundaries[-1].get("boundary_phase") != "close"
    ):
        _fail(TRACE_COMPLETENESS_INVALID, f"trace boundaries are incomplete: {product_key}")

    continuation_by_id = {
        str(item["continuation_id"]): item for item in source["continuations"]
    }
    launch_by_participant = {
        str(item["participant_id"]): item for item in source["launches"]
    }
    participant_by_id = {
        str(item["participant_id"]): item for item in source["participants"]
    }
    launches = _events_of(trace, "launch")
    completions = _events_of(trace, "completion")
    arrivals = _events_of(trace, "barrier_arrival")
    releases = _events_of(trace, "barrier_release")
    criteria = _events_of(trace, "criterion")
    if [item.get("continuation_id") for item in launches] != list(_PARTICIPANTS):
        _fail(TRACE_ORDER_INVALID, f"launch order differs for {product_key}")
    if [item.get("continuation_id") for item in completions] != list(
        _EXPECTED_COMPLETION[variant]
    ):
        _fail(TRACE_SCHEDULE_INVALID, f"completion schedule differs for {product_key}")
    if {item.get("participant_id") for item in arrivals} != set(_PARTICIPANTS):
        _fail(BARRIER_INVALID, f"arrival participant set differs for {product_key}")
    for launch in launches:
        participant = str(launch.get("continuation_id", ""))
        if launch.get("source_occurrence_id") != launch_by_participant[participant].get(
            "launch_node_id"
        ):
            _fail(TRACE_IDENTITY_INVALID, f"launch source binding differs for {product_key}")
    for completion in completions:
        participant = str(completion.get("continuation_id", ""))
        if completion.get("source_occurrence_id") != continuation_by_id[participant].get(
            "node_id"
        ):
            _fail(TRACE_IDENTITY_INVALID, f"completion source binding differs for {product_key}")
    for arrival in arrivals:
        participant = str(arrival.get("participant_id", ""))
        if (
            arrival.get("continuation_id") != participant
            or participant not in participant_by_id
            or arrival.get("source_occurrence_id")
            != participant_by_id[participant].get("arrival_node_id")
        ):
            _fail(BARRIER_INVALID, f"arrival source binding differs for {product_key}")
    release = releases[0]
    criterion = criteria[0]
    if release.get("source_occurrence_id") != source["barrier"].get("release_site_id"):
        _fail(BARRIER_INVALID, f"release source binding differs for {product_key}")
    if criterion.get("source_occurrence_id") != source["criterion"].get("node_id"):
        _fail(BARRIER_INVALID, f"criterion source binding differs for {product_key}")
    order = {item["event_id"]: int(item["observation_index"]) for item in events}
    completion_for = {str(item["continuation_id"]): item for item in completions}
    arrival_for = {str(item["participant_id"]): item for item in arrivals}
    if any(
        order[completion_for[item]["event_id"]] >= order[arrival_for[item]["event_id"]]
        for item in _PARTICIPANTS
    ):
        _fail(BARRIER_INVALID, f"arrival precedes its completion: {product_key}")
    if max(order[item["event_id"]] for item in arrivals) >= order[release["event_id"]]:
        _fail(BARRIER_INVALID, f"release occurs before all arrivals: {product_key}")
    if order[release["event_id"]] >= order[criterion["event_id"]]:
        _fail(BARRIER_INVALID, f"criterion does not follow release: {product_key}")
    return {
        "events": events,
        "launches": launches,
        "completions": completions,
        "arrivals": arrivals,
        "release": release,
        "criterion": criterion,
    }


def _evidence(kind: str, artifact_id: str, fact_id: str) -> dict[str, str]:
    return {"evidence_kind": kind, "artifact_id": artifact_id, "fact_id": fact_id}


def _relation(
    trace_id: str,
    suffix: str,
    relation_type: str,
    from_id: str,
    to_id: str,
    claim_scope: str,
    evidence_refs: Sequence[Mapping[str, str]],
) -> dict[str, Any]:
    return {
        "relation_id": f"{trace_id}:relation:{suffix}",
        "relation_type": relation_type,
        "from_id": from_id,
        "to_id": to_id,
        "claim_scope": claim_scope,
        "evidence_refs": [dict(item) for item in evidence_refs],
        "derived_from_relation_ids": [],
    }


def _relations_for_invocation(
    trace: Mapping[str, Any],
    product_key: str,
    validated: Mapping[str, Any],
    source: Mapping[str, Any],
) -> list[dict[str, Any]]:
    trace_id = str(trace["trace_id"])
    source_file = f"{product_key}.async-source"
    audit_file = f"{product_key}.compiler-audit"
    trace_file = f"{product_key}.trace"
    launches = {str(item["continuation_id"]): item for item in validated["launches"]}
    completions = {
        str(item["continuation_id"]): item for item in validated["completions"]
    }
    arrivals = {str(item["participant_id"]): item for item in validated["arrivals"]}
    release = validated["release"]
    criterion = validated["criterion"]
    relations = [
        _relation(
            trace_id,
            "launch-order-A-B",
            "launch_order",
            str(launches["A"]["event_id"]),
            str(launches["B"]["event_id"]),
            "source_guaranteed",
            (
                _evidence("source_fact", source_file, "launch:A:ordinal:0"),
                _evidence("source_fact", source_file, "launch:B:ordinal:1"),
                _evidence("trace_event", trace_file, str(launches["A"]["event_id"])),
                _evidence("trace_event", trace_file, str(launches["B"]["event_id"])),
            ),
        )
    ]
    for participant in _PARTICIPANTS:
        relations.append(
            _relation(
                trace_id,
                f"continuation-{participant}",
                "continuation_of",
                str(launches[participant]["event_id"]),
                str(completions[participant]["event_id"]),
                "observed_invocation",
                (
                    _evidence("compiler_audit", audit_file, f"continuation:{participant}"),
                    _evidence("trace_event", trace_file, str(launches[participant]["event_id"])),
                    _evidence("trace_event", trace_file, str(completions[participant]["event_id"])),
                ),
            )
        )
        relations.append(
            _relation(
                trace_id,
                f"local-resume-{participant}",
                "local_resume_order",
                str(completions[participant]["event_id"]),
                str(arrivals[participant]["event_id"]),
                "observed_invocation",
                (
                    _evidence("trace_event", trace_file, str(completions[participant]["event_id"])),
                    _evidence("trace_event", trace_file, str(arrivals[participant]["event_id"])),
                ),
            )
        )
        relations.append(
            _relation(
                trace_id,
                f"participant-{participant}",
                "participant_of",
                str(arrivals[participant]["event_id"]),
                str(release["event_id"]),
                "observed_invocation",
                (
                    _evidence("source_fact", source_file, f"participant:{participant}"),
                    _evidence("trace_event", trace_file, str(arrivals[participant]["event_id"])),
                ),
            )
        )
        relations.append(
            _relation(
                trace_id,
                f"barrier-waits-{participant}",
                "barrier_waits_for",
                str(arrivals[participant]["event_id"]),
                str(release["event_id"]),
                "source_guaranteed",
                (
                    _evidence("source_fact", source_file, "barrier:release=all"),
                    _evidence("trace_event", trace_file, str(arrivals[participant]["event_id"])),
                    _evidence("trace_event", trace_file, str(release["event_id"])),
                ),
            )
        )
    relations.extend(
        [
            _relation(
                trace_id,
                "barrier-release",
                "barrier_release",
                str(release["event_id"]),
                str(criterion["event_id"]),
                "observed_invocation",
                (
                    _evidence("source_fact", source_file, "barrier:single-fire"),
                    *(
                        _evidence("trace_event", trace_file, str(item["event_id"]))
                        for item in (*validated["arrivals"], release, criterion)
                    ),
                ),
            ),
            _relation(
                trace_id,
                "criterion-after-release",
                "criterion_after_release",
                str(release["event_id"]),
                str(criterion["event_id"]),
                "observed_invocation",
                (
                    _evidence("trace_event", trace_file, str(release["event_id"])),
                    _evidence("trace_event", trace_file, str(criterion["event_id"])),
                ),
            ),
        ]
    )
    return relations


def _reaches(relations: Sequence[Mapping[str, Any]], start: str, target: str) -> bool:
    outgoing: dict[str, set[str]] = {}
    for relation in relations:
        outgoing.setdefault(str(relation["from_id"]), set()).add(str(relation["to_id"]))
    frontier = [start]
    visited = {start}
    while frontier:
        current = frontier.pop()
        for next_id in outgoing.get(current, set()):
            if next_id == target:
                return True
            if next_id not in visited:
                visited.add(next_id)
                frontier.append(next_id)
    return False


def build_async_profile(products: AsyncProducts) -> dict[str, Any]:
    """Bind independently owned source/audit/trace products into one checked profile."""

    canonical = _validate_source_products(products)
    all_header_identities: list[str] = []
    all_event_ids: list[str] = []
    invocations: list[dict[str, Any]] = []
    for trace, key in zip(products.traces, products.trace_product_keys, strict=True):
        for name in ("run_id", "trace_id", "invocation_id"):
            all_header_identities.append(str(trace.get(name, "")))
        validated = _validate_trace(trace, key, products.sources[key])
        all_event_ids.extend(str(item["event_id"]) for item in validated["events"])
        relations = _relations_for_invocation(trace, key, validated, products.sources[key])
        completions = {str(item["continuation_id"]): item for item in validated["completions"]}
        left_id = str(completions["A"]["event_id"])
        right_id = str(completions["B"]["event_id"])
        left_reaches_right = _reaches(relations, left_id, right_id)
        right_reaches_left = _reaches(relations, right_id, left_id)
        if left_reaches_right or right_reaches_left:
            _fail(INCOMPARABILITY_INVALID, f"completion relation was forged in {key}")
        invocations.append(
            {
                "product_id": key,
                "run_id": trace["run_id"],
                "trace_id": trace["trace_id"],
                "instance_id": trace["instance_id"],
                "invocation_id": trace["invocation_id"],
                "schedule_variant": trace["schedule_variant"],
                "recording_limit": trace["recording_limit"],
                "dropped_event_count": trace["dropped_event_count"],
                "close_reason": trace["close_reason"],
                "complete": trace["complete"],
                "launch_event_ids": [item["event_id"] for item in validated["launches"]],
                "completion_event_ids": [
                    item["event_id"] for item in validated["completions"]
                ],
                "completion_order": [
                    item["continuation_id"] for item in validated["completions"]
                ],
                "arrival_event_ids": [item["event_id"] for item in validated["arrivals"]],
                "barrier_release_event_id": validated["release"]["event_id"],
                "criterion_event_id": validated["criterion"]["event_id"],
                "relations": relations,
                "incomparability_checks": [
                    {
                        "left_continuation_id": "A",
                        "right_continuation_id": "B",
                        "left_completion_event_id": left_id,
                        "right_completion_event_id": right_id,
                        "left_reaches_right": False,
                        "right_reaches_left": False,
                        "relation_set_complete": True,
                        "result": "incomparable",
                        "proof_basis": "pairwise_reachability_plus_completeness",
                        "evidence_relation_ids": [
                            relation["relation_id"] for relation in relations
                        ],
                    }
                ],
            }
        )
    if (
        any(not value for value in all_header_identities)
        or len(all_header_identities) != len(set(all_header_identities))
    ):
        _fail(TRACE_IDENTITY_INVALID, "run, trace and invocation identities are not unique")
    if len(all_event_ids) != len(set(all_event_ids)):
        _fail(TRACE_IDENTITY_INVALID, "event occurrence identity is not globally unique")
    invocations.sort(key=lambda item: str(item["product_id"]))

    truth = products.reviewed_ground_truth["source_truth"]
    source_sample = products.sources["run1/A_FIRST"]
    product_bindings = []
    for key in sorted(products.files):
        source = products.sources[key]
        product_bindings.append(
            {
                "product_id": key,
                **dict(products.files[key]),
                "compile_sha256": source["provenance"]["compile_sha256"],
                "overlay_sha256": source["provenance"]["schedule_overlay"][
                    "overlay_sha256"
                ],
                "active_state_sha256": source["provenance"]["active_state_sha256"],
            }
        )
    relation_count = sum(len(item["relations"]) for item in invocations)
    profile = {
        "format": PROFILE_FORMAT,
        "schema_version": PROFILE_VERSION,
        "profile_id": PROFILE_ID,
        "rules_version": RULES_VERSION,
        "validation_state": "VALIDATED_PROFILE",
        "source": {
            "engine_version": source_sample["engine_version"],
            "asset_path": truth["asset_path"],
            "asset_sha256": products.asset_sha256,
            "graph_id": truth["graph_id"],
            "sequence_node_id": truth["sequence_node_id"],
            "criterion_node_id": truth["criterion"]["node_id"],
            "criterion_execute_pin_id": truth["criterion"]["execute_pin_id"],
            "criterion_source_action": truth["criterion"]["source_action"],
            "criterion_assigned_value": truth["criterion"]["assigned_value"],
            "reviewed_ground_truth_file": "reviewed-ground-truth.v1.json",
            "reviewed_ground_truth_sha256": _sha256_file(
                products.evidence_dir / "reviewed-ground-truth.v1.json"
            ),
        },
        "launches": [deepcopy(item) for item in canonical["launches"]],
        "continuations": [
            {name: deepcopy(item[name]) for name in (
                "continuation_id",
                "node_id",
                "node_family",
                "latent_function_name",
                "source_node_guid",
                "latent_uuid",
                "resume_pin_id",
                "resume_code_offsets",
            )}
            for item in canonical["continuations"]
        ],
        "barrier": {
            **deepcopy(dict(canonical["barrier"])),
            "participant_ids": list(_PARTICIPANTS),
        },
        "schedule_variants": deepcopy(list(source_sample["schedule_variants"])),
        "boundaries": deepcopy(list(canonical["boundaries"])),
        "trace_bounds": deepcopy(dict(canonical["trace_bounds"])),
        "product_bindings": product_bindings,
        "invocations": invocations,
        "counts": {
            "launch_count": 2,
            "continuation_count": 2,
            "participant_count": 2,
            "schedule_variant_count": 2,
            "source_product_count": 4,
            "compiler_audit_count": 4,
            "invocation_count": 4,
            "relation_count": relation_count,
            "incomparability_check_count": 4,
        },
    }
    return profile


def _validate_relations(
    invocation: Mapping[str, Any], expected: Mapping[str, Any]
) -> None:
    relations = _array(
        invocation.get("relations"), CAUSAL_RELATION_INVALID, "invocation relations"
    )
    if Counter(str(item.get("relation_type", "")) for item in relations) != _RELATION_COUNTS:
        _fail(CAUSAL_RELATION_INVALID, "relation inventory is incomplete or forged")
    local_ids = {
        *invocation.get("launch_event_ids", []),
        *invocation.get("completion_event_ids", []),
        *invocation.get("arrival_event_ids", []),
        invocation.get("barrier_release_event_id"),
        invocation.get("criterion_event_id"),
    }
    for relation in relations:
        if relation.get("from_id") not in local_ids or relation.get("to_id") not in local_ids:
            _fail(CAUSAL_RELATION_INVALID, "relation crosses its invocation boundary")
        if not relation.get("evidence_refs"):
            _fail(CAUSAL_RELATION_INVALID, "relation lacks typed evidence")
        if relation.get("claim_scope") not in {
            "source_guaranteed",
            "observed_invocation",
            "derived_closure",
        }:
            _fail(CAUSAL_RELATION_INVALID, "relation claim scope is invalid")
    if relations != expected.get("relations"):
        _fail(CAUSAL_RELATION_INVALID, "relations differ from validated source/trace facts")


def _validate_incomparability(invocation: Mapping[str, Any]) -> None:
    checks = _array(
        invocation.get("incomparability_checks"),
        INCOMPARABILITY_INVALID,
        "incomparability checks",
    )
    if len(checks) != 1:
        _fail(INCOMPARABILITY_INVALID, "exactly one A/B check is required")
    check = checks[0]
    if (
        check.get("proof_basis") != "pairwise_reachability_plus_completeness"
        or check.get("left_reaches_right") is not False
        or check.get("right_reaches_left") is not False
        or check.get("relation_set_complete") is not True
        or check.get("result") != "incomparable"
    ):
        _fail(INCOMPARABILITY_INVALID, "incomparability proof is incomplete or scalar-only")
    relations = invocation["relations"]
    left = str(check.get("left_completion_event_id", ""))
    right = str(check.get("right_completion_event_id", ""))
    if _reaches(relations, left, right) or _reaches(relations, right, left):
        _fail(INCOMPARABILITY_INVALID, "a completion reaches the other completion")
    if check.get("evidence_relation_ids") != [item["relation_id"] for item in relations]:
        _fail(INCOMPARABILITY_INVALID, "incomparability evidence does not cover relations")


def validate_async_profile(profile: Mapping[str, Any], products: AsyncProducts) -> None:
    """Rebuild the authoritative projection and reject semantic drift fail-closed."""

    expected = build_async_profile(products)
    top_names = (
        "format",
        "schema_version",
        "profile_id",
        "rules_version",
        "validation_state",
        "source",
        "launches",
        "continuations",
        "schedule_variants",
        "boundaries",
        "trace_bounds",
        "product_bindings",
    )
    if any(profile.get(name) != expected.get(name) for name in top_names):
        _fail(SOURCE_BINDING_INVALID, "profile source/product binding differs")
    if profile.get("barrier") != expected.get("barrier"):
        _fail(BARRIER_INVALID, "profile barrier differs from source truth")
    invocations = _array(profile.get("invocations"), TRACE_IDENTITY_INVALID, "invocations")
    expected_invocations = expected["invocations"]
    if len(invocations) != len(expected_invocations):
        _fail(TRACE_IDENTITY_INVALID, "invocation coverage differs")
    expected_by_product = {item["product_id"]: item for item in expected_invocations}
    if len({item.get("product_id") for item in invocations}) != len(invocations):
        _fail(TRACE_IDENTITY_INVALID, "profile product identity is duplicated")
    for invocation in invocations:
        expected_invocation = expected_by_product.get(invocation.get("product_id"))
        if expected_invocation is None:
            _fail(TRACE_IDENTITY_INVALID, "profile contains an unknown invocation")
        relation_free = {
            key: value
            for key, value in invocation.items()
            if key not in {"relations", "incomparability_checks"}
        }
        expected_relation_free = {
            key: value
            for key, value in expected_invocation.items()
            if key not in {"relations", "incomparability_checks"}
        }
        if relation_free != expected_relation_free:
            _fail(TRACE_IDENTITY_INVALID, "invocation projection differs from trace")
        _validate_relations(invocation, expected_invocation)
        _validate_incomparability(invocation)
        if invocation.get("incomparability_checks") != expected_invocation.get(
            "incomparability_checks"
        ):
            _fail(INCOMPARABILITY_INVALID, "incomparability record differs")
    if profile.get("counts") != expected.get("counts"):
        _fail(COUNT_MISMATCH, "profile counts do not reconcile")


def _expect_diagnostic(
    name: str,
    expected: str,
    callback: Callable[[], Any],
) -> dict[str, Any]:
    try:
        callback()
    except LC4AsyncError as error:
        if error.code != expected:
            raise LC4AsyncArtifactError(
                f"mutation {name} raised {error.code}, expected {expected}"
            ) from error
        return {"name": name, "expected_diagnostic": expected, "passed": True}
    raise LC4AsyncArtifactError(f"mutation {name} was accepted")


def _replace_products_source(
    products: AsyncProducts, key: str, callback: Callable[[dict[str, Any]], None]
) -> AsyncProducts:
    sources = deepcopy(dict(products.sources))
    changed = deepcopy(dict(sources[key]))
    callback(changed)
    sources[key] = changed
    return replace(products, sources=sources)


def _replace_products_trace(
    products: AsyncProducts, index: int, callback: Callable[[dict[str, Any]], None]
) -> AsyncProducts:
    traces = deepcopy(list(products.traces))
    callback(traces[index])
    return replace(products, traces=tuple(traces))


def _insert_event(trace: dict[str, Any], kind: str, before_kind: str) -> None:
    events = trace["events"]
    before_index = next(
        index for index, event in enumerate(events) if event["event_kind"] == before_kind
    )
    event = deepcopy(events[before_index - 1])
    event["event_id"] = f"{trace['trace_id']}:event:mutation:{kind}:{before_index}"
    event["event_kind"] = kind
    event["boundary_phase"] = ""
    if kind == "reset":
        event["continuation_id"] = ""
        event["participant_id"] = ""
        event["source_occurrence_id"] = "BlueprintLensAsyncBarrier:LC4_RUN"
    events.insert(before_index, event)
    for index, item in enumerate(events):
        item["observation_index"] = index


def run_async_mutations(
    profile: Mapping[str, Any], products: AsyncProducts
) -> dict[str, Any]:
    """Run the frozen 24-case source/audit/trace/profile adversarial matrix."""

    cases: list[dict[str, Any]] = []

    changed = _replace_products_source(
        products,
        "run1/A_FIRST",
        lambda source: source["continuations"][0].__setitem__(
            "latent_uuid", source["continuations"][0]["latent_uuid"] + 1
        ),
    )
    cases.append(_expect_diagnostic("wrong_latent_uuid", COMPILER_LINKAGE_INVALID, lambda: build_async_profile(changed)))

    def swap_participants(source: dict[str, Any]) -> None:
        source["launches"][0]["participant_id"], source["launches"][1]["participant_id"] = (
            source["launches"][1]["participant_id"], source["launches"][0]["participant_id"]
        )

    changed = _replace_products_source(products, "run1/A_FIRST", swap_participants)
    cases.append(_expect_diagnostic("swapped_participant_ids", BARRIER_INVALID, lambda: build_async_profile(changed)))

    def duplicate_event(trace: dict[str, Any]) -> None:
        trace["events"][2]["event_id"] = trace["events"][1]["event_id"]

    changed = _replace_products_trace(products, 0, duplicate_event)
    cases.append(_expect_diagnostic("duplicate_event_occurrence", TRACE_IDENTITY_INVALID, lambda: build_async_profile(changed)))

    changed_profile = deepcopy(dict(profile))
    changed_profile["invocations"][0]["relations"][0]["to_id"] = changed_profile["invocations"][1]["launch_event_ids"][1]
    cases.append(_expect_diagnostic("cross_invocation_relation", CAUSAL_RELATION_INVALID, lambda: validate_async_profile(changed_profile, products)))

    changed_profile = deepcopy(dict(profile))
    relation = changed_profile["invocations"][0]["relations"][0]
    relation["from_id"], relation["to_id"] = changed_profile["invocations"][0]["completion_event_ids"]
    cases.append(_expect_diagnostic("forged_completion_relation", CAUSAL_RELATION_INVALID, lambda: validate_async_profile(changed_profile, products)))

    changed_profile = deepcopy(dict(profile))
    relation = next(item for item in changed_profile["invocations"][0]["relations"] if item["relation_type"] == "criterion_after_release")
    relation["from_id"], relation["to_id"] = relation["to_id"], relation["from_id"]
    cases.append(_expect_diagnostic("reversed_release_relation", CAUSAL_RELATION_INVALID, lambda: validate_async_profile(changed_profile, products)))

    changed_profile = deepcopy(dict(profile))
    changed_profile["invocations"][0]["relations"].pop()
    cases.append(_expect_diagnostic("incomplete_relation_set", CAUSAL_RELATION_INVALID, lambda: validate_async_profile(changed_profile, products)))

    changed_profile = deepcopy(dict(profile))
    changed_profile["invocations"][0]["incomparability_checks"][0]["proof_basis"] = "observation_index_only"
    cases.append(_expect_diagnostic("scalar_only_incomparability", INCOMPARABILITY_INVALID, lambda: validate_async_profile(changed_profile, products)))

    changed = _replace_products_source(products, "run1/A_FIRST", lambda source: source["participants"].pop())
    cases.append(_expect_diagnostic("missing_participant", BARRIER_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: _insert_event(trace, "barrier_arrival", "barrier_release"))
    cases.append(_expect_diagnostic("duplicate_arrival", BARRIER_INVALID, lambda: build_async_profile(changed)))

    def release_before_all(trace: dict[str, Any]) -> None:
        events = trace["events"]
        release_index = next(index for index, item in enumerate(events) if item["event_kind"] == "barrier_release")
        arrival_indices = [index for index, item in enumerate(events) if item["event_kind"] == "barrier_arrival"]
        release = events.pop(release_index)
        events.insert(arrival_indices[-1], release)
        for index, item in enumerate(events):
            item["observation_index"] = index

    changed = _replace_products_trace(products, 0, release_before_all)
    cases.append(_expect_diagnostic("release_before_all", BARRIER_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: _insert_event(trace, "barrier_release", "criterion"))
    cases.append(_expect_diagnostic("duplicate_release", BARRIER_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: _insert_event(trace, "reset", "barrier_release"))
    cases.append(_expect_diagnostic("reset_event_in_positive_trace", BARRIER_INVALID, lambda: build_async_profile(changed)))

    def cancel(trace: dict[str, Any]) -> None:
        trace["complete"] = False
        trace["close_reason"] = "cancelled"

    changed = _replace_products_trace(products, 0, cancel)
    cases.append(_expect_diagnostic("cancelled_trace", TRACE_COMPLETENESS_INVALID, lambda: build_async_profile(changed)))

    def duplicate_invocation(trace: dict[str, Any]) -> None:
        original = products.traces[0]
        trace["invocation_id"] = original["invocation_id"]
        for event in trace["events"]:
            event["invocation_id"] = original["invocation_id"]

    changed = _replace_products_trace(products, 1, duplicate_invocation)
    cases.append(_expect_diagnostic("reentry_duplicate_invocation", TRACE_IDENTITY_INVALID, lambda: build_async_profile(changed)))

    def drop(trace: dict[str, Any]) -> None:
        trace["dropped_event_count"] = 1
        trace["complete"] = False
        trace["close_reason"] = "overflow"

    changed = _replace_products_trace(products, 0, drop)
    cases.append(_expect_diagnostic("dropped_event", TRACE_COMPLETENESS_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: trace["events"].pop())
    cases.append(_expect_diagnostic("truncated_trace", TRACE_COMPLETENESS_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: trace.__setitem__("schedule_variant", "B_FIRST"))
    cases.append(_expect_diagnostic("wrong_schedule_header", TRACE_SCHEDULE_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_trace(products, 0, lambda trace: trace["events"][4].__setitem__("observation_index", 99))
    cases.append(_expect_diagnostic("out_of_order_observation_index", TRACE_ORDER_INVALID, lambda: build_async_profile(changed)))

    def incomplete(trace: dict[str, Any]) -> None:
        trace["complete"] = False
        trace["close_reason"] = "timeout"

    changed = _replace_products_trace(products, 0, incomplete)
    cases.append(_expect_diagnostic("incomplete_invocation", TRACE_COMPLETENESS_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_source(products, "run1/A_FIRST", lambda source: source["barrier"].__setitem__("barrier_object_identity", "OrdinaryMerge"))
    cases.append(_expect_diagnostic("ordinary_merge_as_and_barrier", BARRIER_INVALID, lambda: build_async_profile(changed)))

    def add_participant(source: dict[str, Any]) -> None:
        added = deepcopy(source["participants"][0])
        added["participant_id"] = "C"
        source["participants"].append(added)

    changed = _replace_products_source(products, "run1/A_FIRST", add_participant)
    cases.append(_expect_diagnostic("undeclared_barrier_participant", BARRIER_INVALID, lambda: build_async_profile(changed)))

    audits = dict(products.compiler_audits)
    audit = audits["run1/A_FIRST"]
    audits["run1/A_FIRST"] = replace(audit, continuations=audit.continuations[:1])
    changed = replace(products, compiler_audits=audits)
    cases.append(_expect_diagnostic("missing_compiler_linkage", COMPILER_LINKAGE_INVALID, lambda: build_async_profile(changed)))

    changed = _replace_products_source(products, "run1/A_FIRST", lambda source: source["provenance"].__setitem__("compile_sha256", "0" * 64))
    cases.append(_expect_diagnostic("stale_compile_hash", SOURCE_BINDING_INVALID, lambda: build_async_profile(changed)))

    return {
        "format": "blueprint-lens-lc4-async-mutations",
        "schema_version": "1.0.0",
        "status": "PASS",
        "case_count": len(cases),
        "cases": cases,
    }


def _build_in_directory(
    products: AsyncProducts,
    output_dir: Path,
    schema_path: Path,
) -> dict[str, Path]:
    asset_hash_before = _sha256_file(products.asset_path)
    profile = build_async_profile(products)
    profile_path = output_dir / "BP_LC4_AsyncBarrier.async-profile.v1.json"
    _write_json(profile_path, profile)
    validate_json_file(profile_path, schema_path)
    validate_async_profile(profile, products)
    mutations = run_async_mutations(profile, products)
    mutation_path = output_dir / "mutation-report.json"
    _write_json(mutation_path, mutations)
    asset_hash_after = _sha256_file(products.asset_path)
    checks = {
        "asset_hash_stable": asset_hash_before == asset_hash_after,
        "source_runs_deterministic": all(
            products.files[f"run1/{variant}"]["source_sha256"]
            == products.files[f"run2/{variant}"]["source_sha256"]
            for variant in _VARIANTS
        ),
        "compiler_audits_deterministic": all(
            products.files[f"run1/{variant}"]["compiler_audit_sha256"]
            == products.files[f"run2/{variant}"]["compiler_audit_sha256"]
            for variant in _VARIANTS
        ),
        "four_complete_identity_distinct_traces": len(profile["invocations"]) == 4,
        "both_schedule_variants_repeated": Counter(
            item["schedule_variant"] for item in profile["invocations"]
        )
        == Counter({"A_FIRST": 2, "B_FIRST": 2}),
        "profile_schema_valid": True,
        "profile_semantic_valid": True,
        "all_mutations_rejected": mutations["status"] == "PASS",
        "reviewed_ground_truth_bound": True,
    }
    if not all(checks.values()):
        failed = sorted(name for name, passed in checks.items() if not passed)
        raise LC4AsyncArtifactError(f"LC4-ASYNC readiness checks failed: {failed}")
    gate = {
        "format": "blueprint-lens-lc4-async-schema-gate",
        "schema_version": "1.0.0",
        "status": "SCHEMA_VALIDATOR_MUTATIONS_VERIFIED__READINESS_DECISION_NEXT",
        "profile_id": PROFILE_ID,
        "rules_version": RULES_VERSION,
        "next_gate": "readiness/TRUTH_FROZEN decision",
        "checks": checks,
        "counts": {
            **profile["counts"],
            "mutation_case_count": mutations["case_count"],
        },
        "hashes": {
            "asset_sha256_before": asset_hash_before,
            "asset_sha256_after": asset_hash_after,
            "reviewed_ground_truth_sha256": profile["source"][
                "reviewed_ground_truth_sha256"
            ],
            "async_profile_sha256": _sha256_file(profile_path),
            "mutation_report_sha256": _sha256_file(mutation_path),
        },
        "artifacts": {
            "async_profile": profile_path.name,
            "mutation_report": mutation_path.name,
        },
        "boundaries": deepcopy(profile["boundaries"]),
        "limitations": [
            "Any future TRUTH_FROZEN decision would apply only to the bounded two-Delay BP_LC4_AsyncBarrier profile.",
            "core-v1 remains DEFERRED__CORE_V1_FRONTIER_ONLY",
            "Observation index, world tick and spatial placement do not prove causal order or incomparability.",
            "No UE-visible surface, human comprehension, preference, general scalability or product default is established.",
            "Surface work and LC5 remain separately gated.",
        ],
    }
    gate_path = output_dir / "schema-gate.json"
    _write_json(gate_path, gate)
    if _sha256_file(products.asset_path) != asset_hash_before:
        raise LC4AsyncArtifactError("LC4-ASYNC Blueprint package changed during analysis")
    return {
        "profile": profile_path,
        "mutations": mutation_path,
        "gate": gate_path,
    }


def build_lc4_async_artifacts(
    evidence_dir: str | Path,
    asset_path: str | Path,
    output_dir: str | Path,
    schema_path: str | Path,
) -> dict[str, Path]:
    """Build in staging and publish the schema Gate record last."""

    evidence = Path(evidence_dir).resolve()
    asset = Path(asset_path).resolve()
    destination = Path(output_dir).resolve()
    schema = Path(schema_path).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    gate_path = destination / "schema-gate.json"
    gate_path.unlink(missing_ok=True)
    products = load_async_products(evidence, asset)
    try:
        with tempfile.TemporaryDirectory(prefix=".lc4-async-staging-", dir=destination) as staging:
            staged = _build_in_directory(products, Path(staging), schema)
            published: dict[str, Path] = {}
            for key in ("profile", "mutations"):
                target = destination / staged[key].name
                os.replace(staged[key], target)
                published[key] = target
            os.replace(staged["gate"], gate_path)
            published["gate"] = gate_path
            return published
    except Exception:
        gate_path.unlink(missing_ok=True)
        raise
