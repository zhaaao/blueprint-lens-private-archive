"""Truth-preserving visual projection for the bounded LC4-ASYNC profile."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import html
import hashlib
import json
from pathlib import Path
import textwrap
from typing import Any, Mapping

from PIL import Image, ImageDraw, ImageFont


CONDITION_IDS = (
    "LC4_ASYNC_COMPLETE_TEXT",
    "LC4_ASYNC_CAUSAL_EVENT_REGISTER",
    "LC4_ASYNC_BARRIER_LANES",
)
VARIANTS = ("A_FIRST", "B_FIRST")
WIDTHS = (430, 480, 700)

_CONDITION_SLUGS = {
    "LC4_ASYNC_COMPLETE_TEXT": "complete-text",
    "LC4_ASYNC_CAUSAL_EVENT_REGISTER": "causal-event-register",
    "LC4_ASYNC_BARRIER_LANES": "barrier-lanes",
}
_CONDITION_LABELS = {
    "LC4_ASYNC_COMPLETE_TEXT": "Complete Async Text",
    "LC4_ASYNC_CAUSAL_EVENT_REGISTER": "Causal Event Register",
    "LC4_ASYNC_BARRIER_LANES": "Barrier Lanes",
}
_PALETTE = {
    "canvas": "#171a1f",
    "panel": "#20252b",
    "panel_alt": "#252b32",
    "line": "#3b444e",
    "text": "#f0f4f7",
    "muted": "#aab4bd",
    "cyan": "#69d9df",
    "cyan_fill": "#20363a",
    "purple": "#b69cff",
    "purple_fill": "#302943",
    "gold": "#f1c85d",
    "gold_fill": "#443919",
    "green": "#79d59b",
    "green_fill": "#21382a",
    "orange": "#eca867",
    "orange_fill": "#402e20",
    "danger": "#ef8b8b",
}


class LC4AsyncVisualError(ValueError):
    """Raised when a visual projection violates the frozen async contract."""


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _portable_path(path: Path) -> str:
    parts = list(path.resolve().parts)
    for anchor in ("artifacts", "schemas", "analysis"):
        if anchor in parts:
            return "/".join(parts[parts.index(anchor) :])
    return path.name


def _load_json(path: str | Path) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _event_ids(invocation: Mapping[str, Any]) -> list[str]:
    return [
        *invocation["launch_event_ids"],
        *invocation["completion_event_ids"],
        *invocation["arrival_event_ids"],
        invocation["barrier_release_event_id"],
        invocation["criterion_event_id"],
    ]


def _source_entities(profile: Mapping[str, Any]) -> dict[str, Any]:
    source = profile["source"]
    return {
        "sequence": {
            "entity_id": source["sequence_node_id"],
            "entity_kind": "ordered_launch_source",
            "label": "Sequence · launch A then B",
        },
        "launches": deepcopy(profile["launches"]),
        "continuations": deepcopy(profile["continuations"]),
        "barrier": deepcopy(profile["barrier"]),
        "criterion": {
            "entity_id": source["criterion_node_id"],
            "execute_pin_id": source["criterion_execute_pin_id"],
            "label": source["criterion_source_action"],
            "assigned_value": source["criterion_assigned_value"],
        },
    }


def build_accountable_ledger(
    profile_path: str | Path,
    readiness_path: str | Path,
) -> dict[str, Any]:
    """Project the frozen profile without adding layout or semantic facts."""

    profile_file = Path(profile_path)
    readiness_file = Path(readiness_path)
    profile = _load_json(profile_file)
    readiness = _load_json(readiness_file)
    if readiness.get("scope") != profile.get("profile_id"):
        raise LC4AsyncVisualError("readiness scope does not match the async profile")
    profile_hash = _sha256(profile_file)
    if readiness.get("hashes", {}).get("async_profile_sha256") != profile_hash:
        raise LC4AsyncVisualError("readiness does not bind the async profile bytes")

    invocations: list[dict[str, Any]] = []
    relations: list[dict[str, Any]] = []
    proofs: list[dict[str, Any]] = []
    for item in profile["invocations"]:
        event_ids = _event_ids(item)
        invocation = {
            "product_id": item["product_id"],
            "schedule_variant": item["schedule_variant"],
            "run_id": item["run_id"],
            "trace_id": item["trace_id"],
            "invocation_id": item["invocation_id"],
            "instance_id": item["instance_id"],
            "complete": item["complete"],
            "close_reason": item["close_reason"],
            "recording_limit": item["recording_limit"],
            "dropped_event_count": item["dropped_event_count"],
            "completion_order": deepcopy(item["completion_order"]),
            "event_ids": event_ids,
            "launch_event_ids": deepcopy(item["launch_event_ids"]),
            "completion_event_ids": deepcopy(item["completion_event_ids"]),
            "arrival_event_ids": deepcopy(item["arrival_event_ids"]),
            "barrier_release_event_id": item["barrier_release_event_id"],
            "criterion_event_id": item["criterion_event_id"],
        }
        invocations.append(invocation)
        relations.extend(
            {"product_id": item["product_id"], **deepcopy(relation)}
            for relation in item["relations"]
        )
        for index, proof in enumerate(item["incomparability_checks"]):
            proofs.append(
                {
                    "proof_id": f'{item["product_id"]}:proof:A_parallel_B:{index}',
                    "product_id": item["product_id"],
                    **deepcopy(proof),
                }
            )

    variant_projections: dict[str, Any] = {}
    for variant in VARIANTS:
        selected = [item for item in invocations if item["schedule_variant"] == variant]
        product_ids = {item["product_id"] for item in selected}
        variant_projections[variant] = {
            "invocation_ids": [item["invocation_id"] for item in selected],
            "event_ids": [event_id for item in selected for event_id in item["event_ids"]],
            "relation_ids": [
                item["relation_id"] for item in relations if item["product_id"] in product_ids
            ],
            "proof_ids": [
                item["proof_id"] for item in proofs if item["product_id"] in product_ids
            ],
            "product_ids": sorted(product_ids),
        }

    ledger = {
        "format": "blueprint-lens-lc4-async-accountable-ledger",
        "schema_version": "1.0.0",
        "status": "PROFILE_BOUND__COMPLETE",
        "question": (
            "Why does Set LC4AsyncComplete execute only after both independent "
            "latent branches complete, and can either completion be first?"
        ),
        "profile_binding": {
            "profile_id": profile["profile_id"],
            "profile_path": _portable_path(profile_file),
            "profile_sha256": profile_hash,
            "readiness_path": _portable_path(readiness_file),
            "readiness_sha256": _sha256(readiness_file),
            "rules_version": profile["rules_version"],
            "validation_state": profile["validation_state"],
            "core_v1_outcome": readiness["core_v1_outcome"],
        },
        "source_entities": _source_entities(profile),
        "invocations": invocations,
        "relations": relations,
        "incomparability_proofs": proofs,
        "boundaries": deepcopy(profile["boundaries"]),
        "actions": [
            {"action_id": "select", "label": "Select"},
            {"action_id": "show_relation_proof", "label": "Show relation proof"},
            {"action_id": "show_all_text", "label": "Show all text"},
            {"action_id": "show_evidence", "label": "Show evidence"},
            {
                "action_id": "open_source",
                "label": "Open source",
                "target_node_id": profile["source"]["criterion_node_id"],
                "target_pin_id": profile["source"]["criterion_execute_pin_id"],
            },
        ],
        "variant_projections": variant_projections,
        "counts": {
            "source_launch_sites": len(profile["launches"]),
            "continuations": len(profile["continuations"]),
            "participants": len(profile["barrier"]["participant_ids"]),
            "invocations": len(invocations),
            "relations": len(relations),
            "incomparability_proofs": len(proofs),
            "boundaries": len(profile["boundaries"]),
        },
    }
    validate_accountable_ledger(ledger)
    return ledger


def validate_accountable_ledger(ledger: Mapping[str, Any]) -> None:
    """Reject semantic omissions, duplication and causal visual promotion."""

    counts = ledger.get("counts", {})
    expected = {
        "source_launch_sites": 2,
        "continuations": 2,
        "participants": 2,
        "invocations": 4,
        "relations": 44,
        "incomparability_proofs": 4,
        "boundaries": 4,
    }
    if counts != expected:
        raise LC4AsyncVisualError(f"ledger counts differ: {counts}")
    invocations = ledger.get("invocations", [])
    if len({item["invocation_id"] for item in invocations}) != 4:
        raise LC4AsyncVisualError("invocation identity is missing or duplicated")
    event_ids = [event_id for item in invocations for event_id in item["event_ids"]]
    if len(event_ids) != 32 or len(set(event_ids)) != 32:
        raise LC4AsyncVisualError("event occurrence identity is missing or duplicated")
    relations = ledger.get("relations", [])
    relation_ids = [item["relation_id"] for item in relations]
    if len(relation_ids) != 44 or len(set(relation_ids)) != 44:
        raise LC4AsyncVisualError("relation identity is missing or duplicated")
    event_set_by_product = {
        item["product_id"]: set(item["event_ids"]) for item in invocations
    }
    for relation in relations:
        product_events = event_set_by_product.get(relation["product_id"], set())
        if relation["from_id"] not in product_events or relation["to_id"] not in product_events:
            raise LC4AsyncVisualError("relation crosses or escapes its invocation")
        if not relation["evidence_refs"]:
            raise LC4AsyncVisualError("relation is not evidence bound")
    proofs = ledger.get("incomparability_proofs", [])
    if len({item["proof_id"] for item in proofs}) != 4:
        raise LC4AsyncVisualError("incomparability proof identity is duplicated")
    for proof in proofs:
        if (
            proof["left_reaches_right"]
            or proof["right_reaches_left"]
            or not proof["relation_set_complete"]
            or proof["result"] != "incomparable"
            or proof["proof_basis"] != "pairwise_reachability_plus_completeness"
        ):
            raise LC4AsyncVisualError("incomparability proof is incomplete")
    if Counter(item["schedule_variant"] for item in invocations) != Counter(
        {"A_FIRST": 2, "B_FIRST": 2}
    ):
        raise LC4AsyncVisualError("schedule variant coverage differs")
    for variant in VARIANTS:
        projection = ledger["variant_projections"].get(variant, {})
        if (
            len(projection.get("invocation_ids", [])) != 2
            or len(projection.get("event_ids", [])) != 16
            or len(projection.get("relation_ids", [])) != 22
            or len(projection.get("proof_ids", [])) != 2
        ):
            raise LC4AsyncVisualError(f"{variant} coverage differs")
        variant_products = {
            item["product_id"]
            for item in invocations
            if item["schedule_variant"] == variant
        }
        expected_invocation_ids = {
            item["invocation_id"]
            for item in invocations
            if item["schedule_variant"] == variant
        }
        expected_event_ids = {
            event_id
            for item in invocations
            if item["schedule_variant"] == variant
            for event_id in item["event_ids"]
        }
        expected_relation_ids = {
            item["relation_id"]
            for item in relations
            if item["product_id"] in variant_products
        }
        expected_proof_ids = {
            item["proof_id"]
            for item in proofs
            if item["product_id"] in variant_products
        }
        if (
            set(projection["product_ids"]) != variant_products
            or set(projection["invocation_ids"]) != expected_invocation_ids
            or set(projection["event_ids"]) != expected_event_ids
            or set(projection["relation_ids"]) != expected_relation_ids
            or set(projection["proof_ids"]) != expected_proof_ids
        ):
            raise LC4AsyncVisualError(f"{variant} identity sets differ")
    action_ids = {item["action_id"] for item in ledger.get("actions", [])}
    if action_ids != {
        "select",
        "show_relation_proof",
        "show_all_text",
        "show_evidence",
        "open_source",
    }:
        raise LC4AsyncVisualError("action coverage differs")
    boundary_kinds = {item["boundary_kind"] for item in ledger.get("boundaries", [])}
    if boundary_kinds != {"scheduler", "world_tick", "external_service", "cancellation"}:
        raise LC4AsyncVisualError("boundary coverage differs")


def _source_ids(ledger: Mapping[str, Any]) -> list[str]:
    source = ledger["source_entities"]
    ids = [source["sequence"]["entity_id"], source["criterion"]["entity_id"]]
    for launch in source["launches"]:
        ids.extend((launch["source_identity"], launch["source_pin_id"]))
    for continuation in source["continuations"]:
        ids.extend((continuation["node_id"], continuation["resume_pin_id"]))
    barrier = source["barrier"]
    ids.extend(
        (
            barrier["barrier_site_id"],
            barrier["release_site_id"],
            barrier["begin_invocation_node_id"],
        )
    )
    for arrival in barrier["arrival_call_sites"]:
        ids.extend(
            (
                arrival["arrival_node_id"],
                arrival["arrival_execute_pin_id"],
                arrival["release_pin_id"],
            )
        )
    return sorted(set(ids))


def information_set(ledger: Mapping[str, Any], variant: str) -> dict[str, Any]:
    """Return the exact semantic set every condition must expose."""

    if variant not in VARIANTS:
        raise LC4AsyncVisualError(f"unknown variant: {variant}")
    projection = ledger["variant_projections"][variant]
    return {
        "source_entity_ids": _source_ids(ledger),
        "invocation_ids": sorted(projection["invocation_ids"]),
        "event_ids": sorted(projection["event_ids"]),
        "relation_ids": sorted(projection["relation_ids"]),
        "proof_ids": sorted(projection["proof_ids"]),
        "boundary_kinds": sorted(item["boundary_kind"] for item in ledger["boundaries"]),
        "action_ids": sorted(item["action_id"] for item in ledger["actions"]),
        "counts": {
            "source_launch_sites": 2,
            "continuations": 2,
            "participants": 2,
            "retained_invocations": 2,
            "relations": 22,
            "incomparability_proofs": 2,
            "arrivals_per_invocation": 2,
            "releases_per_invocation": 1,
            "criteria_per_invocation": 1,
        },
    }


def _effect_name(condition_id: str, variant: str, width: int, suffix: str) -> str:
    return (
        f"lc4-async-{_CONDITION_SLUGS[condition_id]}-"
        f"{variant.lower().replace('_', '-')}-effect-{width}.{suffix}"
    )


def build_visual_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Build the strict 3 x 3 x 2 information-matched review contract."""

    validate_accountable_ledger(ledger)
    states: list[dict[str, Any]] = []
    for condition_id in CONDITION_IDS:
        for variant in VARIANTS:
            for width in WIDTHS:
                states.append(
                    {
                        "state_id": f"{condition_id}__{variant}__W{width}",
                        "condition_id": condition_id,
                        "condition_label": _CONDITION_LABELS[condition_id],
                        "variant": variant,
                        "width": width,
                        "information_set": information_set(ledger, variant),
                        "effect_paths": {
                            "svg": _effect_name(condition_id, variant, width, "svg"),
                            "png": _effect_name(condition_id, variant, width, "png"),
                        },
                    }
                )
    manifest = {
        "format": "blueprint-lens-lc4-async-visual-manifest",
        "schema_version": "1.0.0",
        "status": "INFORMATION_MATCHED__18_REVIEW_STATES",
        "question": ledger["question"],
        "profile_binding": deepcopy(ledger["profile_binding"]),
        "default_condition_id": "LC4_ASYNC_BARRIER_LANES",
        "conditions": [
            {
                "condition_id": condition_id,
                "label": _CONDITION_LABELS[condition_id],
                "role": (
                    "engineering_target"
                    if condition_id == "LC4_ASYNC_BARRIER_LANES"
                    else "information_matched_comparison"
                ),
            }
            for condition_id in CONDITION_IDS
        ],
        "target_widths_logical_px": list(WIDTHS),
        "variants": list(VARIANTS),
        "states": states,
        "matched": [
            "source identities and source actions",
            "two retained invocations and sixteen event occurrences per variant",
            "twenty-two typed evidence-bound relations per variant",
            "two pairwise reachability plus completeness proofs per variant",
            "four explicit boundary facts and identical reconciled counts",
        ],
        "controlled_difference": (
            "text rows versus event-register grouping versus launch/portal/lane/AND-gate geometry"
        ),
        "non_claims": [
            "effect images are not native Slate or UE-visible evidence",
            "no comprehension, preference, scalability or product default is established",
            "completion order is observed and is never promoted to a causal relation",
        ],
    }
    validate_visual_manifest(manifest, ledger)
    return manifest


def validate_visual_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    states = manifest.get("states", [])
    if len(states) != 18 or len({item["state_id"] for item in states}) != 18:
        raise LC4AsyncVisualError("manifest must contain exactly 18 unique states")
    expected_tuples = {
        (condition, variant, width)
        for condition in CONDITION_IDS
        for variant in VARIANTS
        for width in WIDTHS
    }
    actual_tuples = {
        (item["condition_id"], item["variant"], item["width"]) for item in states
    }
    if actual_tuples != expected_tuples:
        raise LC4AsyncVisualError("manifest state matrix differs")
    effect_paths = [
        path for state in states for path in state.get("effect_paths", {}).values()
    ]
    if len(effect_paths) != 36 or len(set(effect_paths)) != 36:
        raise LC4AsyncVisualError("effect path coverage differs")
    for variant in VARIANTS:
        expected = information_set(ledger, variant)
        if any(
            state["information_set"] != expected
            for state in states
            if state["variant"] == variant
        ):
            raise LC4AsyncVisualError(f"{variant} conditions are not information matched")


def _rect(
    commands: list[dict[str, Any]],
    element_id: str,
    x: int,
    y: int,
    width: int,
    height: int,
    fill: str,
    stroke: str = _PALETTE["line"],
    stroke_width: int = 1,
    radius: int = 10,
    role: str = "detail",
) -> None:
    commands.append(
        {
            "kind": "rect",
            "element_id": element_id,
            "x": x,
            "y": y,
            "width": width,
            "height": height,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "radius": radius,
            "role": role,
        }
    )


def _line(
    commands: list[dict[str, Any]],
    element_id: str,
    points: list[tuple[int, int]],
    stroke: str,
    stroke_width: int = 2,
    dashed: bool = False,
    role: str = "detail",
) -> None:
    commands.append(
        {
            "kind": "line",
            "element_id": element_id,
            "points": points,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "dashed": dashed,
            "role": role,
        }
    )


def _circle(
    commands: list[dict[str, Any]],
    element_id: str,
    cx: int,
    cy: int,
    radius: int,
    fill: str,
    stroke: str,
    stroke_width: int = 2,
    role: str = "detail",
) -> None:
    commands.append(
        {
            "kind": "circle",
            "element_id": element_id,
            "cx": cx,
            "cy": cy,
            "radius": radius,
            "fill": fill,
            "stroke": stroke,
            "stroke_width": stroke_width,
            "role": role,
        }
    )


def _arrow(
    commands: list[dict[str, Any]],
    element_id: str,
    tip_x: int,
    tip_y: int,
    direction: str,
    fill: str,
    size: int = 7,
    role: str = "detail",
) -> None:
    if direction == "right":
        points = [(tip_x, tip_y), (tip_x - size, tip_y - size // 2), (tip_x - size, tip_y + size // 2)]
    elif direction == "down":
        points = [(tip_x, tip_y), (tip_x - size // 2, tip_y - size), (tip_x + size // 2, tip_y - size)]
    else:
        raise LC4AsyncVisualError(f"unknown arrow direction: {direction}")
    commands.append(
        {
            "kind": "polygon",
            "element_id": element_id,
            "points": points,
            "fill": fill,
            "role": role,
        }
    )


def _text(
    commands: list[dict[str, Any]],
    element_id: str,
    x: int,
    y: int,
    text: str,
    size: int = 13,
    fill: str = _PALETTE["text"],
    weight: int = 400,
    role: str = "label",
    anchor: str = "start",
) -> None:
    commands.append(
        {
            "kind": "text",
            "element_id": element_id,
            "x": x,
            "y": y,
            "text": text,
            "size": size,
            "fill": fill,
            "weight": weight,
            "role": role,
            "anchor": anchor,
        }
    )


def _text_lines(
    commands: list[dict[str, Any]],
    prefix: str,
    x: int,
    y: int,
    lines: list[str],
    size: int = 13,
    fill: str = _PALETTE["text"],
    weight: int = 400,
    line_height: int | None = None,
) -> int:
    step = line_height or max(18, size + 5)
    for index, line in enumerate(lines):
        _text(commands, f"{prefix}.{index}", x, y + index * step, line, size, fill, weight)
    return y + len(lines) * step


def _header(
    commands: list[dict[str, Any]],
    condition_id: str,
    variant: str,
    width: int,
    pad: int,
) -> int:
    title_size = 23 if width >= 480 else 21
    _text(
        commands,
        "header.eyebrow",
        pad,
        28,
        "LC4 · ASYNC EXPLANATION",
        11,
        _PALETTE["cyan"],
        700,
    )
    _text(
        commands,
        "header.title",
        pad,
        62,
        _CONDITION_LABELS[condition_id],
        title_size,
        _PALETTE["text"],
        700,
    )
    badge_width = 112
    _rect(
        commands,
        "header.variant.badge",
        width - pad - badge_width,
        38,
        badge_width,
        34,
        _PALETTE["purple_fill"],
        _PALETTE["purple"],
        1,
        17,
        "variant_label",
    )
    _text(
        commands,
        "header.variant.text",
        width - pad - badge_width // 2,
        60,
        variant.replace("_", " "),
        12,
        _PALETTE["purple"],
        700,
        "variant_label",
        "middle",
    )
    question = "Why only after both complete — and can either finish first?"
    max_chars = 59 if width >= 700 else 45 if width >= 480 else 39
    lines = textwrap.wrap(question, width=max_chars)
    _text_lines(commands, "header.question", pad, 94, lines, 13, _PALETTE["muted"])
    chips = "2 runs  ·  22 relations  ·  2 proofs  ·  2/2 participants"
    _text(commands, "header.counts", pad, 132, chips, 11, _PALETTE["muted"], 600)
    return 154


def _panel_title(
    commands: list[dict[str, Any]],
    prefix: str,
    x: int,
    y: int,
    width: int,
    title: str,
    subtitle: str,
    accent: str = _PALETTE["cyan"],
    fill: str = _PALETTE["panel"],
    role: str = "detail",
) -> None:
    _line(commands, prefix, [(x + 1, y + 1), (x + width - 1, y + 1)], accent, 1, role=role)
    _text(commands, f"{prefix}.title", x + 14, y + 25, title, 13, accent, 700, role)
    _text(commands, f"{prefix}.subtitle", x + 14, y + 47, subtitle, 11, _PALETTE["muted"], 400, role)


def _short_id(value: str) -> str:
    if ":event:" in value:
        return value.rsplit(":event:", 1)[-1]
    if ":relation:" in value:
        return value.rsplit(":relation:", 1)[-1]
    return value[-12:]


def _selected_invocations(ledger: Mapping[str, Any], variant: str) -> list[dict[str, Any]]:
    return [
        item for item in ledger["invocations"] if item["schedule_variant"] == variant
    ]


def _complete_text_scene(
    ledger: Mapping[str, Any], variant: str, width: int
) -> dict[str, Any]:
    pad = {430: 16, 480: 20, 700: 24}[width]
    inner = width - 2 * pad
    commands: list[dict[str, Any]] = []
    y = _header(commands, "LC4_ASYNC_COMPLETE_TEXT", variant, width, pad)
    source_h = 132
    _rect(commands, "region.source", pad, y, inner, source_h, _PALETTE["panel"], _PALETTE["cyan"], role="source")
    _panel_title(
        commands,
        "region.source.heading",
        pad,
        y,
        inner,
        "SOURCE GUARANTEES",
        "ordered launch · typed continuations · declared AND barrier",
        role="source",
    )
    _text_lines(
        commands,
        "source.rows",
        pad + 14,
        y + 72,
        [
            "[0] launch A  →  Delay continuation A",
            "[1] launch B  →  Delay continuation B",
            "participants {A, B}  ·  explicit reset  ·  single fire",
        ],
        12,
    )
    y += source_h + 14
    invocations = _selected_invocations(ledger, variant)
    event_labels = (
        "launch A",
        "launch B",
        "complete A",
        "complete B",
        "arrive A",
        "arrive B",
        "release",
        "criterion",
    )
    for index, invocation in enumerate(invocations):
        relation_lines = [
            f'{rel_index + 1:02d}  {relation["relation_type"]:<23} [{relation["claim_scope"]}]'
            for rel_index, relation in enumerate(ledger["relations"])
            if relation["product_id"] == invocation["product_id"]
        ]
        event_lines = [
            f'{event_index + 1:02d}  {label:<12} event {_short_id(event_id)}'
            for event_index, (label, event_id) in enumerate(
                zip(event_labels, invocation["event_ids"], strict=True)
            )
        ]
        rows = event_lines + relation_lines
        panel_h = 74 + len(rows) * 18 + 12
        _rect(
            commands,
            f"region.invocation.{index}",
            pad,
            y,
            inner,
            panel_h,
            _PALETTE["panel"],
            _PALETTE["purple"],
            role="invocation",
        )
        order = " then ".join(invocation["completion_order"])
        _panel_title(
            commands,
            f"invocation.{index}.heading",
            pad,
            y,
            inner,
            f'{invocation["product_id"]} · OBSERVED ORDER: {order}',
            "8 events · 11 evidence-bound relations · complete trace",
            _PALETTE["purple"],
            role="invocation",
        )
        _text_lines(
            commands,
            f"invocation.{index}.rows",
            pad + 14,
            y + 71,
            rows,
            11 if width < 700 else 12,
            _PALETTE["text"],
            400,
            18,
        )
        y += panel_h + 14
    proof_h = 132
    _rect(commands, "region.proof", pad, y, inner, proof_h, _PALETTE["purple_fill"], _PALETTE["purple"], 2, role="proof")
    _panel_title(
        commands,
        "proof.heading",
        pad,
        y,
        inner,
        "PAIRWISE PROOF · EACH RETAINED INVOCATION",
        "absence of an edge alone is not evidence",
        _PALETTE["purple"],
        _PALETTE["purple_fill"],
        "proof",
    )
    _text_lines(
        commands,
        "proof.rows",
        pad + 14,
        y + 72,
        [
            "A reaches B: false   ·   B reaches A: false",
            "relation set complete: true   →   A ∥ B",
        ],
        12,
        _PALETTE["text"],
        600,
    )
    y += proof_h + 14
    y = _common_footer(commands, pad, y, inner, width)
    return _scene("LC4_ASYNC_COMPLETE_TEXT", variant, width, y, commands)


def _event_register_scene(
    ledger: Mapping[str, Any], variant: str, width: int
) -> dict[str, Any]:
    pad = {430: 16, 480: 20, 700: 24}[width]
    inner = width - 2 * pad
    commands: list[dict[str, Any]] = []
    y = _header(commands, "LC4_ASYNC_CAUSAL_EVENT_REGISTER", variant, width, pad)
    source_w = 240 if width == 700 else inner
    event_x = pad + source_w + 24 if width == 700 else pad
    event_w = inner - source_w - 24 if width == 700 else inner
    source_h = 274
    _rect(commands, "region.source", pad, y, source_w, source_h, _PALETTE["panel"], _PALETTE["cyan"], role="source")
    _panel_title(
        commands,
        "register.source.heading",
        pad,
        y,
        source_w,
        "SOURCE REGISTER",
        "guaranteed · stable across variants",
        role="source",
    )
    _text_lines(
        commands,
        "register.source.rows",
        pad + 14,
        y + 76,
        [
            "01  Sequence launch A",
            "02  Sequence launch B",
            "03  Delay continuation A",
            "04  Delay continuation B",
            "05  participant A",
            "06  participant B",
            "07  release requires all",
            "08  release once",
            "09  Set LC4AsyncComplete",
        ],
        11 if width < 700 else 12,
        _PALETTE["text"],
        400,
        20,
    )
    invocations = _selected_invocations(ledger, variant)
    event_h = 274
    if width == 700:
        _register_event_panel(commands, ledger, invocations, variant, event_x, y, event_w, event_h, width)
        y += max(source_h, event_h) + 14
    else:
        y += source_h + 14
        _register_event_panel(commands, ledger, invocations, variant, event_x, y, event_w, event_h, width)
        y += event_h + 14
    relation_h = 216
    _rect(commands, "region.relations", pad, y, inner, relation_h, _PALETTE["panel"], _PALETTE["green"], role="relations")
    _panel_title(
        commands,
        "register.relations.heading",
        pad,
        y,
        inner,
        "CAUSAL RELATION REGISTER · 22",
        "typed ownership · display order is not a new edge",
        _PALETTE["green"],
        role="relations",
    )
    relation_counts = Counter(
        relation["relation_type"]
        for relation in ledger["relations"]
        if relation["product_id"] in {item["product_id"] for item in invocations}
    )
    rows = [
        f"{name:<25} × {count:>2}   "
        + ("source guaranteed" if name in {"launch_order", "barrier_waits_for"} else "observed invocation")
        for name, count in sorted(relation_counts.items())
    ]
    _text_lines(commands, "register.relations.rows", pad + 14, y + 74, rows, 11 if width < 700 else 12, line_height=19)
    y += relation_h + 14
    proof_h = 144
    _rect(commands, "region.proof", pad, y, inner, proof_h, _PALETTE["purple_fill"], _PALETTE["purple"], 2, role="proof")
    _panel_title(
        commands,
        "register.proof.heading",
        pad,
        y,
        inner,
        "INCOMPARABILITY REGISTER · 2 PROOFS",
        "both retained runs satisfy the complete relation-set rule",
        _PALETTE["purple"],
        _PALETTE["purple_fill"],
        "proof",
    )
    _text_lines(
        commands,
        "register.proof.rows",
        pad + 14,
        y + 73,
        [
            "A reaches B: false   |   B reaches A: false",
            "relation set complete: true   |   result: A ∥ B",
        ],
        12,
        weight=600,
    )
    y += proof_h + 14
    y = _common_footer(commands, pad, y, inner, width)
    return _scene("LC4_ASYNC_CAUSAL_EVENT_REGISTER", variant, width, y, commands)


def _register_event_panel(
    commands: list[dict[str, Any]],
    ledger: Mapping[str, Any],
    invocations: list[dict[str, Any]],
    variant: str,
    x: int,
    y: int,
    width: int,
    height: int,
    canvas_width: int,
) -> None:
    _rect(commands, "region.events", x, y, width, height, _PALETTE["panel"], _PALETTE["purple"], role="invocation")
    order = "A then B" if variant == "A_FIRST" else "B then A"
    _panel_title(
        commands,
        "register.events.heading",
        x,
        y,
        width,
        f"OBSERVED EVENT REGISTER · {order}",
        "run1 + run2 · each complete · 8 events",
        _PALETTE["purple"],
        role="invocation",
    )
    event_names = ["launch A", "launch B", "complete A", "complete B", "arrive A", "arrive B", "release", "criterion"]
    rows = []
    for invocation in invocations:
        alias = invocation["product_id"].split("/")[0]
        rows.append(f"{alias.upper()}  {'  >  '.join(event_names)}")
        rows.append(f"       observed completion: {' then '.join(invocation['completion_order'])}")
    wrap_width = max(31, (width - 28) // 7)
    wrapped = [part for row in rows for part in textwrap.wrap(row, width=wrap_width) or [""]]
    _text_lines(
        commands,
        "register.events.rows",
        x + 14,
        y + 76,
        wrapped,
        11 if canvas_width < 700 else 12,
        line_height=20,
    )


def _barrier_lanes_scene(
    ledger: Mapping[str, Any], variant: str, width: int
) -> dict[str, Any]:
    pad = {430: 16, 480: 20, 700: 24}[width]
    inner = width - 2 * pad
    commands: list[dict[str, Any]] = []
    y = _header(commands, "LC4_ASYNC_BARRIER_LANES", variant, width, pad)
    source_h = 148
    _rect(commands, "region.source", pad, y, inner, source_h, _PALETTE["cyan_fill"], _PALETTE["cyan"], 2, role="source")
    _panel_title(
        commands,
        "lanes.source.heading",
        pad,
        y,
        inner,
        "ORDERED LAUNCH RAIL · SOURCE GUARANTEE",
        "Sequence launches A before B; this says nothing about completion order",
        _PALETTE["cyan"],
        _PALETTE["cyan_fill"],
        "source",
    )
    observed_order = "A then B" if variant == "A_FIRST" else "B then A"
    _text(
        commands,
        "lanes.observed.order",
        width - pad - 14 if width >= 700 else pad + 14,
        y + (25 if width >= 700 else 72),
        f"Observed order: {observed_order}",
        11,
        _PALETTE["purple"],
        700,
        "variant_label",
        "end" if width >= 700 else "start",
    )
    rail_y = y + 104
    x_a = pad + 58
    x_b = width - pad - 58
    _line(commands, "source.rail", [(x_a, rail_y), (x_b, rail_y)], _PALETTE["cyan"], 3, role="source")
    _arrow(commands, "source.rail.arrow", x_b - 28, rail_y, "right", _PALETTE["cyan"], 9, "source")
    _circle(commands, "source.launch.a", x_a, rail_y, 18, _PALETTE["cyan_fill"], _PALETTE["cyan"], 3, "source")
    _circle(commands, "source.launch.b", x_b, rail_y, 18, _PALETTE["cyan_fill"], _PALETTE["cyan"], 3, "source")
    _text(commands, "source.launch.a.label", x_a, rail_y + 5, "A · 0", 12, _PALETTE["text"], 700, "source", "middle")
    _text(commands, "source.launch.b.label", x_b, rail_y + 5, "B · 1", 12, _PALETTE["text"], 700, "source", "middle")
    y += source_h + 14
    if width == 430:
        lane_w = inner
        lane_h = 196
        for lane_index, participant in enumerate(("A", "B")):
            portal_h = 66
            _portal(commands, participant, pad, y, lane_w, portal_h)
            y += portal_h + 10
            _lane(commands, participant, variant, pad, y, lane_w, lane_h, lane_index)
            y += lane_h + 14
    else:
        gutter = 20 if width == 480 else 40
        lane_w = (inner - gutter) // 2
        portal_h = 68
        lane_h = 210
        _portal(commands, "A", pad, y, lane_w, portal_h)
        _portal(commands, "B", pad + lane_w + gutter, y, lane_w, portal_h)
        y += portal_h + 10
        _lane(commands, "A", variant, pad, y, lane_w, lane_h, 0)
        _lane(commands, "B", variant, pad + lane_w + gutter, y, lane_w, lane_h, 1)
        y += lane_h + 14
    proof_h = 150
    _rect(commands, "region.proof", pad, y, inner, proof_h, _PALETTE["purple_fill"], _PALETTE["purple"], 2, role="proof")
    _panel_title(
        commands,
        "lanes.proof.heading",
        pad,
        y,
        inner,
        "PAIRWISE REACHABILITY + COMPLETENESS",
        "lane position and observed finish order are not causal proof",
        _PALETTE["purple"],
        _PALETTE["purple_fill"],
        "proof",
    )
    _text_lines(
        commands,
        "lanes.proof.rows",
        pad + 14,
        y + 76,
        [
            "A reaches B: false   ·   B reaches A: false",
            "relation set complete: true",
            "THEREFORE   A ∥ B   (two retained proofs)",
        ],
        12 if width < 700 else 13,
        _PALETTE["text"],
        650,
        20,
    )
    y += proof_h + 16
    gate_h = 112
    gate_x = pad + (10 if width < 700 else 42)
    gate_w = inner - (20 if width < 700 else 84)
    socket_a_x = gate_x + 44
    socket_b_x = gate_x + gate_w - 44
    _line(commands, "barrier.arrival.a.route", [(socket_a_x, y - 14), (socket_a_x, y + 8)], _PALETTE["gold"], 3, role="barrier")
    _arrow(commands, "barrier.arrival.a.arrow", socket_a_x, y + 13, "down", _PALETTE["gold"], 7, "barrier")
    _line(commands, "barrier.arrival.b.route", [(socket_b_x, y - 14), (socket_b_x, y + 8)], _PALETTE["gold"], 3, role="barrier")
    _arrow(commands, "barrier.arrival.b.arrow", socket_b_x, y + 13, "down", _PALETTE["gold"], 7, "barrier")
    _rect(commands, "region.barrier", gate_x, y, gate_w, gate_h, _PALETTE["gold_fill"], _PALETTE["gold"], 4, 5, "barrier")
    socket_y = y + 18
    _circle(commands, "barrier.socket.a", socket_a_x, socket_y, 8, _PALETTE["gold"], _PALETTE["text"], 1, "barrier")
    _circle(commands, "barrier.socket.b", socket_b_x, socket_y, 8, _PALETTE["gold"], _PALETTE["text"], 1, "barrier")
    _text(commands, "barrier.socket.a.label", socket_a_x, socket_y - 15, "A arrived", 10, _PALETTE["gold"], 600, "barrier", "middle")
    _text(commands, "barrier.socket.b.label", socket_b_x, socket_y - 15, "B arrived", 10, _PALETTE["gold"], 600, "barrier", "middle")
    _text(commands, "barrier.title", width // 2, y + 60, "AND BARRIER · 2/2 arrived", 16 if width >= 480 else 14, _PALETTE["text"], 750, "barrier", "middle")
    _text(commands, "barrier.release", width // 2, y + 88, "RELEASE ONCE (1)", 13, _PALETTE["gold"], 750, "barrier", "middle")
    y += gate_h
    _line(commands, "barrier.release.route", [(width // 2, y), (width // 2, y + 34)], _PALETTE["gold"], 4, role="barrier")
    _arrow(commands, "barrier.release.route.arrow", width // 2, y + 31, "down", _PALETTE["gold"], 9, "barrier")
    y += 34
    criterion_h = 86
    _rect(commands, "region.criterion", gate_x, y, gate_w, criterion_h, _PALETTE["green_fill"], _PALETTE["green"], 3, role="criterion")
    _text(commands, "criterion.label", width // 2, y + 38, "CRITERION", 11, _PALETTE["green"], 750, "criterion", "middle")
    _text(commands, "criterion.value", width // 2, y + 65, "Set LC4AsyncComplete = true", 15 if width >= 480 else 13, _PALETTE["text"], 700, "criterion", "middle")
    y += criterion_h + 16
    y = _common_footer(commands, pad, y, inner, width)
    return _scene("LC4_ASYNC_BARRIER_LANES", variant, width, y, commands)


def _portal(
    commands: list[dict[str, Any]], participant: str, x: int, y: int, width: int, height: int
) -> None:
    _rect(commands, f"portal.{participant}", x, y, width, height, _PALETTE["panel"], _PALETTE["orange"], 2, 10, "portal")
    _line(commands, f"portal.{participant}.seam", [(x + 16, y + height - 5), (x + width - 16, y + height - 5)], _PALETTE["orange"], 2, True, "portal")
    _text(commands, f"portal.{participant}.title", x + 14, y + 24, f"ASYNC PORTAL {participant}", 12, _PALETTE["orange"], 700, "portal")
    _text(commands, f"portal.{participant}.detail", x + 14, y + 46, "Delay · typed resume linkage", 11, _PALETTE["muted"], 400, "portal")


def _lane(
    commands: list[dict[str, Any]],
    participant: str,
    variant: str,
    x: int,
    y: int,
    width: int,
    height: int,
    lane_index: int,
) -> None:
    first = (variant == "A_FIRST" and participant == "A") or (
        variant == "B_FIRST" and participant == "B"
    )
    _rect(commands, f"lane.{participant}", x, y, width, height, _PALETTE["panel_alt"], _PALETTE["purple"], 2, role="lane")
    _text(commands, f"lane.{participant}.title", x + 14, y + 28, f"COMPLETION LANE {participant}", 13, _PALETTE["purple"], 750, "lane")
    _text(commands, f"lane.{participant}.order", x + 14, y + 53, "completed FIRST" if first else "completed SECOND", 12, _PALETTE["text"], 700, "variant_label")
    _text(commands, f"lane.{participant}.repeat", x + 14, y + 77, "run1 + run2 agree", 11, _PALETTE["muted"], 400, "variant_label")
    route_x = x + 34
    _line(commands, f"lane.{participant}.route", [(route_x, y + 96), (route_x, y + height - 28)], _PALETTE["purple"], 3, role="lane")
    _arrow(commands, f"lane.{participant}.route.arrow", route_x, y + height - 30, "down", _PALETTE["purple"], 8, "lane")
    _circle(commands, f"lane.{participant}.complete", route_x, y + 118, 9, _PALETTE["purple_fill"], _PALETTE["purple"], 2, "lane")
    _circle(commands, f"lane.{participant}.arrival", route_x, y + height - 42, 9, _PALETTE["gold_fill"], _PALETTE["gold"], 2, "lane")
    _text(commands, f"lane.{participant}.complete.label", route_x + 20, y + 123, f"complete {participant}", 11, _PALETTE["text"], role="lane")
    _text(commands, f"lane.{participant}.arrival.label", route_x + 20, y + height - 37, f"arrive {participant}", 11, _PALETTE["text"], role="lane")


def _common_footer(
    commands: list[dict[str, Any]], pad: int, y: int, inner: int, width: int
) -> int:
    boundary_h = 146
    _rect(commands, "region.boundary", pad, y, inner, boundary_h, _PALETTE["orange_fill"], _PALETTE["orange"], 2, role="boundary")
    _panel_title(
        commands,
        "boundary.heading",
        pad,
        y,
        inner,
        "EVIDENCE BOUNDARY · FRONTIER REMAINS",
        "positive profile only · incomplete/cancelled traces abstain",
        _PALETTE["orange"],
        _PALETTE["orange_fill"],
        "boundary",
    )
    lines = [
        "completion order: observed for retained runs",
        "fixed 0.050 s ticks · 8-tick deadline · no external service",
        "cancellation: negative evidence → Frontier / ABSTAINED",
    ]
    _text_lines(commands, "boundary.rows", pad + 14, y + 74, lines, 11 if width < 700 else 12, line_height=19)
    y += boundary_h + 14
    action_h = 54
    _rect(commands, "region.actions", pad, y, inner, action_h, _PALETTE["panel"], _PALETTE["line"], role="actions")
    labels = ["Select", "Proof", "All text", "Evidence", "Open source"]
    available = inner - 20
    gap = 6
    button_w = (available - gap * 4) // 5
    x = pad + 10
    for index, label in enumerate(labels):
        _rect(commands, f"action.{index}", x, y + 11, button_w, 32, _PALETTE["panel_alt"], _PALETTE["line"], 1, 6, "actions")
        display = label if width >= 480 or label not in {"Open source", "All text"} else ("Source" if label == "Open source" else "Text")
        _text(commands, f"action.{index}.label", x + button_w // 2, y + 32, display, 10, _PALETTE["muted"], 600, "actions", "middle")
        x += button_w + gap
    return y + action_h + 22


def _scene(
    condition_id: str,
    variant: str,
    width: int,
    height: int,
    commands: list[dict[str, Any]],
) -> dict[str, Any]:
    regions = []
    for command in commands:
        if command["kind"] == "rect" and command["element_id"].startswith("region."):
            regions.append(
                {
                    "element_id": command["element_id"],
                    "x": command["x"],
                    "y": command["y"],
                    "width": command["width"],
                    "height": command["height"],
                    "role": command["role"],
                }
            )
    return {
        "condition_id": condition_id,
        "variant": variant,
        "width": width,
        "height": int(height),
        "background": _PALETTE["canvas"],
        "commands": commands,
        "regions": regions,
    }


def build_scene(
    ledger: Mapping[str, Any], condition_id: str, variant: str, width: int
) -> dict[str, Any]:
    if condition_id == "LC4_ASYNC_COMPLETE_TEXT":
        return _complete_text_scene(ledger, variant, width)
    if condition_id == "LC4_ASYNC_CAUSAL_EVENT_REGISTER":
        return _event_register_scene(ledger, variant, width)
    if condition_id == "LC4_ASYNC_BARRIER_LANES":
        return _barrier_lanes_scene(ledger, variant, width)
    raise LC4AsyncVisualError(f"unknown condition: {condition_id}")


def _bounds_overlap(left: Mapping[str, Any], right: Mapping[str, Any]) -> bool:
    return not (
        left["x"] + left["width"] <= right["x"]
        or right["x"] + right["width"] <= left["x"]
        or left["y"] + left["height"] <= right["y"]
        or right["y"] + right["height"] <= left["y"]
    )


def _scene_checks(scene: Mapping[str, Any]) -> dict[str, Any]:
    width = scene["width"]
    height = scene["height"]
    regions = scene["regions"]
    containment = all(
        region["x"] >= 0
        and region["y"] >= 0
        and region["x"] + region["width"] <= width
        and region["y"] + region["height"] <= height
        for region in regions
    )
    overlaps = [
        (left["element_id"], right["element_id"])
        for index, left in enumerate(regions)
        for right in regions[index + 1 :]
        if _bounds_overlap(left, right)
    ]
    command_ids = {item["element_id"] for item in scene["commands"]}
    required = {
        "region.source",
        "region.proof",
        "region.boundary",
        "region.actions",
    }
    if scene["condition_id"] == "LC4_ASYNC_BARRIER_LANES":
        required |= {"region.barrier", "region.criterion"}
    return {
        "containment": containment,
        "major_region_overlap_count": len(overlaps),
        "missing_required_elements": sorted(required - command_ids),
        "pass": containment and not overlaps and required <= command_ids,
    }


def _structural_signature(scene: Mapping[str, Any]) -> list[dict[str, Any]]:
    roles = {"source", "portal", "lane", "proof", "barrier", "criterion", "boundary", "actions"}
    signature = []
    for command in scene["commands"]:
        if command.get("role") not in roles or command["kind"] == "text":
            continue
        stable = {key: value for key, value in command.items() if key not in {"fill", "stroke"}}
        signature.append(stable)
    return signature


def _svg(scene: Mapping[str, Any]) -> str:
    width = scene["width"]
    height = scene["height"]
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="LC4 async {_CONDITION_LABELS[scene["condition_id"]]} {scene["variant"]} {width}">',
        f'<rect width="{width}" height="{height}" fill="{scene["background"]}"/>',
        '<style>text{font-family:"Segoe UI",Arial,sans-serif}</style>',
    ]
    for command in scene["commands"]:
        kind = command["kind"]
        if kind == "rect":
            dash = ' stroke-dasharray="6 5"' if command.get("dashed") else ""
            out.append(
                f'<rect id="{html.escape(command["element_id"])}" x="{command["x"]}" y="{command["y"]}" width="{command["width"]}" height="{command["height"]}" rx="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"{dash}/>'
            )
        elif kind == "line":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            dash = ' stroke-dasharray="7 6"' if command["dashed"] else ""
            out.append(
                f'<polyline id="{html.escape(command["element_id"])}" points="{points}" fill="none" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}" stroke-linecap="round" stroke-linejoin="round"{dash}/>'
            )
        elif kind == "circle":
            out.append(
                f'<circle id="{html.escape(command["element_id"])}" cx="{command["cx"]}" cy="{command["cy"]}" r="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>'
            )
        elif kind == "polygon":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            out.append(
                f'<polygon id="{html.escape(command["element_id"])}" points="{points}" fill="{command["fill"]}"/>'
            )
        elif kind == "text":
            anchor = {"start": "start", "middle": "middle", "end": "end"}[command["anchor"]]
            out.append(
                f'<text id="{html.escape(command["element_id"])}" x="{command["x"]}" y="{command["y"]}" font-size="{command["size"]}" font-weight="{command["weight"]}" fill="{command["fill"]}" text-anchor="{anchor}">{html.escape(command["text"])}</text>'
            )
    out.append("</svg>")
    return "\n".join(out) + "\n"


_FONT_PATH = "C:/Windows/Fonts/segoeui.ttf"
_FONT_BOLD_PATH = "C:/Windows/Fonts/segoeuib.ttf"


def _font(size: int, weight: int) -> ImageFont.FreeTypeFont:
    path = _FONT_BOLD_PATH if weight >= 600 else _FONT_PATH
    return ImageFont.truetype(path, size=size)


def _render_png(scene: Mapping[str, Any], path: Path) -> None:
    image = Image.new("RGB", (scene["width"], scene["height"]), scene["background"])
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        kind = command["kind"]
        if kind == "rect":
            bounds = (
                command["x"],
                command["y"],
                command["x"] + command["width"],
                command["y"] + command["height"],
            )
            draw.rounded_rectangle(
                bounds,
                radius=command["radius"],
                fill=command["fill"],
                outline=command["stroke"] if command["stroke_width"] else None,
                width=command["stroke_width"],
            )
        elif kind == "line":
            if command["dashed"]:
                for (x1, y1), (x2, y2) in zip(command["points"], command["points"][1:]):
                    steps = max(abs(x2 - x1), abs(y2 - y1))
                    if steps == 0:
                        continue
                    for start in range(0, steps, 13):
                        end = min(start + 7, steps)
                        p1 = (x1 + (x2 - x1) * start / steps, y1 + (y2 - y1) * start / steps)
                        p2 = (x1 + (x2 - x1) * end / steps, y1 + (y2 - y1) * end / steps)
                        draw.line((p1, p2), fill=command["stroke"], width=command["stroke_width"])
            else:
                draw.line(command["points"], fill=command["stroke"], width=command["stroke_width"], joint="curve")
        elif kind == "circle":
            bounds = (
                command["cx"] - command["radius"],
                command["cy"] - command["radius"],
                command["cx"] + command["radius"],
                command["cy"] + command["radius"],
            )
            draw.ellipse(bounds, fill=command["fill"], outline=command["stroke"], width=command["stroke_width"])
        elif kind == "polygon":
            draw.polygon(command["points"], fill=command["fill"])
        elif kind == "text":
            anchor = {"start": "la", "middle": "ma", "end": "ra"}[command["anchor"]]
            draw.text(
                (command["x"], command["y"]),
                command["text"],
                fill=command["fill"],
                font=_font(command["size"], command["weight"]),
                anchor=anchor,
            )
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _build_review_board(
    output_dir: Path, manifest: Mapping[str, Any], variant: str, path: Path
) -> None:
    cell_w = 500
    cell_h = 610
    margin = 34
    header_h = 94
    board = Image.new("RGB", (margin * 2 + cell_w * 3, header_h + margin + cell_h * 3), _PALETTE["canvas"])
    draw = ImageDraw.Draw(board)
    draw.text((margin, 24), f"LC4 ASYNC · {variant.replace('_', ' ')} · RESPONSIVE REVIEW", fill=_PALETTE["text"], font=_font(25, 700))
    draw.text((margin, 61), "3 information-matched conditions × 430 / 480 / 700 logical px", fill=_PALETTE["muted"], font=_font(14, 400))
    for row, condition_id in enumerate(CONDITION_IDS):
        for column, width in enumerate(WIDTHS):
            state = next(
                item
                for item in manifest["states"]
                if item["condition_id"] == condition_id
                and item["variant"] == variant
                and item["width"] == width
            )
            image = Image.open(output_dir / state["effect_paths"]["png"]).convert("RGB")
            label_h = 48
            available_h = cell_h - label_h - 12
            ratio = min((cell_w - 18) / image.width, available_h / image.height)
            resized = image.resize((int(image.width * ratio), int(image.height * ratio)), Image.Resampling.LANCZOS)
            x0 = margin + column * cell_w
            y0 = header_h + row * cell_h
            draw.rounded_rectangle((x0 + 5, y0 + 5, x0 + cell_w - 5, y0 + cell_h - 5), radius=12, fill=_PALETTE["panel"], outline=_PALETTE["line"], width=2)
            draw.text((x0 + 18, y0 + 18), f"{_CONDITION_LABELS[condition_id]} · {width}", fill=_PALETTE["cyan"] if condition_id == "LC4_ASYNC_BARRIER_LANES" else _PALETTE["text"], font=_font(14, 700))
            px = x0 + (cell_w - resized.width) // 2
            py = y0 + label_h + (available_h - resized.height) // 2
            board.paste(resized, (px, py))
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _build_overview_board(output_dir: Path, path: Path) -> None:
    names = [
        "lc4-async-barrier-lanes-a-first-effect-700.png",
        "lc4-async-barrier-lanes-b-first-effect-700.png",
    ]
    images = [Image.open(output_dir / name).convert("RGB") for name in names]
    cell_w = 760
    header = 100
    margin = 30
    available_h = 1160
    board = Image.new("RGB", (margin * 2 + cell_w * 2, header + available_h + margin), _PALETTE["canvas"])
    draw = ImageDraw.Draw(board)
    draw.text((margin, 24), "LC4 ASYNC · BARRIER LANES · TRACE VARIANT CHECK", fill=_PALETTE["text"], font=_font(25, 700))
    draw.text((margin, 62), "Structure stays fixed; only observed completion labels change", fill=_PALETTE["muted"], font=_font(14, 400))
    for index, image in enumerate(images):
        ratio = min((cell_w - 24) / image.width, (available_h - 24) / image.height)
        resized = image.resize((int(image.width * ratio), int(image.height * ratio)), Image.Resampling.LANCZOS)
        x = margin + index * cell_w + (cell_w - resized.width) // 2
        y = header + (available_h - resized.height) // 2
        board.paste(resized, (x, y))
    board.save(path, format="PNG", optimize=False, compress_level=9)


def build_lc4_async_visual_artifacts(
    profile_path: str | Path,
    readiness_path: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Generate ledger, matched manifest, oracle, effects and review boards."""

    destination = Path(output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    ledger = build_accountable_ledger(profile_path, readiness_path)
    manifest = build_visual_manifest(ledger)
    ledger_path = destination / "lc4-async-accountable-ledger.v1.json"
    manifest_path = destination / "lc4-async-grammar-manifest.v1.json"
    _write_json(ledger_path, ledger)
    _write_json(manifest_path, manifest)

    scenes: dict[str, dict[str, Any]] = {}
    oracle_states: list[dict[str, Any]] = []
    for state in manifest["states"]:
        scene = build_scene(ledger, state["condition_id"], state["variant"], state["width"])
        scenes[state["state_id"]] = scene
        svg_path = destination / state["effect_paths"]["svg"]
        png_path = destination / state["effect_paths"]["png"]
        svg_path.write_text(_svg(scene), encoding="utf-8", newline="\n")
        _render_png(scene, png_path)
        oracle_states.append(
            {
                "state_id": state["state_id"],
                "condition_id": state["condition_id"],
                "variant": state["variant"],
                "width": state["width"],
                "canvas": [scene["width"], scene["height"]],
                "regions": deepcopy(scene["regions"]),
                "checks": _scene_checks(scene),
                "structural_signature_sha256": hashlib.sha256(
                    json.dumps(_structural_signature(scene), sort_keys=True).encode("utf-8")
                ).hexdigest(),
            }
        )
    if not all(item["checks"]["pass"] for item in oracle_states):
        raise LC4AsyncVisualError("one or more scene geometry checks failed")
    equality_checks = []
    for condition_id in CONDITION_IDS:
        for width in WIDTHS:
            a_id = f"{condition_id}__A_FIRST__W{width}"
            b_id = f"{condition_id}__B_FIRST__W{width}"
            matches = _structural_signature(scenes[a_id]) == _structural_signature(scenes[b_id])
            equality_checks.append(
                {
                    "condition_id": condition_id,
                    "width": width,
                    "a_b_structure_equal": matches,
                }
            )
            if not matches:
                raise LC4AsyncVisualError(f"A/B structure differs for {condition_id} at {width}")
    oracle = {
        "format": "blueprint-lens-lc4-async-geometry-oracle",
        "schema_version": "1.0.0",
        "status": "PASS__18_STATES__A_B_STRUCTURE_EQUAL",
        "coordinate_space": "top-left logical pixels at device scale 1",
        "states": oracle_states,
        "a_b_structural_equality": equality_checks,
        "non_claim": "design-target geometry only; not Slate or UE-visible evidence",
    }
    oracle_path = destination / "lc4-async-geometry-oracle.v1.json"
    _write_json(oracle_path, oracle)

    board_a = destination / "lc4-async-review-board-a-first.png"
    board_b = destination / "lc4-async-review-board-b-first.png"
    overview = destination / "lc4-async-review-board-barrier-variant-overview.png"
    _build_review_board(destination, manifest, "A_FIRST", board_a)
    _build_review_board(destination, manifest, "B_FIRST", board_b)
    _build_overview_board(destination, overview)

    hashed_paths = [ledger_path, manifest_path, oracle_path]
    hashed_paths.extend(
        destination / effect_path
        for state in manifest["states"]
        for effect_path in state["effect_paths"].values()
    )
    hashed_paths.extend((board_a, board_b, overview))
    hashes = {
        "format": "blueprint-lens-lc4-async-visual-hashes",
        "schema_version": "1.0.0",
        "status": "PASS__BYTE_INVENTORY_COMPLETE",
        "file_count": len(hashed_paths),
        "files": {
            path.name: {"sha256": _sha256(path), "bytes": path.stat().st_size}
            for path in sorted(hashed_paths, key=lambda item: item.name)
        },
    }
    hashes_path = destination / "lc4-async-visual-hashes.v1.json"
    _write_json(hashes_path, hashes)
    return {
        "ledger": ledger_path,
        "manifest": manifest_path,
        "oracle": oracle_path,
        "hashes": hashes_path,
        "review_board_a_first": board_a,
        "review_board_b_first": board_b,
        "review_board_overview": overview,
    }
