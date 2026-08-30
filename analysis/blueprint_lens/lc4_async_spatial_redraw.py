"""Deterministic spatial redraws for the frozen LC4-ASYNC A_FIRST projection."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import html
import json
from pathlib import Path
from typing import Any, Mapping

from PIL import Image, ImageDraw

from . import lc4_async_visual as visual


CONDITION_IDS = (
    "LC4_ASYNC_DUAL_LIFELINE_JOIN",
    "LC4_ASYNC_PARTIAL_ORDER_JOIN",
    "LC4_ASYNC_TOKEN_BARRIER_TRACKS",
)
SELECTED_CONDITION_ID = "LC4_ASYNC_PARTIAL_ORDER_JOIN"

_LABELS = {
    "LC4_ASYNC_DUAL_LIFELINE_JOIN": "Dual Lifeline Join",
    "LC4_ASYNC_PARTIAL_ORDER_JOIN": "Partial-Order Join",
    "LC4_ASYNC_TOKEN_BARRIER_TRACKS": "Token Barrier Tracks",
}

_SLUGS = {
    "LC4_ASYNC_DUAL_LIFELINE_JOIN": "dual-lifeline-join",
    "LC4_ASYNC_PARTIAL_ORDER_JOIN": "partial-order-join",
    "LC4_ASYNC_TOKEN_BARRIER_TRACKS": "token-barrier-tracks",
}

_P = {
    **visual._PALETTE,
    "canvas": "#11151b",
    "ink": "#f5f7fa",
    "dim": "#9ba8b4",
    "guide": "#43505c",
    "surface": "#1a2028",
    "cyan": "#54d6df",
    "violet": "#bda7ff",
    "gold": "#f3ca62",
    "green": "#76d49b",
    "orange": "#e9a568",
}


class LC4AsyncSpatialRedrawError(ValueError):
    """Raised when a redraw violates the accepted spatial comparison contract."""


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def load_accepted_ledger(
    profile_path: str | Path,
    readiness_path: str | Path,
) -> dict[str, Any]:
    """Load the already accepted ledger projection without deriving new truth."""

    ledger = visual.build_accountable_ledger(profile_path, readiness_path)
    visual.validate_accountable_ledger(ledger)
    return ledger


def information_set(ledger: Mapping[str, Any], variant: str) -> dict[str, Any]:
    """Return the exact information set held constant across redraw candidates."""

    matched = deepcopy(visual.information_set(ledger, variant))
    matched["boundary_ids"] = sorted(
        f'boundary:{item["boundary_kind"]}' for item in ledger["boundaries"]
    )
    return matched


def _effect_name(condition_id: str, suffix: str) -> str:
    return f"lc4-async-{_SLUGS[condition_id]}-a-first-effect-700.{suffix}"


def build_spatial_redraw_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Freeze the three-state 700/A_FIRST comparison contract."""

    visual.validate_accountable_ledger(ledger)
    matched = information_set(ledger, "A_FIRST")
    states = [
        {
            "state_id": f"{condition_id}__A_FIRST__W700",
            "condition_id": condition_id,
            "condition_label": _LABELS[condition_id],
            "variant": "A_FIRST",
            "width": 700,
            "information_set": deepcopy(matched),
            "effect_paths": {
                "svg": _effect_name(condition_id, "svg"),
                "png": _effect_name(condition_id, "png"),
            },
        }
        for condition_id in CONDITION_IDS
    ]
    manifest = {
        "format": "blueprint-lens-lc4-async-spatial-redraw-manifest",
        "schema_version": "1.0.0",
        "status": "INFORMATION_MATCHED__PARTIAL_ORDER_JOIN_OWNER_SELECTED",
        "question": ledger["question"],
        "profile_binding": deepcopy(ledger["profile_binding"]),
        "default_condition_id": None,
        "selected_condition_id": SELECTED_CONDITION_ID,
        "selection_status": "OWNER_SELECTED__IMPLEMENTATION_CONDITION",
        "conditions": [
            {
                "condition_id": condition_id,
                "label": _LABELS[condition_id],
                "role": (
                    "owner_selected_implementation_condition"
                    if condition_id == SELECTED_CONDITION_ID
                    else "retained_information_matched_alternative"
                ),
            }
            for condition_id in CONDITION_IDS
        ],
        "target_widths_logical_px": [700],
        "variants": ["A_FIRST"],
        "states": states,
        "matched": [
            "two retained invocations and sixteen event occurrences",
            "twenty-two typed evidence-bound relations",
            "two pairwise reachability plus completeness proofs",
            "four boundary facts, five actions and identical source identities",
        ],
        "controlled_difference": (
            "dual observed-order lifelines versus transitive-reduction partial-order DAG "
            "versus static retained-evidence token tracks"
        ),
        "non_claims": [
            "owner selection is an implementation condition, not a product default",
            "effect images are not Slate or UE-visible evidence",
            "no comprehension, preference or scalability result is established",
            "observed completion order is never promoted to causality",
        ],
    }
    validate_spatial_redraw_manifest(manifest, ledger)
    return manifest


def validate_spatial_redraw_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    states = manifest.get("states", [])
    expected = {(condition_id, "A_FIRST", 700) for condition_id in CONDITION_IDS}
    actual = {(item["condition_id"], item["variant"], item["width"]) for item in states}
    if len(states) != 3 or actual != expected:
        raise LC4AsyncSpatialRedrawError("redraw manifest must contain exactly three 700/A_FIRST states")
    if manifest.get("default_condition_id") is not None:
        raise LC4AsyncSpatialRedrawError("owner selection must not become a product default")
    if manifest.get("selected_condition_id") != SELECTED_CONDITION_ID:
        raise LC4AsyncSpatialRedrawError("Partial-Order Join must remain owner-selected")
    if manifest.get("selection_status") != "OWNER_SELECTED__IMPLEMENTATION_CONDITION":
        raise LC4AsyncSpatialRedrawError("owner-selection status differs")
    roles = {item["condition_id"]: item["role"] for item in manifest.get("conditions", [])}
    expected_roles = {
        condition_id: (
            "owner_selected_implementation_condition"
            if condition_id == SELECTED_CONDITION_ID
            else "retained_information_matched_alternative"
        )
        for condition_id in CONDITION_IDS
    }
    if roles != expected_roles:
        raise LC4AsyncSpatialRedrawError("condition roles differ from the owner selection")
    paths = [path for item in states for path in item["effect_paths"].values()]
    if len(paths) != 6 or len(set(paths)) != 6:
        raise LC4AsyncSpatialRedrawError("redraw effect paths must be unique")
    expected_information = information_set(ledger, "A_FIRST")
    if any(item["information_set"] != expected_information for item in states):
        raise LC4AsyncSpatialRedrawError("redraw candidates are not information matched")


def _rect(
    commands: list[dict[str, Any]],
    element_id: str,
    x: int,
    y: int,
    width: int,
    height: int,
    fill: str,
    stroke: str = _P["guide"],
    stroke_width: int = 1,
    radius: int = 8,
    role: str = "detail",
    *,
    full_enclosure: bool = False,
) -> None:
    visual._rect(
        commands,
        element_id,
        x,
        y,
        width,
        height,
        fill,
        stroke,
        stroke_width,
        radius,
        role,
    )
    commands[-1]["full_enclosure"] = full_enclosure


def _line(
    commands: list[dict[str, Any]],
    element_id: str,
    points: list[tuple[int, int]],
    stroke: str,
    stroke_width: int = 2,
    dashed: bool = False,
    role: str = "detail",
) -> None:
    visual._line(commands, element_id, points, stroke, stroke_width, dashed, role)


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
    visual._circle(commands, element_id, cx, cy, radius, fill, stroke, stroke_width, role)


def _arrow(
    commands: list[dict[str, Any]],
    element_id: str,
    x: int,
    y: int,
    direction: str,
    fill: str,
    size: int = 8,
    role: str = "detail",
) -> None:
    visual._arrow(commands, element_id, x, y, direction, fill, size, role)


def _text(
    commands: list[dict[str, Any]],
    element_id: str,
    x: int,
    y: int,
    text: str,
    size: int = 13,
    fill: str = _P["ink"],
    weight: int = 400,
    role: str = "label",
    anchor: str = "start",
) -> None:
    visual._text(commands, element_id, x, y, text, size, fill, weight, role, anchor)


def _header(commands: list[dict[str, Any]], condition_id: str) -> None:
    _text(commands, "header.eyebrow", 28, 28, "LC4 · ASYNC SPATIAL REDRAW", 11, _P["cyan"], 700)
    _text(commands, "header.title", 28, 62, _LABELS[condition_id], 25, _P["ink"], 700)
    _text(commands, "header.variant", 672, 28, "A_FIRST · 700", 11, _P["violet"], 700, anchor="end")
    _text(
        commands,
        "header.question",
        28,
        96,
        "Why only after both complete — and can either finish first?",
        13,
        _P["dim"],
    )
    _text(
        commands,
        "header.counts",
        28,
        124,
        "2 retained invocations · 16 events · 22 relations · 2 proofs · 4 boundaries",
        11,
        _P["dim"],
        600,
    )
    _line(commands, "header.rule", [(28, 144), (672, 144)], _P["guide"], 1)


def _source_launch(commands: list[dict[str, Any]], y: int = 210) -> None:
    _text(commands, "source.caption", 28, y - 32, "SOURCE-GUARANTEED LAUNCH ORDER", 10, _P["cyan"], 700, "source")
    _circle(commands, "source.sequence", 84, y, 8, _P["surface"], _P["cyan"], 2, "source")
    _text(commands, "source.sequence.label", 84, y + 29, "Sequence", 11, _P["dim"], 600, "source", "middle")
    _line(commands, "source.launch.route", [(92, y), (265, y), (435, y)], _P["cyan"], 3, role="source")
    _arrow(commands, "source.launch.route.arrow", 435, y, "right", _P["cyan"], 9, "source")
    _circle(commands, "source.launch.A", 265, y, 10, _P["canvas"], _P["cyan"], 3, "source")
    _circle(commands, "source.launch.B", 435, y, 10, _P["canvas"], _P["cyan"], 3, "source")
    _text(commands, "source.launch.A.label", 265, y - 18, "launch A", 12, _P["ink"], 700, "source", "middle")
    _text(commands, "source.launch.B.label", 435, y - 18, "launch B", 12, _P["ink"], 700, "source", "middle")
    _text(commands, "source.launch.order", 350, y + 34, "launch A → B", 12, _P["cyan"], 700, "source", "middle")


def _proof_ribbon(commands: list[dict[str, Any]], x: int, y: int, orientation: str) -> None:
    if orientation == "vertical":
        _line(commands, "proof.bracket", [(x, y), (x + 10, y), (x + 10, y + 116), (x, y + 116)], _P["violet"], 2, role="proof")
        labels = ["A ↛ B", "B ↛ A", "relation set complete", "therefore A ∥ B"]
        for index, label in enumerate(labels):
            _text(commands, f"proof.label.{index}", x + 24, y + 18 + index * 28, label, 12 if index < 3 else 14, _P["violet"] if index == 3 else _P["ink"], 700 if index == 3 else 500, "proof")
    else:
        _line(commands, "proof.bracket", [(x, y), (x, y + 9), (x + 502, y + 9), (x + 502, y)], _P["violet"], 2, role="proof")
        labels = ["A ↛ B", "B ↛ A", "relation set complete", "therefore A ∥ B"]
        positions = [x, x + 98, x + 196, x + 382]
        for index, (label, px) in enumerate(zip(labels, positions)):
            _text(commands, f"proof.label.{index}", px, y + 31, label, 11 if index < 3 else 13, _P["violet"] if index == 3 else _P["ink"], 700 if index == 3 else 500, "proof")


def _horizontal_barrier(
    commands: list[dict[str, Any]], y: int, left: int = 176, right: int = 524
) -> None:
    _text(commands, "barrier.caption", 28, y - 22, "EXPLICIT PROJECT-OWNED JOIN", 10, _P["gold"], 700, "barrier")
    _rect(
        commands,
        "barrier.and",
        left,
        y,
        right - left,
        24,
        _P["gold"],
        _P["gold"],
        0,
        2,
        "barrier",
        full_enclosure=True,
    )
    _circle(commands, "barrier.socket.A", 246, y, 7, _P["canvas"], _P["gold"], 2, "barrier")
    _circle(commands, "barrier.socket.B", 454, y, 7, _P["canvas"], _P["gold"], 2, "barrier")
    _text(commands, "barrier.socket.A.label", 246, y - 12, "socket A", 10, _P["gold"], 700, "barrier", "middle")
    _text(commands, "barrier.socket.B.label", 454, y - 12, "socket B", 10, _P["gold"], 700, "barrier", "middle")
    _text(commands, "barrier.label", 350, y + 17, "AND · 2/2 ARRIVED", 11, _P["canvas"], 700, "barrier", "middle")
    _line(commands, "barrier.release.route", [(350, y + 24), (350, y + 72)], _P["green"], 4, role="barrier")
    _arrow(commands, "barrier.release.arrow", 350, y + 72, "down", _P["green"], 10, "barrier")
    _text(commands, "barrier.release.label", 366, y + 55, "RELEASE ONCE (1)", 11, _P["green"], 700, "barrier")


def _criterion(commands: list[dict[str, Any]], y: int) -> None:
    _rect(
        commands,
        "criterion.dock",
        210,
        y,
        280,
        62,
        "#193126",
        _P["green"],
        2,
        8,
        "criterion",
        full_enclosure=True,
    )
    _text(commands, "criterion.eyebrow", 350, y + 20, "CRITERION · AFTER RELEASE", 10, _P["green"], 700, "criterion", "middle")
    _text(commands, "criterion.label", 350, y + 44, "Set LC4AsyncComplete = true", 14, _P["ink"], 700, "criterion", "middle")


def _footer(commands: list[dict[str, Any]], frontier_y: int, actions_y: int) -> int:
    _rect(
        commands,
        "frontier.strip",
        28,
        frontier_y,
        644,
        88,
        "#2c241d",
        _P["orange"],
        2,
        6,
        "frontier",
        full_enclosure=True,
    )
    _text(commands, "frontier.title", 46, frontier_y + 25, "FRONTIER · BOUNDED POSITIVE PROFILE", 11, _P["orange"], 700, "frontier")
    _text(commands, "frontier.row.0", 46, frontier_y + 49, "observed order only · fixed 0.050 s ticks · eight-tick deadline", 11, _P["ink"], role="frontier")
    _text(commands, "frontier.row.1", 46, frontier_y + 70, "no external service · cancelled/incomplete → ABSTAINED", 11, _P["dim"], role="frontier")

    _rect(
        commands,
        "actions.dock",
        28,
        actions_y,
        644,
        56,
        _P["surface"],
        _P["guide"],
        1,
        8,
        "actions",
        full_enclosure=True,
    )
    labels = ("Select", "Proof", "All text", "Evidence", "Open source")
    x = 48
    for index, label in enumerate(labels):
        width = (96, 92, 104, 108, 126)[index]
        _line(commands, f"action.{index}.underline", [(x, actions_y + 40), (x + width - 14, actions_y + 40)], _P["guide"], 1, role="actions")
        _text(commands, f"action.{index}.label", x, actions_y + 31, label, 11, _P["dim"], 600, "actions")
        x += width + 18
    return actions_y + 78


def _dual_lifeline_scene() -> dict[str, Any]:
    commands: list[dict[str, Any]] = []
    _header(commands, "LC4_ASYNC_DUAL_LIFELINE_JOIN")
    _source_launch(commands)
    _text(commands, "axis.observed_trace_order", 42, 328, "OBSERVED TRACE ORDER ↓", 10, _P["violet"], 700, "lifeline")
    _line(commands, "axis.observed_trace_order.guide", [(62, 344), (62, 592)], _P["guide"], 1, True, "lifeline")
    _arrow(commands, "axis.observed_trace_order.arrow", 62, 592, "down", _P["guide"], 8, "lifeline")

    for participant, x in (("A", 246), ("B", 454)):
        _text(commands, f"lifeline.{participant}.title", x, 286, f"participant {participant}", 13, _P["ink"], 700, "lifeline", "middle")
        _line(commands, f"lifeline.{participant}", [(x, 305), (x, 610)], _P["violet"], 2, True, "lifeline")
        _circle(commands, f"lifeline.{participant}.launch", x, 326, 8, _P["canvas"], _P["cyan"], 2, "lifeline")
        _line(commands, f"lifeline.{participant}.boundary", [(x - 54, 376), (x + 54, 376)], _P["orange"], 2, True, "lifeline")
        _text(commands, f"lifeline.{participant}.boundary.label", x, 365, "CONTINUATION BOUNDARY", 9, _P["orange"], 700, "lifeline", "middle")

    _line(commands, "lifeline.launch.message", [(246, 326), (454, 326)], _P["cyan"], 2, role="lifeline")
    _arrow(commands, "lifeline.launch.message.arrow", 454, 326, "right", _P["cyan"], 8, "lifeline")
    _text(commands, "lifeline.launch.message.label", 350, 316, "typed launch A → B", 10, _P["cyan"], 700, "lifeline", "middle")

    for participant, x, complete_y, arrival_y in (("A", 246, 438, 492), ("B", 454, 510, 564)):
        _circle(commands, f"lifeline.{participant}.complete", x, complete_y, 10, _P["canvas"], _P["violet"], 3, "lifeline")
        _text(commands, f"lifeline.{participant}.complete.label", x + 18, complete_y + 4, f"complete {participant}", 11, _P["ink"], 600, "lifeline")
        _circle(commands, f"lifeline.{participant}.arrival", x, arrival_y, 9, _P["canvas"], _P["gold"], 3, "lifeline")
        _text(commands, f"lifeline.{participant}.arrival.label", x + 18, arrival_y + 4, f"arrive {participant}", 11, _P["gold"], 600, "lifeline")
        _line(commands, f"lifeline.{participant}.to_socket", [(x, arrival_y + 9), (x, 688)], _P["gold"], 2, role="lifeline")
        _arrow(commands, f"lifeline.{participant}.to_socket.arrow", x, 688, "down", _P["gold"], 8, "lifeline")

    _proof_ribbon(commands, 88, 596, "horizontal")
    _horizontal_barrier(commands, 700)
    _criterion(commands, 794)
    height = _footer(commands, 876, 978)
    return _scene("LC4_ASYNC_DUAL_LIFELINE_JOIN", commands, height)


def _dag_node(
    commands: list[dict[str, Any]], element_id: str, x: int, y: int, label: str, accent: str
) -> None:
    _circle(commands, element_id, x, y, 12, _P["canvas"], accent, 3, "dag")
    _text(commands, f"{element_id}.label", x, y - 20, label, 10, _P["ink"], 700, "dag", "middle")


def _dag_edge(
    commands: list[dict[str, Any]], element_id: str, points: list[tuple[int, int]], stroke: str
) -> None:
    _line(commands, element_id, points, stroke, 2, role="dag")
    x, y = points[-1]
    _arrow(commands, f"{element_id}.arrow", x, y, "down", stroke, 7, "dag")


def _partial_order_scene() -> dict[str, Any]:
    commands: list[dict[str, Any]] = []
    _header(commands, "LC4_ASYNC_PARTIAL_ORDER_JOIN")
    _text(commands, "dag.transitive_reduction", 28, 160, "TRANSITIVE-REDUCTION DAG · source/profile relations only", 10, _P["cyan"], 700, "dag")
    _source_launch(commands, 220)

    _dag_edge(commands, "dag.edge.launch_order", [(265, 230), (265, 270), (435, 270), (435, 300)], _P["cyan"])
    for participant, x in (("A", 232), ("B", 468)):
        _dag_node(commands, f"dag.continuation.{participant}", x, 354, f"{participant} · CONTINUATION BOUNDARY", _P["orange"])
        source_x = 265 if participant == "A" else 435
        _dag_edge(commands, f"dag.edge.launch_to_continuation.{participant}", [(source_x, 230), (x, 342)], _P["orange"])
        _dag_node(commands, f"dag.completion.{participant}", x, 470, f"complete {participant}", _P["violet"])
        _dag_edge(commands, f"dag.edge.continuation_to_completion.{participant}", [(x, 366), (x, 458)], _P["violet"])
        _dag_node(commands, f"dag.arrival.{participant}", x, 576, f"arrive {participant}", _P["gold"])
        _dag_edge(commands, f"dag.edge.completion_to_arrival.{participant}", [(x, 482), (x, 564)], _P["gold"])
        _dag_edge(commands, f"dag.edge.arrival_to_socket.{participant}", [(x, 588), (x, 638)], _P["gold"])

    _line(commands, "dag.rank.completions", [(176, 470), (524, 470)], _P["guide"], 1, True, "dag")
    _text(commands, "dag.rank.completions.label", 350, 410, "same partial-order rank · placement does not assert precedence", 10, _P["dim"], 600, "dag", "middle")
    _proof_ribbon(commands, 526, 394, "vertical")
    _line(commands, "dag.proof.bracket", [(526, 394), (536, 394), (536, 510), (526, 510)], _P["violet"], 2, role="dag")

    _horizontal_barrier(commands, 650)
    _criterion(commands, 744)
    height = _footer(commands, 826, 928)
    return _scene("LC4_ASYNC_PARTIAL_ORDER_JOIN", commands, height)


def _token_tracks_scene() -> dict[str, Any]:
    commands: list[dict[str, Any]] = []
    _header(commands, "LC4_ASYNC_TOKEN_BARRIER_TRACKS")
    _source_launch(commands)
    _text(commands, "token.static.caption", 28, 274, "STATIC RETAINED-RUN EVIDENCE · not live state or animation", 10, _P["violet"], 700, "token")

    barrier_x = 550
    track_start = 150
    _rect(commands, "barrier.and", barrier_x, 348, 24, 220, _P["gold"], _P["gold"], 0, 2, "barrier", full_enclosure=True)
    for participant, y, source_x in (("A", 390, 265), ("B", 526, 435)):
        _line(commands, f"track.{participant}", [(track_start, y), (barrier_x, y)], _P["violet"], 4, role="token")
        _line(commands, f"track.{participant}.source_route", [(source_x, 220), (source_x, y - 42), (track_start, y - 42), (track_start, y)], _P["cyan"], 2, role="token")
        _arrow(commands, f"track.{participant}.source_route.arrow", track_start, y, "down", _P["cyan"], 8, "token")
        _text(commands, f"track.{participant}.label", 48, y + 5, f"participant {participant}", 12, _P["ink"], 700, "token")
        _line(commands, f"track.{participant}.boundary", [(210, y - 28), (210, y + 28)], _P["orange"], 2, True, "token")
        _text(commands, f"track.{participant}.boundary.label", 222, y + 34, "CONTINUATION BOUNDARY", 9, _P["orange"], 700, "token")
        _circle(commands, f"token.{participant}.complete", 350, y, 11, _P["canvas"], _P["violet"], 3, "token")
        _text(commands, f"token.{participant}.complete.label", 350, y - 19, f"complete {participant}", 10, _P["ink"], 600, "token", "middle")
        _circle(commands, f"token.{participant}.retained_evidence", barrier_x - 18, y, 13, _P["gold"], _P["canvas"], 3, "token")
        _text(commands, f"token.{participant}.retained_evidence.label", barrier_x - 18, y + 4, participant, 10, _P["canvas"], 700, "token", "middle")
        _text(commands, f"barrier.socket.{participant}.label", barrier_x + 34, y - 13, f"socket {participant}", 10, _P["gold"], 700, "barrier")

    _text(commands, "barrier.and.label", barrier_x + 12, 596, "AND · 2/2 ARRIVED", 11, _P["gold"], 700, "barrier", "middle")
    _circle(commands, "token.release.single", 622, 458, 15, _P["green"], _P["canvas"], 3, "token")
    _text(commands, "token.release.single.label", 622, 462, "1", 11, _P["canvas"], 700, "token", "middle")
    _line(commands, "barrier.release.route", [(574, 458), (607, 458)], _P["green"], 4, role="barrier")
    _arrow(commands, "barrier.release.route.arrow", 607, 458, "right", _P["green"], 9, "barrier")
    _text(commands, "barrier.release.label", 608, 438, "RELEASE ONCE (1)", 10, _P["green"], 700, "barrier", "middle")
    _line(commands, "token.release.to_criterion", [(622, 473), (622, 684), (350, 684), (350, 724)], _P["green"], 3, role="token")
    _arrow(commands, "token.release.to_criterion.arrow", 350, 724, "down", _P["green"], 9, "token")

    _proof_ribbon(commands, 70, 612, "horizontal")
    _criterion(commands, 736)
    height = _footer(commands, 818, 920)
    return _scene("LC4_ASYNC_TOKEN_BARRIER_TRACKS", commands, height)


def _scene(condition_id: str, commands: list[dict[str, Any]], height: int) -> dict[str, Any]:
    return {
        "condition_id": condition_id,
        "variant": "A_FIRST",
        "width": 700,
        "height": height,
        "background": _P["canvas"],
        "commands": commands,
        "regions": [
            {
                "element_id": item["element_id"],
                "x": item["x"],
                "y": item["y"],
                "width": item["width"],
                "height": item["height"],
                "role": item["role"],
            }
            for item in commands
            if item["kind"] == "rect" and item.get("full_enclosure", False)
        ],
    }


def build_spatial_scene(
    ledger: Mapping[str, Any], condition_id: str
) -> dict[str, Any]:
    """Build one open-canvas candidate over the frozen A_FIRST information set."""

    visual.validate_accountable_ledger(ledger)
    builders = {
        "LC4_ASYNC_DUAL_LIFELINE_JOIN": _dual_lifeline_scene,
        "LC4_ASYNC_PARTIAL_ORDER_JOIN": _partial_order_scene,
        "LC4_ASYNC_TOKEN_BARRIER_TRACKS": _token_tracks_scene,
    }
    if condition_id not in builders:
        raise LC4AsyncSpatialRedrawError(f"unknown redraw condition: {condition_id}")
    scene = builders[condition_id]()
    scene["information_set"] = information_set(ledger, "A_FIRST")
    checks = validate_spatial_scene(scene)
    if not checks["pass"]:
        raise LC4AsyncSpatialRedrawError(f"scene checks failed for {condition_id}: {checks}")
    return scene


def relation_primitive_signature(scene: Mapping[str, Any]) -> str:
    prefixes = {
        "LC4_ASYNC_DUAL_LIFELINE_JOIN": ("lifeline.", "axis.observed_trace_order"),
        "LC4_ASYNC_PARTIAL_ORDER_JOIN": ("dag.",),
        "LC4_ASYNC_TOKEN_BARRIER_TRACKS": ("track.", "token."),
    }[scene["condition_id"]]
    ids = sorted(
        item["element_id"]
        for item in scene["commands"]
        if item["element_id"].startswith(prefixes)
    )
    return hashlib.sha256("\n".join(ids).encode("utf-8")).hexdigest()


def validate_spatial_scene(scene: Mapping[str, Any]) -> dict[str, Any]:
    command_ids = {item["element_id"] for item in scene["commands"]}
    universal = {
        "source.launch.order",
        "proof.label.0",
        "proof.label.1",
        "proof.label.2",
        "proof.label.3",
        "barrier.and",
        "barrier.socket.A.label",
        "barrier.socket.B.label",
        "barrier.release.label",
        "criterion.dock",
        "frontier.strip",
        "actions.dock",
    }
    unique = {
        "LC4_ASYNC_DUAL_LIFELINE_JOIN": {"lifeline.A", "lifeline.B", "lifeline.launch.message"},
        "LC4_ASYNC_PARTIAL_ORDER_JOIN": {"dag.edge.launch_order", "dag.rank.completions", "dag.proof.bracket"},
        "LC4_ASYNC_TOKEN_BARRIER_TRACKS": {"track.A", "track.B", "token.release.single"},
    }[scene["condition_id"]]
    enclosures = [
        item
        for item in scene["commands"]
        if item["kind"] == "rect" and item.get("full_enclosure", False)
    ]
    enclosure_roles = {item["role"] for item in enclosures}
    allowed_enclosure_roles = {"criterion", "barrier", "frontier", "actions"}
    points: list[tuple[int, int]] = []
    for item in scene["commands"]:
        if item["kind"] == "line" or item["kind"] == "polygon":
            points.extend(item["points"])
        elif item["kind"] == "circle":
            points.extend(
                (
                    (item["cx"] - item["radius"], item["cy"] - item["radius"]),
                    (item["cx"] + item["radius"], item["cy"] + item["radius"]),
                )
            )
        elif item["kind"] == "rect":
            points.extend(((item["x"], item["y"]), (item["x"] + item["width"], item["y"] + item["height"])))
        elif item["kind"] == "text":
            points.append((item["x"], item["y"]))
    contained = all(0 <= x <= scene["width"] and 0 <= y <= scene["height"] for x, y in points)
    missing = sorted((universal | unique) - command_ids)
    budget_ok = len(enclosures) <= 4 and enclosure_roles <= allowed_enclosure_roles
    return {
        "canvas_containment": contained,
        "full_enclosure_count": len(enclosures),
        "full_enclosure_roles": sorted(enclosure_roles),
        "enclosure_budget_pass": budget_ok,
        "missing_required_elements": missing,
        "relation_primitive_signature": relation_primitive_signature(scene),
        "pass": contained and budget_ok and not missing,
    }


def _svg(scene: Mapping[str, Any]) -> str:
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{scene["width"]}" height="{scene["height"]}" viewBox="0 0 {scene["width"]} {scene["height"]}" role="img" aria-label="LC4 async {_LABELS[scene["condition_id"]]} {scene["variant"].replace("_", " ")} {scene["width"]}">',
        f'<rect width="{scene["width"]}" height="{scene["height"]}" fill="{scene["background"]}"/>',
        '<style>text{font-family:"Segoe UI",Arial,sans-serif}</style>',
    ]
    for command in scene["commands"]:
        kind = command["kind"]
        element_id = html.escape(command["element_id"])
        if kind == "rect":
            out.append(
                f'<rect id="{element_id}" x="{command["x"]}" y="{command["y"]}" width="{command["width"]}" height="{command["height"]}" rx="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>'
            )
        elif kind == "line":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            dash = ' stroke-dasharray="7 6"' if command["dashed"] else ""
            out.append(
                f'<polyline id="{element_id}" points="{points}" fill="none" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}" stroke-linecap="round" stroke-linejoin="round"{dash}/>'
            )
        elif kind == "circle":
            out.append(
                f'<circle id="{element_id}" cx="{command["cx"]}" cy="{command["cy"]}" r="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>'
            )
        elif kind == "polygon":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            out.append(f'<polygon id="{element_id}" points="{points}" fill="{command["fill"]}"/>')
        elif kind == "text":
            anchor = {"start": "start", "middle": "middle", "end": "end"}[command["anchor"]]
            out.append(
                f'<text id="{element_id}" x="{command["x"]}" y="{command["y"]}" font-size="{command["size"]}" font-weight="{command["weight"]}" fill="{command["fill"]}" text-anchor="{anchor}">{html.escape(command["text"])}</text>'
            )
    out.append("</svg>")
    return "\n".join(out) + "\n"


def _build_comparison_board(output_dir: Path, states: list[Mapping[str, Any]], path: Path) -> None:
    images = [Image.open(output_dir / item["effect_paths"]["png"]).convert("RGB") for item in states]
    margin = 32
    gap = 24
    cell_w = 700
    header_h = 116
    body_h = max(image.height for image in images)
    board = Image.new("RGB", (margin * 2 + cell_w * 3 + gap * 2, header_h + body_h + margin), _P["canvas"])
    draw = ImageDraw.Draw(board)
    draw.text((margin, 24), "LC4 ASYNC · THREE SPATIAL CANDIDATES · A_FIRST / 700", fill=_P["ink"], font=visual._font(25, 700))
    draw.text((margin, 62), "Partial-Order Join owner-selected for implementation · no product default established", fill=_P["dim"], font=visual._font(14, 400))
    for index, (state, image) in enumerate(zip(states, images)):
        x = margin + index * (cell_w + gap)
        draw.text((x, 92), f"{index + 1} · {state['condition_label']}", fill=_P["cyan"] if index == 1 else _P["ink"], font=visual._font(14, 700))
        board.paste(image, (x, header_h))
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _legacy_lock() -> dict[str, Any]:
    root = Path(__file__).resolve().parents[2]
    legacy_dir = root / "artifacts/r1/lc4-async-visual-candidates"
    inventory_path = legacy_dir / "lc4-async-visual-hashes.v1.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    for name, record in inventory["files"].items():
        path = legacy_dir / name
        if not path.is_file() or _sha256(path) != record["sha256"]:
            raise LC4AsyncSpatialRedrawError(f"legacy v1 visual artifact changed: {name}")
    return {
        "format": "blueprint-lens-lc4-async-spatial-redraw-legacy-lock",
        "schema_version": "1.0.0",
        "status": "PASS__LEGACY_V1_PACKET_BYTE_PRESERVED",
        "legacy_inventory_path": "artifacts/r1/lc4-async-visual-candidates/lc4-async-visual-hashes.v1.json",
        "legacy_inventory_sha256": _sha256(inventory_path),
        "legacy_file_count": inventory["file_count"],
    }


def build_lc4_async_spatial_redraw_artifacts(
    profile_path: str | Path,
    readiness_path: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Publish only the three redraw states, oracle, board and integrity records."""

    destination = Path(output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    legacy_lock = _legacy_lock()
    ledger = load_accepted_ledger(profile_path, readiness_path)
    manifest = build_spatial_redraw_manifest(ledger)

    manifest_path = destination / "lc4-async-spatial-redraw-manifest.v1.json"
    oracle_path = destination / "lc4-async-spatial-redraw-geometry-oracle.v1.json"
    legacy_lock_path = destination / "lc4-async-spatial-redraw-legacy-v1-lock.json"
    board_path = destination / "lc4-async-spatial-redraw-a-first-700-board.png"
    hashes_path = destination / "lc4-async-spatial-redraw-hashes.v1.json"
    _write_json(manifest_path, manifest)
    _write_json(legacy_lock_path, legacy_lock)

    oracle_states = []
    effect_paths: list[Path] = []
    for state in manifest["states"]:
        scene = build_spatial_scene(ledger, state["condition_id"])
        svg_path = destination / state["effect_paths"]["svg"]
        png_path = destination / state["effect_paths"]["png"]
        svg_path.write_text(_svg(scene), encoding="utf-8", newline="\n")
        visual._render_png(scene, png_path)
        checks = validate_spatial_scene(scene)
        oracle_states.append(
            {
                "state_id": state["state_id"],
                "condition_id": state["condition_id"],
                "variant": "A_FIRST",
                "width": 700,
                "canvas": [scene["width"], scene["height"]],
                "full_enclosures": deepcopy(scene["regions"]),
                "checks": checks,
            }
        )
        effect_paths.extend((svg_path, png_path))
    oracle = {
        "format": "blueprint-lens-lc4-async-spatial-redraw-geometry-oracle",
        "schema_version": "1.0.0",
        "status": "PASS__THREE_OPEN_CANVAS_STATES",
        "coordinate_space": "top-left logical pixels at device scale 1",
        "states": oracle_states,
        "non_claim": "authoring-side design effects only; not Slate or UE-visible evidence",
    }
    _write_json(oracle_path, oracle)
    _build_comparison_board(destination, manifest["states"], board_path)

    hashed_paths = [manifest_path, oracle_path, legacy_lock_path, board_path, *effect_paths]
    inventory = {
        "format": "blueprint-lens-lc4-async-spatial-redraw-hashes",
        "schema_version": "1.0.0",
        "status": "PASS__TEN_FILE_REDRAW_INVENTORY_COMPLETE",
        "file_count": len(hashed_paths),
        "files": {
            path.name: {"sha256": _sha256(path), "bytes": path.stat().st_size}
            for path in sorted(hashed_paths, key=lambda item: item.name)
        },
    }
    _write_json(hashes_path, inventory)
    _legacy_lock()
    return {
        "manifest": manifest_path,
        "oracle": oracle_path,
        "legacy_lock": legacy_lock_path,
        "hashes": hashes_path,
        "comparison_board": board_path,
    }


SELECTED_WIDTHS = (430, 480, 700)
SELECTED_VARIANTS = ("A_FIRST", "B_FIRST")


def _selected_effect_name(variant: str, width: int, suffix: str) -> str:
    variant_slug = variant.lower().replace("_", "-")
    return (
        "lc4-async-partial-order-join-selected-"
        f"{variant_slug}-effect-{width}.{suffix}"
    )


def build_selected_responsive_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Freeze the selected 3-width x 2-variant effect-image contract."""

    visual.validate_accountable_ledger(ledger)
    states = []
    for variant in SELECTED_VARIANTS:
        matched = information_set(ledger, variant)
        for width in SELECTED_WIDTHS:
            states.append(
                {
                    "state_id": f"{SELECTED_CONDITION_ID}__{variant}__W{width}",
                    "condition_id": SELECTED_CONDITION_ID,
                    "condition_label": _LABELS[SELECTED_CONDITION_ID],
                    "variant": variant,
                    "width": width,
                    "information_set": deepcopy(matched),
                    "effect_paths": {
                        "svg": _selected_effect_name(variant, width, "svg"),
                        "png": _selected_effect_name(variant, width, "png"),
                    },
                }
            )
    manifest = {
        "format": "blueprint-lens-lc4-async-selected-responsive-manifest",
        "schema_version": "1.0.0",
        "status": "PARTIAL_ORDER_JOIN__SIX_EFFECT_STATES__OWNER_REVIEW_PENDING",
        "question": ledger["question"],
        "profile_binding": deepcopy(ledger["profile_binding"]),
        "selected_condition_id": SELECTED_CONDITION_ID,
        "selection_status": "OWNER_SELECTED__IMPLEMENTATION_CONDITION",
        "default_condition_id": None,
        "target_widths_logical_px": list(SELECTED_WIDTHS),
        "variants": list(SELECTED_VARIANTS),
        "states": states,
        "variant_rule": (
            "A_FIRST/B_FIRST may change retained observation text and evidence identities only; "
            "causal geometry is equal at each width"
        ),
        "non_claims": [
            "owner selection is not a product default",
            "responsive effects are not Slate or UE-visible evidence",
            "no comprehension, preference or scalability result is established",
        ],
    }
    validate_selected_responsive_manifest(manifest, ledger)
    return manifest


def validate_selected_responsive_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    states = manifest.get("states", [])
    expected = {
        (SELECTED_CONDITION_ID, variant, width)
        for variant in SELECTED_VARIANTS
        for width in SELECTED_WIDTHS
    }
    actual = {
        (item["condition_id"], item["variant"], item["width"]) for item in states
    }
    if len(states) != 6 or actual != expected:
        raise LC4AsyncSpatialRedrawError(
            "selected manifest must contain exactly six Partial-Order Join states"
        )
    if manifest.get("selected_condition_id") != SELECTED_CONDITION_ID:
        raise LC4AsyncSpatialRedrawError("selected responsive condition differs")
    if manifest.get("default_condition_id") is not None:
        raise LC4AsyncSpatialRedrawError("owner selection must not become a product default")
    paths = [path for item in states for path in item["effect_paths"].values()]
    if len(paths) != 12 or len(set(paths)) != 12:
        raise LC4AsyncSpatialRedrawError("selected effect path coverage differs")
    for variant in SELECTED_VARIANTS:
        expected_information = information_set(ledger, variant)
        if any(
            item["information_set"] != expected_information
            for item in states
            if item["variant"] == variant
        ):
            raise LC4AsyncSpatialRedrawError(f"{variant} selected states are not matched")


def _selected_header(
    commands: list[dict[str, Any]], variant: str, width: int, pad: int
) -> int:
    title_size = 24 if width >= 480 else 22
    _text(
        commands,
        "header.eyebrow",
        pad,
        28,
        "LC4 · ASYNC · SELECTED",
        10,
        _P["cyan"],
        700,
    )
    _text(
        commands,
        "header.variant",
        width - pad,
        28,
        f'{variant.replace("_", " ")} · {width}',
        10,
        _P["violet"],
        700,
        anchor="end",
    )
    _text(
        commands,
        "header.title",
        pad,
        62,
        "Partial-Order Join",
        title_size,
        _P["ink"],
        700,
    )
    if width >= 480:
        question_lines = ["Why only after both complete — and can either finish first?"]
    else:
        question_lines = [
            "Why only after both complete —",
            "and can either finish first?",
        ]
    for index, line in enumerate(question_lines):
        _text(
            commands,
            f"header.question.{index}",
            pad,
            94 + index * 18,
            line,
            12,
            _P["dim"],
        )
    counts_y = 132 if width >= 480 else 146
    _text(
        commands,
        "header.counts",
        pad,
        counts_y,
        "2 runs · 16 events · 22 relations · 2 proofs · 4 boundaries",
        10,
        _P["dim"],
        600,
    )
    rule_y = counts_y + 20
    _line(
        commands,
        "header.rule",
        [(pad, rule_y), (width - pad, rule_y)],
        _P["guide"],
        1,
    )
    return rule_y


def _selected_source_launch(
    commands: list[dict[str, Any]], width: int, pad: int, y: int, x_a: int, x_b: int
) -> None:
    sequence_x = pad + 24
    _text(
        commands,
        "source.caption",
        pad,
        y - 30,
        "SOURCE-GUARANTEED LAUNCH ORDER",
        9,
        _P["cyan"],
        700,
        "source",
    )
    _circle(commands, "source.sequence", sequence_x, y, 7, _P["surface"], _P["cyan"], 2, "source")
    _text(
        commands,
        "source.sequence.label",
        sequence_x,
        y + 26,
        "Sequence",
        10,
        _P["dim"],
        600,
        "source",
        "middle",
    )
    _line(
        commands,
        "source.launch.route",
        [(sequence_x + 8, y), (x_a, y), (x_b, y)],
        _P["cyan"],
        3,
        role="source",
    )
    _arrow(commands, "source.launch.route.arrow", x_b, y, "right", _P["cyan"], 8, "source")
    for participant, x in (("A", x_a), ("B", x_b)):
        _circle(
            commands,
            f"source.launch.{participant}",
            x,
            y,
            9,
            _P["canvas"],
            _P["cyan"],
            3,
            "source",
        )
        _text(
            commands,
            f"source.launch.{participant}.label",
            x,
            y - 16,
            f"launch {participant}",
            10,
            _P["ink"],
            700,
            "source",
            "middle",
        )
    _text(
        commands,
        "source.launch.order",
        (x_a + x_b) // 2,
        y + 32,
        "launch A → B",
        11,
        _P["cyan"],
        700,
        "source",
        "middle",
    )


def _selected_horizontal_proof(
    commands: list[dict[str, Any]], width: int, pad: int, y: int
) -> None:
    left = pad + 12
    right = width - pad - 12
    _line(
        commands,
        "proof.bracket",
        [(left, y), (left, y + 8), (right, y + 8), (right, y)],
        _P["violet"],
        2,
        role="proof",
    )
    _line(
        commands,
        "dag.proof.bracket",
        [(left, y), (left, y + 8), (right, y + 8), (right, y)],
        _P["violet"],
        2,
        role="dag",
    )
    _text(commands, "proof.label.0", left, y + 30, "A ↛ B", 10, _P["ink"], role="proof")
    _text(
        commands,
        "proof.label.1",
        left + 72,
        y + 30,
        "B ↛ A",
        10,
        _P["ink"],
        role="proof",
    )
    _text(
        commands,
        "proof.label.2",
        left,
        y + 52,
        "relation set complete",
        10,
        _P["ink"],
        role="proof",
    )
    _text(
        commands,
        "proof.label.3",
        right,
        y + 52,
        "therefore A ∥ B",
        11,
        _P["violet"],
        700,
        "proof",
        "end",
    )


def _selected_barrier(
    commands: list[dict[str, Any]], width: int, pad: int, y: int, x_a: int, x_b: int
) -> None:
    left = pad + 54
    right = width - pad - 54
    centre = width // 2
    _text(
        commands,
        "barrier.caption",
        pad,
        y + 46,
        "EXPLICIT PROJECT-OWNED JOIN",
        9,
        _P["gold"],
        700,
        "barrier",
    )
    _rect(
        commands,
        "barrier.and",
        left,
        y,
        right - left,
        24,
        _P["gold"],
        _P["gold"],
        0,
        2,
        "barrier",
        full_enclosure=True,
    )
    for participant, x in (("A", x_a), ("B", x_b)):
        _circle(
            commands,
            f"barrier.socket.{participant}",
            x,
            y,
            6,
            _P["canvas"],
            _P["gold"],
            2,
            "barrier",
        )
        _text(
            commands,
            f"barrier.socket.{participant}.label",
            x - 10 if participant == "A" else x + 10,
            y - 11,
            f"socket {participant}",
            9,
            _P["gold"],
            700,
            "barrier",
            "end" if participant == "A" else "start",
        )
    _text(
        commands,
        "barrier.label",
        centre,
        y + 15,
        "AND · 2/2 ARRIVED",
        10,
        _P["canvas"],
        700,
        "barrier",
        "middle",
    )
    _line(
        commands,
        "barrier.release.route",
        [(centre, y + 30), (centre, y + 72)],
        _P["green"],
        4,
        role="barrier",
    )
    _arrow(commands, "barrier.release.arrow", centre, y + 72, "down", _P["green"], 9, "barrier")
    _text(
        commands,
        "barrier.release.label",
        centre + 14,
        y + 58,
        "RELEASE ONCE (1)",
        10,
        _P["green"],
        700,
        "barrier",
    )


def _selected_criterion(
    commands: list[dict[str, Any]], width: int, pad: int, y: int
) -> None:
    box_width = min(280, width - 2 * (pad + 24))
    x = (width - box_width) // 2
    centre = width // 2
    _rect(
        commands,
        "criterion.dock",
        x,
        y,
        box_width,
        62,
        "#193126",
        _P["green"],
        2,
        8,
        "criterion",
        full_enclosure=True,
    )
    _text(
        commands,
        "criterion.eyebrow",
        centre,
        y + 20,
        "CRITERION · AFTER RELEASE",
        9,
        _P["green"],
        700,
        "criterion",
        "middle",
    )
    _text(
        commands,
        "criterion.label",
        centre,
        y + 44,
        "Set LC4AsyncComplete = true",
        12,
        _P["ink"],
        700,
        "criterion",
        "middle",
    )


def _selected_footer(
    commands: list[dict[str, Any]], width: int, pad: int, frontier_y: int, actions_y: int
) -> int:
    inner = width - 2 * pad
    _rect(
        commands,
        "frontier.strip",
        pad,
        frontier_y,
        inner,
        94,
        "#2c241d",
        _P["orange"],
        2,
        6,
        "frontier",
        full_enclosure=True,
    )
    _text(
        commands,
        "frontier.title",
        pad + 14,
        frontier_y + 24,
        "FRONTIER · BOUNDED POSITIVE PROFILE",
        10,
        _P["orange"],
        700,
        "frontier",
    )
    _text(
        commands,
        "frontier.row.0",
        pad + 14,
        frontier_y + 49,
        "observed order only · 0.050 s ticks · 8-tick deadline",
        9,
        _P["ink"],
        role="frontier",
    )
    _text(
        commands,
        "frontier.row.1",
        pad + 14,
        frontier_y + 72,
        "no external service · cancel/incomplete → ABSTAINED",
        9,
        _P["dim"],
        role="frontier",
    )
    _rect(
        commands,
        "actions.dock",
        pad,
        actions_y,
        inner,
        58,
        _P["surface"],
        _P["guide"],
        1,
        8,
        "actions",
        full_enclosure=True,
    )
    labels = ("Select", "Proof", "All text", "Evidence", "Open source")
    cell_width = inner / len(labels)
    for index, label in enumerate(labels):
        x = int(pad + cell_width * (index + 0.5))
        _line(
            commands,
            f"action.{index}.underline",
            [(x - int(cell_width * 0.34), actions_y + 42), (x + int(cell_width * 0.34), actions_y + 42)],
            _P["guide"],
            1,
            role="actions",
        )
        _text(
            commands,
            f"action.{index}.label",
            x,
            actions_y + 31,
            label,
            9 if width < 700 else 10,
            _P["dim"],
            600,
            "actions",
            "middle",
        )
    return actions_y + 80


def build_selected_responsive_scene(
    ledger: Mapping[str, Any], variant: str, width: int
) -> dict[str, Any]:
    """Build the selected Partial-Order Join at one declared review state."""

    visual.validate_accountable_ledger(ledger)
    if variant not in SELECTED_VARIANTS or width not in SELECTED_WIDTHS:
        raise LC4AsyncSpatialRedrawError(f"unsupported selected state: {variant}/{width}")
    pad = 20 if width == 430 else 24 if width == 480 else 28
    x_a = int(width * (0.31 if width < 700 else 232 / 700))
    x_b = int(width * (0.69 if width < 700 else 468 / 700))
    centre = width // 2
    commands: list[dict[str, Any]] = []
    header_rule_y = _selected_header(commands, variant, width, pad)
    source_y = header_rule_y + 68
    _text(
        commands,
        "dag.transitive_reduction",
        pad,
        source_y - 42,
        "TRANSITIVE-REDUCTION DAG · validated relations only",
        9,
        _P["cyan"],
        700,
        "dag",
    )
    _selected_source_launch(commands, width, pad, source_y, x_a, x_b)
    continuation_y = source_y + 152
    completion_y = continuation_y + 126
    narrow = width < 700
    proof_y = completion_y + 46 if narrow else completion_y - 76
    arrival_y = completion_y + (190 if narrow else 106)
    barrier_y = arrival_y + 82
    criterion_y = barrier_y + 94
    frontier_y = criterion_y + 82
    actions_y = frontier_y + 108

    _dag_edge(
        commands,
        "dag.edge.launch_order",
        [(x_a, source_y + 10), (x_a, source_y + 48), (x_b, source_y + 48), (x_b, source_y + 80)],
        _P["cyan"],
    )
    _text(
        commands,
        "observed.order",
        centre,
        source_y + 99,
        (
            "observed A → B · not causal"
            if variant == "A_FIRST"
            else "observed B → A · not causal"
        ),
        9,
        _P["violet"],
        600,
        "observation",
        "middle",
    )
    for participant, x in (("A", x_a), ("B", x_b)):
        label = (
            f"{participant} · CONT. BOUNDARY"
            if narrow
            else f"{participant} · CONTINUATION BOUNDARY"
        )
        _dag_node(
            commands,
            f"dag.continuation.{participant}",
            x,
            continuation_y,
            label,
            _P["orange"],
        )
        commands[-1]["x"] = x - 16 if participant == "A" else x + 16
        commands[-1]["anchor"] = "end" if participant == "A" else "start"
        launch_x = x_a if participant == "A" else x_b
        _dag_edge(
            commands,
            f"dag.edge.launch_to_continuation.{participant}",
            [(launch_x, source_y + 10), (x, continuation_y - 12)],
            _P["orange"],
        )
        _dag_node(
            commands,
            f"dag.completion.{participant}",
            x,
            completion_y,
            f"complete {participant}",
            _P["violet"],
        )
        commands[-1]["x"] = x - 16 if participant == "A" else x + 16
        commands[-1]["anchor"] = "end" if participant == "A" else "start"
        _dag_edge(
            commands,
            f"dag.edge.continuation_to_completion.{participant}",
            [(x, continuation_y + 12), (x, completion_y - 12)],
            _P["violet"],
        )
        _dag_node(
            commands,
            f"dag.arrival.{participant}",
            x,
            arrival_y,
            f"arrive {participant}",
            _P["gold"],
        )
        commands[-1]["x"] = x - 16 if participant == "A" else x + 16
        commands[-1]["anchor"] = "end" if participant == "A" else "start"
        _dag_edge(
            commands,
            f"dag.edge.completion_to_arrival.{participant}",
            [(x, completion_y + 12), (x, arrival_y - 12)],
            _P["gold"],
        )
        _dag_edge(
            commands,
            f"dag.edge.arrival_to_socket.{participant}",
            [(x, arrival_y + 12), (x, barrier_y - 12)],
            _P["gold"],
        )
    _line(
        commands,
        "dag.rank.completions",
        [(x_a - 42, completion_y), (x_b + 42, completion_y)],
        _P["guide"],
        1,
        True,
        "dag",
    )
    _text(
        commands,
        "dag.rank.completions.label",
        centre,
        completion_y - 54,
        "same rank · no precedence",
        9,
        _P["dim"],
        600,
        "dag",
        "middle",
    )
    if narrow:
        _selected_horizontal_proof(commands, width, pad, proof_y)
    else:
        proof_x = width - 150
        _proof_ribbon(commands, proof_x, proof_y, "vertical")
        _line(
            commands,
            "dag.proof.bracket",
            [(proof_x, proof_y), (proof_x + 10, proof_y), (proof_x + 10, proof_y + 116), (proof_x, proof_y + 116)],
            _P["violet"],
            2,
            role="dag",
        )
    _selected_barrier(commands, width, pad, barrier_y, x_a, x_b)
    _selected_criterion(commands, width, pad, criterion_y)
    height = _selected_footer(commands, width, pad, frontier_y, actions_y)
    scene = _scene(SELECTED_CONDITION_ID, commands, height)
    scene["variant"] = variant
    scene["width"] = width
    scene["information_set"] = information_set(ledger, variant)
    scene["evidence_refs"] = deepcopy(ledger["variant_projections"][variant]["product_ids"])
    checks = validate_selected_responsive_scene(scene)
    if not checks["pass"]:
        raise LC4AsyncSpatialRedrawError(
            f"selected responsive checks failed for {variant}/{width}: {checks}"
        )
    return scene


def selected_structural_signature(scene: Mapping[str, Any]) -> str:
    """Hash causal geometry while excluding observation/evidence text."""

    geometry = []
    for command in scene["commands"]:
        if command["kind"] == "text":
            continue
        geometry.append(
            {
                key: value
                for key, value in command.items()
                if key not in {"fill", "stroke"}
            }
        )
    return hashlib.sha256(
        json.dumps(geometry, sort_keys=True).encode("utf-8")
    ).hexdigest()


def _text_bounds(command: Mapping[str, Any]) -> tuple[int, int, int, int]:
    font = visual._font(command["size"], command["weight"])
    anchor = {"start": "la", "middle": "ma", "end": "ra"}[command["anchor"]]
    left, top, right, bottom = font.getbbox(command["text"], anchor=anchor)
    left += command["x"]
    right += command["x"]
    top += command["y"]
    bottom += command["y"]
    return left, top, right, bottom


def _text_inside_canvas(command: Mapping[str, Any], width: int, height: int) -> bool:
    left, top, right, bottom = _text_bounds(command)
    return left >= 0 and top >= 0 and right <= width and bottom <= height


def _rectangles_overlap(
    left: tuple[int, int, int, int], right: tuple[int, int, int, int]
) -> bool:
    return not (
        left[2] + 1 <= right[0]
        or right[2] + 1 <= left[0]
        or left[3] + 1 <= right[1]
        or right[3] + 1 <= left[1]
    )


def _segment_hits_rect(
    start: tuple[int, int],
    end: tuple[int, int],
    bounds: tuple[int, int, int, int],
) -> bool:
    """Return whether a route segment enters a text rectangle."""

    x0, y0 = start
    x1, y1 = end
    left, top, right, bottom = bounds
    dx = x1 - x0
    dy = y1 - y0
    entering = 0.0
    leaving = 1.0
    for p, q in ((-dx, x0 - left), (dx, right - x0), (-dy, y0 - top), (dy, bottom - y0)):
        if p == 0:
            if q < 0:
                return False
            continue
        ratio = q / p
        if p < 0:
            entering = max(entering, ratio)
        else:
            leaving = min(leaving, ratio)
        if entering > leaving:
            return False
    return True


def validate_selected_responsive_scene(scene: Mapping[str, Any]) -> dict[str, Any]:
    base = validate_spatial_scene(scene)
    text_commands = [item for item in scene["commands"] if item["kind"] == "text"]
    text_containment = all(
        _text_inside_canvas(item, scene["width"], scene["height"])
        for item in text_commands
    )
    text_bounds = {item["element_id"]: _text_bounds(item) for item in text_commands}
    text_overlaps = [
        (left["element_id"], right["element_id"])
        for index, left in enumerate(text_commands)
        for right in text_commands[index + 1 :]
        if _rectangles_overlap(
            text_bounds[left["element_id"]], text_bounds[right["element_id"]]
        )
    ]
    route_text_collisions = []
    for route in (item for item in scene["commands"] if item["kind"] == "line"):
        if route.get("role") == "actions" or route["element_id"] == "header.rule":
            continue
        for label in text_commands:
            bounds = text_bounds[label["element_id"]]
            if any(
                _segment_hits_rect(start, end, bounds)
                for start, end in zip(route["points"], route["points"][1:])
            ):
                route_text_collisions.append((route["element_id"], label["element_id"]))
    base["text_bounds_containment"] = text_containment
    base["text_text_overlap_count"] = len(text_overlaps)
    base["text_text_overlaps"] = text_overlaps
    base["route_text_collision_count"] = len(route_text_collisions)
    base["route_text_collisions"] = route_text_collisions
    base["selected_structural_signature"] = selected_structural_signature(scene)
    base["pass"] = (
        base["pass"]
        and text_containment
        and not text_overlaps
        and not route_text_collisions
    )
    return base


def _build_selected_responsive_board(
    output_dir: Path,
    states: list[Mapping[str, Any]],
    variant: str,
    path: Path,
) -> None:
    selected = [item for item in states if item["variant"] == variant]
    images = [Image.open(output_dir / item["effect_paths"]["png"]).convert("RGB") for item in selected]
    margin = 28
    gap = 24
    cell_width = 720
    header = 112
    body_height = max(image.height for image in images)
    board = Image.new(
        "RGB",
        (margin * 2 + cell_width * 3 + gap * 2, header + body_height + margin),
        _P["canvas"],
    )
    draw = ImageDraw.Draw(board)
    draw.text(
        (margin, 22),
        f"LC4 ASYNC · SELECTED PARTIAL-ORDER JOIN · {variant}",
        fill=_P["ink"],
        font=visual._font(24, 700),
    )
    draw.text(
        (margin, 58),
        "430 / 480 / 700 logical pixels · deterministic design effects · Slate gated",
        fill=_P["dim"],
        font=visual._font(14, 400),
    )
    for index, (state, image) in enumerate(zip(selected, images)):
        x0 = margin + index * (cell_width + gap)
        draw.text(
            (x0, 88),
            f'{state["width"]} px',
            fill=_P["cyan"],
            font=visual._font(13, 700),
        )
        board.paste(image, (x0 + (cell_width - image.width) // 2, header))
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _build_selected_ab_overview(
    output_dir: Path, states: list[Mapping[str, Any]], path: Path
) -> None:
    chosen = [item for item in states if item["width"] == 700]
    images = [Image.open(output_dir / item["effect_paths"]["png"]).convert("RGB") for item in chosen]
    margin = 28
    gap = 24
    header = 112
    cell_width = 720
    body_height = max(image.height for image in images)
    board = Image.new(
        "RGB",
        (margin * 2 + cell_width * 2 + gap, header + body_height + margin),
        _P["canvas"],
    )
    draw = ImageDraw.Draw(board)
    draw.text(
        (margin, 22),
        "LC4 ASYNC · SELECTED PARTIAL-ORDER JOIN · A/B CHECK",
        fill=_P["ink"],
        font=visual._font(24, 700),
    )
    draw.text(
        (margin, 58),
        "Causal geometry is identical; retained observation and evidence identity differ",
        fill=_P["dim"],
        font=visual._font(14, 400),
    )
    for index, (state, image) in enumerate(zip(chosen, images)):
        x0 = margin + index * (cell_width + gap)
        draw.text(
            (x0, 88),
            state["variant"],
            fill=_P["violet"],
            font=visual._font(13, 700),
        )
        board.paste(image, (x0 + (cell_width - image.width) // 2, header))
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _prior_spatial_packet_lock() -> dict[str, Any]:
    root = Path(__file__).resolve().parents[2]
    artifact_dir = root / "artifacts/r1/lc4-async-visual-candidates"
    inventory_path = artifact_dir / "lc4-async-spatial-redraw-hashes.v1.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    for name, record in inventory["files"].items():
        path = artifact_dir / name
        if not path.is_file() or _sha256(path) != record["sha256"]:
            raise LC4AsyncSpatialRedrawError(
                f"prior spatial candidate artifact changed: {name}"
            )
    return {
        "format": "blueprint-lens-lc4-async-selected-responsive-prior-lock",
        "schema_version": "1.0.0",
        "status": "PASS__TEN_FILE_CANDIDATE_PACKET_BYTE_PRESERVED",
        "prior_inventory_path": (
            "artifacts/r1/lc4-async-visual-candidates/"
            "lc4-async-spatial-redraw-hashes.v1.json"
        ),
        "prior_inventory_sha256": _sha256(inventory_path),
        "prior_file_count": inventory["file_count"],
    }


def build_lc4_async_selected_responsive_artifacts(
    profile_path: str | Path,
    readiness_path: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Publish the selected six-state responsive effect-image packet."""

    destination = Path(output_dir)
    destination.mkdir(parents=True, exist_ok=True)
    prior_lock = _prior_spatial_packet_lock()
    ledger = load_accepted_ledger(profile_path, readiness_path)
    manifest = build_selected_responsive_manifest(ledger)
    manifest_path = destination / "lc4-async-partial-order-join-selected-manifest.v1.json"
    oracle_path = destination / "lc4-async-partial-order-join-selected-geometry-oracle.v1.json"
    prior_lock_path = destination / "lc4-async-partial-order-join-selected-prior-packet-lock.json"
    hashes_path = destination / "lc4-async-partial-order-join-selected-hashes.v1.json"
    board_a = destination / "lc4-async-partial-order-join-selected-a-first-responsive-board.png"
    board_b = destination / "lc4-async-partial-order-join-selected-b-first-responsive-board.png"
    board_ab = destination / "lc4-async-partial-order-join-selected-a-b-overview.png"
    _write_json(manifest_path, manifest)
    _write_json(prior_lock_path, prior_lock)

    scenes: dict[str, dict[str, Any]] = {}
    oracle_states = []
    effect_paths: list[Path] = []
    for state in manifest["states"]:
        scene = build_selected_responsive_scene(
            ledger, state["variant"], state["width"]
        )
        scenes[state["state_id"]] = scene
        svg_path = destination / state["effect_paths"]["svg"]
        png_path = destination / state["effect_paths"]["png"]
        svg_path.write_text(_svg(scene), encoding="utf-8", newline="\n")
        visual._render_png(scene, png_path)
        oracle_states.append(
            {
                "state_id": state["state_id"],
                "condition_id": SELECTED_CONDITION_ID,
                "variant": state["variant"],
                "width": state["width"],
                "canvas": [scene["width"], scene["height"]],
                "full_enclosures": deepcopy(scene["regions"]),
                "checks": validate_selected_responsive_scene(scene),
            }
        )
        effect_paths.extend((svg_path, png_path))
    equality = []
    for width in SELECTED_WIDTHS:
        a_id = f"{SELECTED_CONDITION_ID}__A_FIRST__W{width}"
        b_id = f"{SELECTED_CONDITION_ID}__B_FIRST__W{width}"
        equal = selected_structural_signature(scenes[a_id]) == selected_structural_signature(
            scenes[b_id]
        )
        equality.append({"width": width, "a_b_structure_equal": equal})
        if not equal:
            raise LC4AsyncSpatialRedrawError(f"A/B structure differs at {width}")
    oracle = {
        "format": "blueprint-lens-lc4-async-selected-responsive-geometry-oracle",
        "schema_version": "1.0.0",
        "status": "PASS__SIX_STATES__A_B_STRUCTURE_EQUAL",
        "coordinate_space": "top-left logical pixels at device scale 1",
        "states": oracle_states,
        "a_b_equality": equality,
        "non_claim": "authoring-side design effects only; not Slate or UE-visible evidence",
    }
    _write_json(oracle_path, oracle)
    _build_selected_responsive_board(destination, manifest["states"], "A_FIRST", board_a)
    _build_selected_responsive_board(destination, manifest["states"], "B_FIRST", board_b)
    _build_selected_ab_overview(destination, manifest["states"], board_ab)

    hashed_paths = [
        manifest_path,
        oracle_path,
        prior_lock_path,
        board_a,
        board_b,
        board_ab,
        *effect_paths,
    ]
    inventory = {
        "format": "blueprint-lens-lc4-async-selected-responsive-hashes",
        "schema_version": "1.0.0",
        "status": "PASS__EIGHTEEN_FILE_SELECTED_PACKET_COMPLETE",
        "file_count": len(hashed_paths),
        "files": {
            path.name: {"sha256": _sha256(path), "bytes": path.stat().st_size}
            for path in sorted(hashed_paths, key=lambda item: item.name)
        },
    }
    _write_json(hashes_path, inventory)
    _prior_spatial_packet_lock()
    return {
        "manifest": manifest_path,
        "oracle": oracle_path,
        "prior_packet_lock": prior_lock_path,
        "hashes": hashes_path,
        "review_board_a_first": board_a,
        "review_board_b_first": board_b,
        "review_board_a_b": board_ab,
    }
