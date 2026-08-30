"""Owner-selected responsive manifest contract for LC7 A3."""

from __future__ import annotations

import html
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from typing import Any, Mapping

from PIL import Image, ImageDraw

from blueprint_lens import lc7_adaptive_visual as _base
from blueprint_lens.lc6_visual import FONTS, TOKENS, _font


class LC7SelectedResponsiveError(ValueError):
    """Raised when the selected LC7 responsive contract drifts."""


SELECTED_CONDITION_ID = "LC7_CANONICAL_RECURRENCE_BACKBONE"
TARGET_WIDTHS = (430, 480, 700)
DISCLOSURE_STATES = (
    "NEUTRAL",
    "MEMBER_SUMMARY",
    "MEMBER_RELATIONS",
    "MEMBER_EVIDENCE",
    "COMPLETE_TEXT",
)
RESPONSIVE_MODES = {
    430: "single_column",
    480: "stacked_detail",
    700: "side_by_side",
}
REAL_PROFILE_SCALE_MODE = "FULL"
PRIOR_PACKET_PATH = (
    "artifacts/r1/lc7-static-scc-adaptive-layout-v1"
)
PRIOR_PACKET_SHA256 = (
    "88af4bf539cb60d79587cf127fe468a928d4c095cf78ace6df8a3d14c41e32de"
)
TRUTH_PACKET_PATH = "artifacts/r1/lc7-static-scc-truth"
MANIFEST_NAME = "lc7-a3-selected-manifest.json"
ORACLE_NAME = "lc7-a3-selected-oracle.json"
PRIOR_LOCK_NAME = "lc7-a3-prior-packet-lock.json"
HASHES_NAME = "hashes.json"
BOARD_SVG_NAME = "lc7-a3-responsive-target-board.svg"
BOARD_PNG_NAME = "lc7-a3-responsive-target-board.png"
REQUIRED_FONT_PATHS = tuple(Path(path) for path in FONTS.values())


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC7SelectedResponsiveError(message)


def load_lc7_visual_truth(evidence_dir: str | Path) -> dict[str, Any]:
    """Load the same frozen truth consumed by the adaptive comparison."""

    return _base.load_lc7_visual_truth(evidence_dir)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC7SelectedResponsiveError(
            f"cannot read LC7 selected-responsive input: {path}"
        ) from error
    _require(isinstance(value, dict), f"LC7 JSON input is not an object: {path}")
    return value


def _file_entries(directory: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for path in sorted(directory.iterdir(), key=lambda item: item.name):
        _require(path.is_file(), "prior LC7 packet contains a directory")
        payload = path.read_bytes()
        entries.append(
            {
                "path": path.name,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    return entries


def _aggregate_sha256(entries: list[dict[str, Any]]) -> str:
    payload = json.dumps(
        entries,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def build_prior_packet_lock(
    prior_packet_dir: str | Path,
) -> dict[str, Any]:
    """Validate and byte-lock the frozen 12-file adaptive packet."""

    directory = Path(prior_packet_dir).resolve()
    _require(directory.is_dir(), "prior LC7 adaptive packet is unavailable")
    entries = _file_entries(directory)
    expected_names = set(_base.OUTPUT_FILES)
    actual_names = {entry["path"] for entry in entries}
    _require(
        len(entries) == 12 and actual_names == expected_names,
        "prior LC7 adaptive packet must contain exactly its 12 frozen files",
    )

    hashes = _read_json(directory / "hashes.json")
    _require(
        hashes.get("excluded") == ["hashes.json"],
        "prior LC7 hash inventory is not self-excluding",
    )
    hash_files = hashes.get("files")
    _require(
        isinstance(hash_files, dict)
        and set(hash_files) == expected_names - {"hashes.json"},
        "prior LC7 hash coverage differs",
    )
    by_name = {entry["path"]: entry for entry in entries}
    for name, declared_hash in hash_files.items():
        _require(
            declared_hash == by_name[name]["sha256"],
            f"prior LC7 packet differs from its inventory: {name}",
        )

    aggregate = _aggregate_sha256(entries)
    _require(
        aggregate == PRIOR_PACKET_SHA256,
        "prior LC7 adaptive packet differs from the owner-selected bytes",
    )
    try:
        _base.validate_packet(directory)
    except (OSError, ValueError) as error:
        raise LC7SelectedResponsiveError(
            "prior LC7 adaptive packet fails its native audit"
        ) from error

    return {
        "format": "blueprint-lens-lc7-selected-prior-packet-lock",
        "format_version": "1.0.0",
        "path": PRIOR_PACKET_PATH,
        "entry_count": len(entries),
        "sha256": aggregate,
        "entries": entries,
    }


def _portable_truth_binding(truth: Mapping[str, Any]) -> dict[str, Any]:
    binding = truth.get("binding")
    _require(isinstance(binding, Mapping), "LC7 truth binding is unavailable")
    expected_files = {
        "explanation": "BP_LC7_StaticSCC.explanation.v1.json",
        "profile": "BP_LC7_StaticSCC.scc-profile.v1.json",
        "reviewed": "reviewed-ground-truth.v1.json",
    }
    portable: dict[str, Any] = {}
    for key, filename in expected_files.items():
        path_key = f"{key}_path"
        hash_key = f"{key}_sha256"
        source_path = binding.get(path_key)
        digest = binding.get(hash_key)
        _require(
            isinstance(source_path, str)
            and Path(source_path).name == filename,
            f"LC7 truth binding path differs: {path_key}",
        )
        _require(
            isinstance(digest, str)
            and len(digest) == 64
            and all(character in "0123456789abcdef" for character in digest),
            f"LC7 truth binding hash differs: {hash_key}",
        )
        portable[path_key] = f"{TRUTH_PACKET_PATH}/{filename}"
        portable[hash_key] = digest
    return portable


def _information_sha256(information: Mapping[str, Any]) -> str:
    payload = json.dumps(
        information,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _state_slug(disclosure_state: str) -> str:
    return disclosure_state.casefold().replace("_", "-")


def _validate_prior_manifest(
    prior_manifest: Mapping[str, Any],
    prior_packet_dir: str | Path,
    truth: Mapping[str, Any],
) -> None:
    actual = _read_json(
        Path(prior_packet_dir).resolve()
        / "lc7-adaptive-layout-manifest.json"
    )
    _require(
        dict(prior_manifest) == actual,
        "provided LC7 adaptive manifest differs from the frozen packet",
    )
    _require(
        actual.get("status") == _base.OUTPUT_STATUS
        and actual.get("selected_condition_id") == SELECTED_CONDITION_ID
        and actual.get("default_condition_id") is None,
        "prior LC7 selection contract differs",
    )
    _require(
        actual.get("profile_id") == "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1"
        and actual.get("claim_scope") == "STATIC_SOURCE_VISIBLE_SCC",
        "prior LC7 truth scope differs",
    )
    _require(
        actual.get("truth_binding", {}).get("explanation_sha256")
        == truth["binding"]["explanation_sha256"]
        and actual.get("truth_binding", {}).get("profile_sha256")
        == truth["binding"]["profile_sha256"]
        and actual.get("truth_binding", {}).get("reviewed_sha256")
        == truth["binding"]["reviewed_sha256"],
        "prior LC7 truth hashes differ",
    )
    defects = actual.get("defect_ledger")
    _require(
        isinstance(defects, list)
        and len(defects) == 9
        and len({item.get("defect_id") for item in defects}) == 9,
        "prior LC7 defect ledger differs",
    )


def _expected_manifest(
    truth: Mapping[str, Any],
    prior_manifest: Mapping[str, Any],
    prior_packet_dir: str | Path,
) -> dict[str, Any]:
    lock = build_prior_packet_lock(prior_packet_dir)
    _validate_prior_manifest(prior_manifest, prior_packet_dir, truth)
    scene = _base.build_scene(truth, SELECTED_CONDITION_ID)
    checks = _base.adaptive_scene_checks(scene, truth)
    _require(checks.get("pass") is True, "selected A3 scene is invalid")
    information = _base.scene_information_set(scene)
    _require(
        len(information["unit_ids"]) == 8
        and len(information["relation_ids"]) == 8,
        "selected A3 information set is not 8/8",
    )
    _require(
        prior_manifest.get("information_sets", {}).get(
            SELECTED_CONDITION_ID
        )
        == information,
        "selected A3 information differs from the adaptive packet",
    )
    group = truth.get("scc_group")
    _require(
        isinstance(group, Mapping)
        and isinstance(group.get("id"), str)
        and isinstance(group.get("entry_unit_id"), str),
        "selected A3 SCC focus is unavailable",
    )
    focused_scc_id = group["id"]
    selected_unit_id = group["entry_unit_id"]
    information_hash = _information_sha256(information)
    states: list[dict[str, Any]] = []
    for width in TARGET_WIDTHS:
        for disclosure_state in DISCLOSURE_STATES:
            state_selected_unit = (
                None
                if disclosure_state == "NEUTRAL"
                else selected_unit_id
            )
            slug = _state_slug(disclosure_state)
            stem = f"lc7-a3-{slug}-effect-{width}"
            states.append(
                {
                    "state_id": (
                        f"{SELECTED_CONDITION_ID}__{disclosure_state}"
                        f"__W{width}"
                    ),
                    "condition_id": SELECTED_CONDITION_ID,
                    "condition_label": _base.CONDITIONS[
                        SELECTED_CONDITION_ID
                    ][0],
                    "width": width,
                    "canvas_height": 760 if width == 700 else 1296,
                    "responsive_mode": RESPONSIVE_MODES[width],
                    "disclosure_state": disclosure_state,
                    "selected_unit_id": state_selected_unit,
                    "selected_scc_group_id": (
                        None
                        if state_selected_unit is None
                        else focused_scc_id
                    ),
                    "focused_scc_group_id": focused_scc_id,
                    "scale_mode": REAL_PROFILE_SCALE_MODE,
                    "information_sha256": information_hash,
                    "evidence_state": "authoring_design_target",
                    "effect_paths": {
                        "svg": f"{stem}.svg",
                        "png": f"{stem}.png",
                    },
                }
            )

    prior_non_claims = list(prior_manifest["non_claims"])
    return {
        "format": "blueprint-lens-lc7-selected-responsive-manifest",
        "format_version": "1.0.0",
        "status": "A3_SELECTED__THREE_WIDTH_AUTHORING_TARGETS",
        "selection_status": (
            "OWNER_SELECTED_A3__IMPLEMENTATION_CONDITION"
        ),
        "selected_condition_id": SELECTED_CONDITION_ID,
        "default_condition_id": None,
        "comparison_control": "LC7_COMPLETE_TEXT",
        "profile_id": "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1",
        "claim_scope": "STATIC_SOURCE_VISIBLE_SCC",
        "runtime_iterations": truth["profile"]["runtime_iterations"],
        "target_widths_logical_px": list(TARGET_WIDTHS),
        "real_profile_scale_mode_by_width": {
            str(width): REAL_PROFILE_SCALE_MODE
            for width in TARGET_WIDTHS
        },
        "focused_scc_group_id": focused_scc_id,
        "truth_binding": _portable_truth_binding(truth),
        "information_set": deepcopy(information),
        "prior_packet": {
            "path": lock["path"],
            "entry_count": lock["entry_count"],
            "sha256": lock["sha256"],
        },
        "responsive_board_paths": {
            "svg": BOARD_SVG_NAME,
            "png": BOARD_PNG_NAME,
        },
        "control_paths": {
            "manifest": MANIFEST_NAME,
            "oracle": ORACLE_NAME,
            "prior_packet_lock": PRIOR_LOCK_NAME,
            "hashes": HASHES_NAME,
        },
        "packet_file_count": 36,
        "states": states,
        "defect_ledger": deepcopy(prior_manifest["defect_ledger"]),
        "non_claims": [
            *prior_non_claims,
            "owner selection is not a product default",
            "responsive effects are not Slate or UE-visible evidence",
            (
                "synthetic FOCUS/INDEX fixtures are engineering tests, "
                "not real scalability evidence"
            ),
        ],
    }


def build_selected_manifest(
    truth: Mapping[str, Any],
    prior_manifest: Mapping[str, Any],
    prior_packet_dir: str | Path,
) -> dict[str, Any]:
    """Build the exact owner-selected, null-default responsive contract."""

    manifest = _expected_manifest(
        truth,
        prior_manifest,
        prior_packet_dir,
    )
    validate_selected_manifest(
        manifest,
        truth,
        prior_manifest,
        prior_packet_dir,
    )
    return manifest


def validate_selected_manifest(
    manifest: Mapping[str, Any],
    truth: Mapping[str, Any],
    prior_manifest: Mapping[str, Any],
    prior_packet_dir: str | Path,
) -> None:
    """Reject selection, truth, evidence, scale, or prior-packet drift."""

    _require(
        dict(manifest)
        == _expected_manifest(
            truth,
            prior_manifest,
            prior_packet_dir,
        ),
        "LC7 selected responsive manifest differs from the frozen contract",
    )


def _scene_dimensions(width: int) -> tuple[int, int]:
    _require(width in TARGET_WIDTHS, "unsupported LC7 responsive width")
    return (width, 760 if width == 700 else 1296)


def _scene_regions(width: int) -> dict[str, list[int] | str]:
    if width == 700:
        return {
            "content": [24, 142, 652, 594],
            "overview": [24, 142, 414, 594],
            "detail": [454, 142, 222, 594],
            "axis": "horizontal",
        }
    content_width = width - 48
    return {
        "content": [24, 142, content_width, 1130],
        "overview": [24, 142, content_width, 594],
        "detail": [24, 752, content_width, 520],
        "axis": "vertical",
    }


def _mark_region(
    commands: list[dict[str, Any]],
    start: int,
    region: str,
) -> None:
    for command in commands[start:]:
        command["region"] = region


def _add_header(
    commands: list[dict[str, Any]],
    width: int,
    disclosure_state: str,
) -> None:
    _base._rect(
        commands,
        "header",
        24,
        24,
        width - 48,
        100,
        fill=TOKENS["surface"],
        stroke=TOKENS["line"],
        radius=10,
    )
    _base._text(
        commands,
        "header.title",
        40,
        38,
        "Static Recurrence · A3",
        font_size=18,
        weight="bold",
    )
    _base._text(
        commands,
        "header.mode",
        width - 104,
        42,
        "FULL",
        fill=TOKENS["core"],
        font_size=8,
        weight="bold",
        fact_id="shared.scale_mode",
    )
    if width == 700:
        question_lines = (
            "Which source-visible units and relations form the recurrence upstream of Set LC7Complete?",
        )
    else:
        question_lines = (
            "Which source-visible units and relations form the recurrence",
            "upstream of Set LC7Complete?",
        )
    for index, line in enumerate(question_lines):
        _base._text(
            commands,
            f"header.question.{index}",
            40,
            72 + index * 16,
            line,
            font_size=9,
            weight="bold",
            **({"fact_id": "shared.question"} if index == 0 else {}),
        )
    _base._text(
        commands,
        "header.state",
        width - 158,
        104,
        disclosure_state.replace("MEMBER_", ""),
        fill=TOKENS["muted"],
        font_size=7,
        weight="bold",
    )


def _add_action(
    commands: list[dict[str, Any]],
    command_id: str,
    box: tuple[int, int, int, int],
    label: str,
    action_id: str,
) -> None:
    x, y, width, height = box
    _base._rect(
        commands,
        command_id,
        x,
        y,
        width,
        height,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["line"],
        radius=6,
        action_id=action_id,
        hit_target=[x, y, width, height],
    )
    _base._text(
        commands,
        f"{command_id}.text",
        x + 9,
        y + 9,
        label,
        font_size=8,
        weight="bold",
    )


def _add_overview(
    commands: list[dict[str, Any]],
    truth: Mapping[str, Any],
    bounds: list[int],
    selected: bool,
) -> None:
    start = len(commands)
    ox, oy, ow, oh = bounds
    _base._rect(
        commands,
        "overview",
        ox,
        oy,
        ow,
        oh,
        fill=TOKENS["surface"],
        stroke=TOKENS["line"],
        radius=10,
    )
    _base._text(
        commands,
        "overview.condition",
        ox + 16,
        oy + 14,
        "A3 · ADAPTIVE RECURRENCE BACKBONE",
        fill=TOKENS["core"],
        font_size=8,
        weight="bold",
    )
    _base._rect(
        commands,
        "overview.count",
        ox + 16,
        oy + 38,
        164,
        24,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["query"],
        radius=12,
        group_id=truth["scc_group"]["id"],
        fact_id="shared.scc_membership",
    )
    _base._text(
        commands,
        "overview.count.text",
        ox + 27,
        oy + 45,
        "STATIC SCC · 3 MEMBERS",
        fill=TOKENS["query"],
        font_size=7,
        weight="bold",
        fact_id="shared.counts",
    )
    _base._rect(
        commands,
        "overview.runtime",
        ox + 190,
        oy + 38,
        ow - 206,
        24,
        fill="#241D31",
        stroke=TOKENS["query"],
        radius=12,
        fact_id="shared.runtime_boundary",
    )
    runtime_text = (
        "RUNTIME · NOT CLAIMED"
        if ow < 410
        else "RUNTIME ITERATIONS · NOT CLAIMED"
    )
    _base._text(
        commands,
        "overview.runtime.text",
        ox + 200,
        oy + 45,
        runtime_text,
        fill=TOKENS["query"],
        font_size=7,
        weight="bold",
    )

    left = ox + 16
    centre = ox + ow // 2 - 55
    right = ox + ow - 124
    nodes = {
        "event": (left, oy + 110, 96, 42),
        "initialise": (left + 112, oy + 110, 104, 42),
        "get_counter": (right, oy + 78, 108, 42),
        "compare": (right, oy + 142, 108, 42),
        "branch": (centre, oy + 218, 110, 46),
        "visited": (centre, oy + 310, 110, 46),
        "advance": (centre, oy + 402, 110, 46),
        "criterion": (right, oy + 218, 108, 46),
    }
    _base._rect(
        commands,
        "overview.context.cue",
        left - 4,
        oy + 96,
        224,
        70,
        fill=TOKENS["surface_alt"],
        stroke=None,
        radius=8,
    )
    _base._text(
        commands,
        "overview.context.label",
        left,
        oy + 86,
        "ENTRY CONTEXT",
        fill=TOKENS["muted"],
        font_size=7,
        weight="bold",
    )
    _base._rect(
        commands,
        "overview.predicate.dock",
        right - 4,
        oy + 68,
        116,
        130,
        fill="#1B1A17",
        stroke=None,
        radius=8,
    )
    _base._text(
        commands,
        "overview.predicate.label",
        right,
        oy + 64,
        "PREDICATE",
        fill=TOKENS["opaque"],
        font_size=7,
        weight="bold",
    )
    _base._rect(
        commands,
        "overview.scc.band",
        centre - 18,
        oy + 204,
        146,
        270,
        fill="#1A1720",
        stroke=None,
        radius=12,
        group_id=truth["scc_group"]["id"],
    )
    _base._text(
        commands,
        "overview.scc.label",
        centre - 10,
        oy + 192,
        "SCC SPINE · 3",
        fill=TOKENS["query"],
        font_size=7,
        weight="bold",
    )
    event = nodes["event"]
    initialise = nodes["initialise"]
    get_counter = nodes["get_counter"]
    compare = nodes["compare"]
    branch = nodes["branch"]
    visited = nodes["visited"]
    advance = nodes["advance"]
    criterion = nodes["criterion"]
    branch_top_left = (branch[0] + 30, branch[1])
    branch_top_right = (branch[0] + branch[2] - 20, branch[1])
    branch_bottom = (branch[0] + branch[2] // 2, branch[1] + branch[3])
    branch_left = (branch[0], branch[1] + branch[3] // 2)
    branch_right = (
        branch[0] + branch[2],
        branch[1] + branch[3] // 2,
    )

    _base._route(
        commands,
        truth,
        "event_to_initialise",
        [
            (event[0] + event[2], event[1] + event[3] // 2),
            (initialise[0], initialise[1] + initialise[3] // 2),
        ],
        label="then",
        stroke=TOKENS["core"],
    )
    _base._route(
        commands,
        truth,
        "initialise_to_branch",
        [
            (
                initialise[0] + initialise[2] // 2,
                initialise[1] + initialise[3],
            ),
            (initialise[0] + initialise[2] // 2, branch[1] - 30),
            (branch_top_left[0], branch[1] - 30),
            branch_top_left,
        ],
        label="then",
        stroke=TOKENS["core"],
    )
    _base._route(
        commands,
        truth,
        "get_to_compare",
        [
            (get_counter[0] + get_counter[2] // 2, get_counter[1] + get_counter[3]),
            (compare[0] + compare[2] // 2, compare[1]),
        ],
        label="A",
        stroke=TOKENS["opaque"],
    )
    _base._route(
        commands,
        truth,
        "compare_to_branch",
        [
            (compare[0] + compare[2] // 2, compare[1] + compare[3]),
            (compare[0] + compare[2] // 2, branch[1] - 16),
            (branch_top_right[0], branch[1] - 16),
            branch_top_right,
        ],
        label="condition",
        stroke=TOKENS["opaque"],
    )
    _base._route(
        commands,
        truth,
        "branch_to_visited",
        [
            branch_bottom,
            (visited[0] + visited[2] // 2, visited[1]),
        ],
        label="then",
        stroke=TOKENS["core"],
    )
    _base._route(
        commands,
        truth,
        "visited_to_advance",
        [
            (visited[0] + visited[2] // 2, visited[1] + visited[3]),
            (advance[0] + advance[2] // 2, advance[1]),
        ],
        label="then",
        stroke=TOKENS["core"],
    )
    return_x = centre - 24
    _base._route(
        commands,
        truth,
        "advance_to_branch",
        [
            (advance[0], advance[1] + advance[3] // 2),
            (return_x, advance[1] + advance[3] // 2),
            (return_x, branch_left[1]),
            branch_left,
        ],
        label="return",
        stroke=TOKENS["query"],
    )
    _base._route(
        commands,
        truth,
        "branch_to_criterion",
        [
            branch_right,
            (criterion[0], criterion[1] + criterion[3] // 2),
        ],
        label="else",
        stroke=TOKENS["criterion"],
    )

    for key, box in nodes.items():
        _base._node(commands, truth, key, box)

    if selected:
        _base._rect(
            commands,
            "selection.member",
            branch[0] - 3,
            branch[1] - 3,
            branch[2] + 6,
            branch[3] + 6,
            fill=None,
            stroke=TOKENS["query"],
            stroke_width=2,
            radius=9,
            state_emphasis=True,
            selected_unit_id=truth["scc_group"]["entry_unit_id"],
        )

    action_width = (ow - 46) // 3
    action_y = oy + oh - 42
    actions = (
        ("inspect", "Inspect cycle", "inspect_cycle"),
        ("complete", "Complete text", "show_complete_text"),
        ("source", "Open source", "open_source"),
    )
    for index, (suffix, label, action_id) in enumerate(actions):
        _add_action(
            commands,
            f"action.{suffix}",
            (
                ox + 16 + index * (action_width + 7),
                action_y,
                action_width,
                30,
            ),
            label,
            action_id,
        )
    _mark_region(commands, start, "overview")


def _detail_row(
    commands: list[dict[str, Any]],
    command_id: str,
    x: int,
    y: int,
    width: int,
    title: str,
    value: str,
    **metadata: Any,
) -> None:
    _base._rect(
        commands,
        command_id,
        x,
        y,
        width,
        38,
        fill=TOKENS["surface"],
        stroke=TOKENS["line"],
        radius=6,
        **metadata,
    )
    _base._text(
        commands,
        f"{command_id}.title",
        x + 10,
        y + 6,
        title,
        fill=TOKENS["muted"],
        font_size=7,
        weight="bold",
    )
    _base._text(
        commands,
        f"{command_id}.value",
        x + 10,
        y + 19,
        value,
        font_size=8,
        weight="bold",
    )


def _add_detail(
    commands: list[dict[str, Any]],
    truth: Mapping[str, Any],
    bounds: list[int],
    disclosure_state: str,
) -> dict[str, Any]:
    start = len(commands)
    dx, dy, dw, dh = bounds
    selected = disclosure_state != "NEUTRAL"
    _base._rect(
        commands,
        "detail",
        dx,
        dy,
        dw,
        dh,
        fill=TOKENS["surface_alt"],
        stroke=TOKENS["line"],
        radius=10,
    )
    _base._text(
        commands,
        "detail.label",
        dx + 16,
        dy + 14,
        "DETAIL · LOCAL SCROLL",
        fill=TOKENS["muted"],
        font_size=8,
        weight="bold",
    )
    title = (
        "Select one cycle member"
        if not selected
        else "Branch · SCC member 1 of 3"
    )
    _base._text(
        commands,
        "detail.title",
        dx + 16,
        dy + 38,
        title,
        font_size=12,
        weight="bold",
    )
    tab_width = (dw - 44) // 3
    for index, (state, label) in enumerate(
        (
            ("MEMBER_SUMMARY", "SUMMARY"),
            ("MEMBER_RELATIONS", "RELATIONS"),
            ("MEMBER_EVIDENCE", "EVIDENCE"),
        )
    ):
        active = disclosure_state == state
        x = dx + 16 + index * (tab_width + 6)
        _base._rect(
            commands,
            f"detail.tab.{index}",
            x,
            dy + 68,
            tab_width,
            26,
            fill="#241D31" if active else TOKENS["surface"],
            stroke=TOKENS["query"] if active else TOKENS["line"],
            radius=5,
        )
        _base._text(
            commands,
            f"detail.tab.{index}.text",
            x + 7,
            dy + 76,
            label,
            fill=TOKENS["query"] if active else TOKENS["muted"],
            font_size=7,
            weight="bold",
        )

    stats = {
        "summary_row_count": 0,
        "relation_row_count": 0,
        "evidence_row_count": 0,
        "paragraph_block_count": 0,
        "complete_text_action_gated": False,
        "source_ids_preserved": False,
    }
    content_x = dx + 16
    content_width = dw - 32
    row_y = dy + 112
    selected_unit_id = truth["scc_group"]["entry_unit_id"]
    selected_unit = truth["units"][selected_unit_id]
    if disclosure_state == "NEUTRAL":
        _base._text(
            commands,
            "detail.neutral.help",
            content_x,
            row_y + 4,
            "Overview stays complete; no member is preselected.",
            fill=TOKENS["muted"],
            font_size=8,
        )
        _base._text(
            commands,
            "detail.neutral.boundary",
            content_x,
            row_y + 26,
            "Choose a member or use Complete text.",
            fill=TOKENS["muted"],
            font_size=8,
        )
    elif disclosure_state == "MEMBER_SUMMARY":
        rows = (
            ("ROLE", selected_unit["role"].upper()),
            ("STATUS", selected_unit["semantic_status"].upper()),
            ("SCC MEMBERSHIP", "STRUCTURAL · 3 MEMBERS"),
            ("INCLUSION", selected_unit["inclusion_reasons"][0].upper()),
        )
        for index, (label, value) in enumerate(rows):
            _detail_row(
                commands,
                f"detail.summary.{index}",
                content_x,
                row_y + index * 46,
                content_width,
                label,
                value,
                unit_ref_id=selected_unit_id,
            )
        stats["summary_row_count"] = len(rows)
    elif disclosure_state == "MEMBER_RELATIONS":
        ordered_relations = sorted(
            truth["relations"].values(),
            key=lambda relation: relation["id"],
        )
        for index, relation in enumerate(ordered_relations):
            direction = (
                "OUT"
                if relation["source_unit_id"] == selected_unit_id
                else "IN" if relation["target_unit_id"] == selected_unit_id else "CTX"
            )
            _detail_row(
                commands,
                f"detail.relation.{index}",
                content_x,
                row_y + index * 48,
                content_width,
                f"{index + 1:02d} · {relation['kind'].upper()}",
                f"{direction} · {relation['label'].upper()}",
                relation_ref_id=relation["id"],
                source_unit_id=relation["source_unit_id"],
                target_unit_id=relation["target_unit_id"],
            )
        stats["relation_row_count"] = len(ordered_relations)
    elif disclosure_state == "MEMBER_EVIDENCE":
        source = selected_unit["source_references"][0]
        evidence_rows = (
            ("NODE GUID", source["native_node_guid"]),
            ("SOURCE NODE", source["source_node_id"].split("::node::")[-1]),
            ("SOURCE PINS", f"{len(source['source_pin_ids'])} ACCOUNTABLE PINS"),
            ("TRUTH SHA", truth["binding"]["explanation_sha256"][:24]),
        )
        for index, (label, value) in enumerate(evidence_rows):
            _detail_row(
                commands,
                f"detail.evidence.{index}",
                content_x,
                row_y + index * 52,
                content_width,
                label,
                value,
                source_node_id=source["source_node_id"],
                local_scroll=True,
            )
        stats["evidence_row_count"] = len(evidence_rows)
        stats["source_ids_preserved"] = True
    elif disclosure_state == "COMPLETE_TEXT":
        lines = (
            "Complete text · explicit action-gated fallback",
            "Entry LC7_STATIC_SCC initializes LC7Counter.",
            "Get LC7Counter supplies the opaque integer predicate.",
            "Branch, Set LC7Visited and Set LC7Counter form the SCC.",
            "THEN advances through the SCC; RETURN reaches Branch.",
            "ELSE exits to criterion Set LC7Complete.",
            "All 8 unit IDs and 8 relation IDs remain source-linked.",
            "Runtime iterations: NOT_CLAIMED.",
        )
        _base._rect(
            commands,
            "detail.complete.block",
            content_x,
            row_y,
            content_width,
            250,
            fill=TOKENS["surface"],
            stroke=TOKENS["line"],
            radius=7,
            paragraph_block_id="complete-text-1",
            action_gate="show_complete_text",
        )
        for index, line in enumerate(lines):
            _base._text(
                commands,
                f"detail.complete.line.{index}",
                content_x + 10,
                row_y + 12 + index * 27,
                line,
                font_size=8,
                weight="bold" if index in {0, 7} else "regular",
                paragraph_block_id="complete-text-1",
            )
        stats["paragraph_block_count"] = 1
        stats["complete_text_action_gated"] = True
        stats["source_ids_preserved"] = True
    else:
        raise LC7SelectedResponsiveError("unsupported LC7 disclosure state")
    _mark_region(commands, start, "detail")
    return stats


def _overview_sha256(commands: list[Mapping[str, Any]]) -> str:
    overview = [
        {
            key: value
            for key, value in command.items()
            if key not in {"state_emphasis", "selected_unit_id"}
        }
        for command in commands
        if command.get("region") == "overview"
        and not command.get("state_emphasis")
    ]
    payload = json.dumps(
        overview,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def build_selected_scene(
    truth: Mapping[str, Any],
    width: int,
    disclosure_state: str,
) -> dict[str, Any]:
    """Build one deterministic FULL A3 state at an exact target width."""

    _require(width in TARGET_WIDTHS, "unsupported LC7 responsive width")
    _require(
        disclosure_state in DISCLOSURE_STATES,
        "unsupported LC7 disclosure state",
    )
    canvas_width, canvas_height = _scene_dimensions(width)
    regions = _scene_regions(width)
    commands: list[dict[str, Any]] = []
    _base._rect(
        commands,
        "canvas",
        0,
        0,
        canvas_width,
        canvas_height,
        fill=TOKENS["background"],
        stroke=None,
        radius=0,
        region="canvas",
    )
    _add_header(commands, width, disclosure_state)
    for command in commands:
        if command.get("id", "").startswith("header"):
            command["region"] = "header"
    selected = disclosure_state != "NEUTRAL"
    _add_overview(
        commands,
        truth,
        regions["overview"],
        selected,
    )
    detail_stats = _add_detail(
        commands,
        truth,
        regions["detail"],
        disclosure_state,
    )
    selected_unit_id = (
        truth["scc_group"]["entry_unit_id"] if selected else None
    )
    source_ids = sorted(
        {
            reference["source_node_id"]
            for unit in truth["units"].values()
            for reference in unit["source_references"]
        }
        | {
            endpoint["source_edge_id"]
            for relation in truth["relations"].values()
            for endpoint in relation["source_edge_endpoints"]
        }
    )
    scene = {
        "format": "blueprint-lens-lc7-selected-responsive-scene",
        "format_version": "1.0.0",
        "scene_id": (
            f"{SELECTED_CONDITION_ID}__{disclosure_state}__W{width}"
        ),
        "condition_id": SELECTED_CONDITION_ID,
        "condition_label": _base.CONDITIONS[SELECTED_CONDITION_ID][0],
        "width": canvas_width,
        "height": canvas_height,
        "viewport_height": 760,
        "responsive_mode": RESPONSIVE_MODES[width],
        "layout_axis": regions["axis"],
        "content_bounds": regions["content"],
        "overview_bounds": regions["overview"],
        "detail_bounds": regions["detail"],
        "disclosure_state": disclosure_state,
        "detail_mode": disclosure_state if selected else None,
        "scale_mode": REAL_PROFILE_SCALE_MODE,
        "selected_unit_id": selected_unit_id,
        "selected_scc_group_id": (
            truth["scc_group"]["id"] if selected else None
        ),
        "focused_scc_group_id": truth["scc_group"]["id"],
        "visible_scc_member_count": truth["scc_group"]["member_count"],
        "eye_path": ["entry", "predicate", "scc_spine", "criterion"],
        "complete_text_source_ids": (
            source_ids if disclosure_state == "COMPLETE_TEXT" else []
        ),
        "commands": commands,
        **detail_stats,
    }
    scene["overview_sha256"] = _overview_sha256(commands)
    selected_scene_checks(scene, truth, raise_on_error=True)
    return scene


def _command_bounds(
    command: Mapping[str, Any],
) -> tuple[int, int, int, int] | None:
    kind = command.get("kind")
    if kind == "rect":
        return (
            command["x"],
            command["y"],
            command["x"] + command["width"],
            command["y"] + command["height"],
        )
    if kind == "text":
        left, top, right, bottom = _font(
            command["font_size"],
            command["weight"],
        ).getbbox(command["text"])
        return (
            command["x"] + left,
            command["y"] + top,
            command["x"] + right,
            command["y"] + bottom,
        )
    if kind in {"polyline", "polygon"}:
        xs = [point[0] for point in command["points"]]
        ys = [point[1] for point in command["points"]]
        return min(xs), min(ys), max(xs), max(ys)
    if kind == "circle":
        return (
            command["cx"] - command["radius"],
            command["cy"] - command["radius"],
            command["cx"] + command["radius"],
            command["cy"] + command["radius"],
        )
    return None


def _boxes_overlap(
    first: tuple[int, int, int, int],
    second: tuple[int, int, int, int],
) -> bool:
    return (
        max(first[0], second[0]) < min(first[2], second[2])
        and max(first[1], second[1]) < min(first[3], second[3])
    )


def _segment_intersects_box_interior(
    start: tuple[int, int],
    end: tuple[int, int],
    box: tuple[int, int, int, int],
) -> bool:
    left, top, right, bottom = box
    if start[0] == end[0]:
        x = start[0]
        low, high = sorted((start[1], end[1]))
        return left < x < right and max(low, top) < min(high, bottom)
    if start[1] == end[1]:
        y = start[1]
        low, high = sorted((start[0], end[0]))
        return top < y < bottom and max(low, left) < min(high, right)
    return True


def _route_node_intersections(
    scene: Mapping[str, Any],
    truth: Mapping[str, Any],
) -> list[list[str]]:
    nodes = {
        command["unit_id"]: _command_bounds(command)
        for command in scene["commands"]
        if command.get("kind") == "rect" and command.get("unit_id")
    }
    errors: list[list[str]] = []
    for route in scene["commands"]:
        if route.get("kind") != "polyline" or not route.get("relation_id"):
            continue
        relation = truth["relations"][route["relation_id"]]
        allowed = {
            relation["source_unit_id"],
            relation["target_unit_id"],
        }
        for unit_id, box in nodes.items():
            if unit_id in allowed or box is None:
                continue
            if any(
                _segment_intersects_box_interior(tuple(a), tuple(b), box)
                for a, b in zip(route["points"], route["points"][1:])
            ):
                errors.append([route["id"], unit_id])
    return sorted(errors)


def _text_node_overlaps(scene: Mapping[str, Any]) -> list[list[str]]:
    nodes = [
        command
        for command in scene["commands"]
        if command.get("kind") == "rect" and command.get("unit_id")
    ]
    errors: list[list[str]] = []
    for text_command in scene["commands"]:
        if text_command.get("kind") != "text":
            continue
        text_bounds = _command_bounds(text_command)
        if text_bounds is None:
            continue
        for node in nodes:
            if text_command.get("unit_ref_id") == node["unit_id"]:
                continue
            node_bounds = _command_bounds(node)
            if node_bounds and _boxes_overlap(text_bounds, node_bounds):
                errors.append([text_command["id"], node["id"]])
    return sorted(errors)


def _attachment_errors(scene: Mapping[str, Any]) -> list[str]:
    routes = {
        command["relation_key"]: command
        for command in scene["commands"]
        if command.get("kind") == "polyline" and command.get("relation_key")
    }
    attachment_points = [
        tuple(routes["initialise_to_branch"]["points"][-1]),
        tuple(routes["compare_to_branch"]["points"][-1]),
        tuple(routes["branch_to_visited"]["points"][0]),
        tuple(routes["advance_to_branch"]["points"][-1]),
        tuple(routes["branch_to_criterion"]["points"][0]),
    ]
    return [] if len(set(attachment_points)) == len(attachment_points) else [
        "branch_family_attachments_are_not_distinct"
    ]


def selected_scene_checks(
    scene: Mapping[str, Any],
    truth: Mapping[str, Any],
    *,
    raise_on_error: bool = False,
) -> dict[str, Any]:
    """Measure coverage, text fit, routes, attachments, and state invariants."""

    commands = scene.get("commands", [])
    command_ids = [command.get("id") for command in commands]
    duplicate_ids = sorted(
        {
            command_id
            for command_id in command_ids
            if command_ids.count(command_id) > 1
        }
    )
    out_of_bounds = []
    for command in commands:
        bounds = _command_bounds(command)
        if bounds and (
            bounds[0] < 0
            or bounds[1] < 0
            or bounds[2] > scene.get("width", 0)
            or bounds[3] > scene.get("height", 0)
        ):
            out_of_bounds.append(command["id"])
    unit_ids = sorted(
        {
            command["unit_id"]
            for command in commands
            if command.get("unit_id")
        }
    )
    relation_ids = sorted(
        {
            command["relation_id"]
            for command in commands
            if command.get("relation_id")
        }
    )
    group_ids = {
        command["group_id"]
        for command in commands
        if command.get("group_id")
    }
    action_ids = {
        command["action_id"]
        for command in commands
        if command.get("action_id")
    }
    selected_member_ids = [
        command["selected_unit_id"]
        for command in commands
        if command.get("selected_unit_id")
    ]
    runtime_ids = {
        command["id"]
        for command in commands
        if command.get("fact_id") == "shared.runtime_boundary"
    }
    geometry = _base.route_geometry(scene)
    returning_id = "route.advance_to_branch"
    bend_errors = sorted(
        route_id
        for route_id, bends in geometry["bend_counts"].items()
        if bends > 3 or (route_id == returning_id and bends > 2)
    )
    expected_units = sorted(truth["units"])
    expected_relations = sorted(truth["relations"])
    criterion_count = int(truth["criterion_unit_id"] in unit_ids)
    source_ids_preserved = bool(scene.get("source_ids_preserved"))
    if scene.get("disclosure_state") == "COMPLETE_TEXT":
        expected_source_ids = {
            reference["source_node_id"]
            for unit in truth["units"].values()
            for reference in unit["source_references"]
        } | {
            endpoint["source_edge_id"]
            for relation in truth["relations"].values()
            for endpoint in relation["source_edge_endpoints"]
        }
        source_ids_preserved = (
            set(scene.get("complete_text_source_ids", []))
            == expected_source_ids
        )
    checks = {
        "duplicate_command_ids": duplicate_ids,
        "out_of_bounds_ids": sorted(out_of_bounds),
        "text_node_overlap_ids": _text_node_overlaps(scene),
        "route_node_intersections": _route_node_intersections(scene, truth),
        "collinear_route_overlaps": geometry["collinear_overlap_pairs"],
        "attachment_errors": _attachment_errors(scene),
        "bend_errors": bend_errors,
        "unit_count": len(unit_ids),
        "relation_count": len(relation_ids),
        "scc_count": int(group_ids == {truth["scc_group"]["id"]}),
        "criterion_count": criterion_count,
        "runtime_boundary_count": len(runtime_ids),
        "action_count": len(action_ids),
        "selected_member_count": len(selected_member_ids),
        "unit_identity_errors": (
            [] if unit_ids == expected_units else ["unit_identity"]
        ),
        "relation_identity_errors": (
            [] if relation_ids == expected_relations else ["relation_identity"]
        ),
        "action_errors": (
            []
            if action_ids == {"inspect_cycle", "show_complete_text", "open_source"}
            else ["actions"]
        ),
        "overview_hash_errors": (
            []
            if scene.get("overview_sha256")
            == _overview_sha256(list(commands))
            else ["overview_hash"]
        ),
        "source_id_errors": (
            []
            if scene.get("disclosure_state") != "COMPLETE_TEXT"
            or source_ids_preserved
            else ["complete_text_source_ids"]
        ),
        "eye_path": list(scene.get("eye_path", [])),
        "geometry": geometry,
    }
    list_error_keys = (
        "duplicate_command_ids",
        "out_of_bounds_ids",
        "text_node_overlap_ids",
        "route_node_intersections",
        "collinear_route_overlaps",
        "attachment_errors",
        "bend_errors",
        "unit_identity_errors",
        "relation_identity_errors",
        "action_errors",
        "overview_hash_errors",
        "source_id_errors",
    )
    scalar_pass = (
        checks["unit_count"] == 8
        and checks["relation_count"] == 8
        and checks["scc_count"] == 1
        and checks["criterion_count"] == 1
        and checks["runtime_boundary_count"] == 1
        and checks["action_count"] == 3
        and checks["selected_member_count"]
        == (0 if scene.get("disclosure_state") == "NEUTRAL" else 1)
        and (
            not selected_member_ids
            or selected_member_ids
            == [truth["scc_group"]["entry_unit_id"]]
        )
        and checks["eye_path"]
        == ["entry", "predicate", "scc_spine", "criterion"]
    )
    checks["pass"] = bool(
        scalar_pass
        and not any(checks[key] for key in list_error_keys)
    )
    if raise_on_error:
        _require(checks["pass"], f"selected LC7 scene checks failed: {checks}")
    return checks


def svg_for_selected_scene(
    scene: Mapping[str, Any],
    truth: Mapping[str, Any],
) -> str:
    """Render one validated responsive scene as deterministic SVG."""

    selected_scene_checks(scene, truth, raise_on_error=True)
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{scene["width"]}" height="{scene["height"]}" '
            f'viewBox="0 0 {scene["width"]} {scene["height"]}">'
        ),
        (
            f'  <title>{html.escape(str(scene["condition_label"]))} · '
            f'{html.escape(str(scene["disclosure_state"]))} · '
            f'{scene["width"]}</title>'
        ),
    ]
    for command in scene["commands"]:
        command_id = html.escape(str(command["id"]), quote=True)
        if command["kind"] == "rect":
            fill = command["fill"] or "none"
            stroke = command["stroke"] or "none"
            lines.append(
                f'  <rect id="{command_id}" x="{command["x"]}" '
                f'y="{command["y"]}" width="{command["width"]}" '
                f'height="{command["height"]}" rx="{command["radius"]}" '
                f'fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "text":
            weight = 700 if command["weight"] == "bold" else 400
            lines.append(
                f'  <text id="{command_id}" x="{command["x"]}" '
                f'y="{command["y"]}" fill="{command["fill"]}" '
                f'font-family="Segoe UI" font-size="{command["font_size"]}" '
                f'font-weight="{weight}" dominant-baseline="text-before-edge">'
                f'{html.escape(str(command["text"]))}</text>'
            )
        elif command["kind"] == "polyline":
            points = " ".join(
                f"{point[0]},{point[1]}" for point in command["points"]
            )
            dash = (
                f' stroke-dasharray="{",".join(map(str, command["dash"]))}"'
                if command["dash"]
                else ""
            )
            lines.append(
                f'  <polyline id="{command_id}" points="{points}" '
                f'fill="none" stroke="{command["stroke"]}" '
                f'stroke-width="{command["stroke_width"]}"{dash} '
                'stroke-linecap="round" stroke-linejoin="round"/>'
            )
        elif command["kind"] == "polygon":
            points = " ".join(
                f"{point[0]},{point[1]}" for point in command["points"]
            )
            lines.append(
                f'  <polygon id="{command_id}" points="{points}" '
                f'fill="{command["fill"]}" stroke="{command["stroke"]}" '
                f'stroke-width="{command["stroke_width"]}"/>'
            )
        elif command["kind"] == "circle":
            lines.append(
                f'  <circle id="{command_id}" cx="{command["cx"]}" '
                f'cy="{command["cy"]}" r="{command["radius"]}" '
                f'fill="{command["fill"]}"/>'
            )
        else:
            raise LC7SelectedResponsiveError(
                f"unsupported selected LC7 draw command: {command['kind']}"
            )
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def png_for_selected_scene(
    scene: Mapping[str, Any],
    truth: Mapping[str, Any],
    path: str | Path,
) -> None:
    """Render one validated responsive scene as deterministic PNG."""

    selected_scene_checks(scene, truth, raise_on_error=True)
    image = Image.new(
        "RGB",
        (scene["width"], scene["height"]),
        TOKENS["background"],
    )
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        if command["kind"] == "rect":
            draw.rounded_rectangle(
                [
                    command["x"],
                    command["y"],
                    command["x"] + command["width"],
                    command["y"] + command["height"],
                ],
                radius=command["radius"],
                fill=command["fill"],
                outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif command["kind"] == "text":
            draw.text(
                (command["x"], command["y"]),
                command["text"],
                font=_font(command["font_size"], command["weight"]),
                fill=command["fill"],
                anchor="lt",
            )
        elif command["kind"] == "polyline":
            draw.line(
                [tuple(point) for point in command["points"]],
                fill=command["stroke"],
                width=command["stroke_width"],
                joint="curve",
            )
        elif command["kind"] == "polygon":
            draw.polygon(
                [tuple(point) for point in command["points"]],
                fill=command["fill"],
                outline=command["stroke"],
            )
        elif command["kind"] == "circle":
            draw.ellipse(
                [
                    command["cx"] - command["radius"],
                    command["cy"] - command["radius"],
                    command["cx"] + command["radius"],
                    command["cy"] + command["radius"],
                ],
                fill=command["fill"],
            )
        else:
            raise LC7SelectedResponsiveError(
                f"unsupported selected LC7 draw command: {command['kind']}"
            )
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(
        target,
        format="PNG",
        optimize=False,
        compress_level=9,
    )


def _board_geometry(
    scene: Mapping[str, Any],
    column: int,
    row: int,
) -> tuple[int, int, float]:
    card_x = 30 + column * 590
    card_y = 100 + row * 600
    scale = min(520 / scene["width"], 520 / scene["height"])
    rendered_width = scene["width"] * scale
    x = round(card_x + 280 - rendered_width / 2)
    return x, card_y + 54, scale


def _responsive_board_svg(
    scenes: list[Mapping[str, Any]],
    truth: Mapping[str, Any],
) -> str:
    width, height = 1800, 3120
    by_key = {
        (scene["width"], scene["disclosure_state"]): scene
        for scene in scenes
    }
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}">'
        ),
        "  <title>LC7 A3 selected responsive target matrix</title>",
        f'  <rect width="{width}" height="{height}" fill="{TOKENS["background"]}"/>',
        (
            '  <text x="30" y="20" fill="#F4F7FB" '
            'font-family="Segoe UI" font-size="24" font-weight="700" '
            'dominant-baseline="text-before-edge">'
            "LC7 A3 · selected responsive target matrix</text>"
        ),
        (
            '  <text x="30" y="56" fill="#98A7BA" '
            'font-family="Segoe UI" font-size="12" '
            'dominant-baseline="text-before-edge">'
            "15 authoring targets · FULL real profile · no Slate or human claim</text>"
        ),
    ]
    for column, target_width in enumerate(TARGET_WIDTHS):
        for row, state in enumerate(DISCLOSURE_STATES):
            scene = by_key[(target_width, state)]
            card_x = 30 + column * 590
            card_y = 100 + row * 600
            x, y, scale = _board_geometry(scene, column, row)
            lines.append(
                f'  <rect x="{card_x}" y="{card_y}" width="560" '
                f'height="570" rx="12" fill="{TOKENS["surface"]}" '
                f'stroke="{TOKENS["line"]}"/>'
            )
            lines.append(
                f'  <text x="{card_x + 16}" y="{card_y + 16}" '
                f'fill="{TOKENS["text"]}" font-family="Segoe UI" '
                f'font-size="13" font-weight="700" '
                f'dominant-baseline="text-before-edge">'
                f'{target_width}px · {html.escape(state)}</text>'
            )
            body = svg_for_selected_scene(scene, truth).splitlines()[3:-1]
            prefix = f"board-{target_width}-{state.casefold()}__"
            lines.append(
                f'  <g transform="translate({x} {y}) scale({scale:.8f})">'
            )
            lines.extend(
                "  " + line.replace('id="', f'id="{prefix}')
                for line in body
            )
            lines.append("  </g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _responsive_board_png(
    scenes: list[Mapping[str, Any]],
    effect_paths: Mapping[str, Path],
    path: Path,
) -> None:
    image = Image.new("RGB", (1800, 3120), TOKENS["background"])
    draw = ImageDraw.Draw(image)
    draw.text(
        (30, 20),
        "LC7 A3 · selected responsive target matrix",
        font=_font(24, "bold"),
        fill=TOKENS["text"],
        anchor="lt",
    )
    draw.text(
        (30, 56),
        "15 authoring targets · FULL real profile · no Slate or human claim",
        font=_font(12, "regular"),
        fill=TOKENS["muted"],
        anchor="lt",
    )
    by_key = {
        (scene["width"], scene["disclosure_state"]): scene
        for scene in scenes
    }
    for column, target_width in enumerate(TARGET_WIDTHS):
        for row, state in enumerate(DISCLOSURE_STATES):
            scene = by_key[(target_width, state)]
            card_x = 30 + column * 590
            card_y = 100 + row * 600
            draw.rounded_rectangle(
                [card_x, card_y, card_x + 560, card_y + 570],
                radius=12,
                fill=TOKENS["surface"],
                outline=TOKENS["line"],
                width=1,
            )
            draw.text(
                (card_x + 16, card_y + 16),
                f"{target_width}px · {state}",
                font=_font(13, "bold"),
                fill=TOKENS["text"],
                anchor="lt",
            )
            x, y, scale = _board_geometry(scene, column, row)
            png_path = effect_paths[scene["scene_id"]]
            with Image.open(png_path) as effect:
                expected_size = (scene["width"], scene["height"])
                _require(
                    effect.size == expected_size,
                    f"selected LC7 effect dimensions differ: {scene['scene_id']}",
                )
                resized = effect.convert("RGB").resize(
                    (
                        round(scene["width"] * scale),
                        round(scene["height"] * scale),
                    ),
                    Image.Resampling.LANCZOS,
                )
                image.paste(resized, (x, y))
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _manifest_file_names(manifest: Mapping[str, Any]) -> set[str]:
    effects = {
        path
        for state in manifest.get("states", [])
        for path in state.get("effect_paths", {}).values()
    }
    boards = set(manifest.get("responsive_board_paths", {}).values())
    controls = set(manifest.get("control_paths", {}).values())
    return effects | boards | controls


def _packet_hashes(directory: Path, names: set[str]) -> dict[str, Any]:
    return {
        "format": "blueprint-lens-lc7-selected-self-excluding-hashes",
        "format_version": "1.0.0",
        "algorithm": "sha256",
        "excluded": [HASHES_NAME],
        "files": {
            name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
            for name in sorted(names)
        },
    }


def _directories_identical(first: Path, second: Path) -> bool:
    first_entries = sorted(
        (path.name, path.is_file()) for path in first.iterdir()
    )
    second_entries = sorted(
        (path.name, path.is_file()) for path in second.iterdir()
    )
    return (
        first_entries == second_entries
        and all(is_file for _, is_file in first_entries)
        and all(
            (first / name).read_bytes() == (second / name).read_bytes()
            for name, _ in first_entries
        )
    )


def validate_packet(directory: str | Path) -> dict[str, Any]:
    """Fail closed unless the selected target packet is exact and self-bound."""

    root = Path(directory)
    _require(root.is_dir(), "selected LC7 packet is unavailable")
    entries = sorted(root.iterdir(), key=lambda path: path.name)
    _require(
        all(path.is_file() for path in entries),
        "selected LC7 packet contains a directory",
    )
    manifest = _read_json(root / MANIFEST_NAME)
    expected_names = _manifest_file_names(manifest)
    actual_names = {path.name for path in entries}
    _require(
        manifest.get("packet_file_count") == 36
        and len(manifest.get("states", [])) == 15
        and len(expected_names) == 36
        and actual_names == expected_names,
        "selected LC7 packet must contain exactly 36 manifest-declared files",
    )
    _require(
        manifest.get("selected_condition_id") == SELECTED_CONDITION_ID
        and manifest.get("default_condition_id") is None
        and {
            (state.get("width"), state.get("disclosure_state"))
            for state in manifest["states"]
        }
        == {
            (width, state)
            for width in TARGET_WIDTHS
            for state in DISCLOSURE_STATES
        },
        "selected LC7 packet state matrix differs",
    )
    hashes = _read_json(root / HASHES_NAME)
    hash_files = hashes.get("files")
    _require(
        hashes.get("excluded") == [HASHES_NAME]
        and isinstance(hash_files, dict)
        and HASHES_NAME not in hash_files
        and set(hash_files) == expected_names - {HASHES_NAME},
        "selected LC7 hash inventory is not self-excluding 35/36",
    )
    for name, digest in hash_files.items():
        _require(
            digest == hashlib.sha256((root / name).read_bytes()).hexdigest(),
            f"selected LC7 packet hash differs: {name}",
        )
    lock = _read_json(root / PRIOR_LOCK_NAME)
    _require(
        lock.get("format")
        == "blueprint-lens-lc7-selected-prior-packet-lock"
        and lock.get("entry_count") == 12
        and lock.get("sha256") == PRIOR_PACKET_SHA256,
        "selected LC7 prior-packet lock differs",
    )
    oracle = _read_json(root / ORACLE_NAME)
    oracle_checks = oracle.get("checks")
    _require(
        oracle.get("format") == "blueprint-lens-lc7-selected-responsive-oracle"
        and oracle.get("status") == "PASS"
        and isinstance(oracle_checks, dict)
        and len(oracle_checks) == 15
        and all(check.get("pass") is True for check in oracle_checks.values()),
        "selected LC7 geometry oracle does not pass",
    )
    return {
        "status": "PASS",
        "files": 36,
        "hashed_files": 35,
        "states": 15,
    }


def build_selected_artifacts(
    evidence_dir: str | Path,
    prior_packet_dir: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Atomically publish the deterministic 36-file selected A3 packet."""

    destination = Path(output_dir).resolve()
    _require(
        not destination.exists() or destination.is_dir(),
        "selected LC7 destination is not a directory",
    )
    for font_path in REQUIRED_FONT_PATHS:
        _require(
            Path(font_path).is_file(),
            f"selected LC7 font is unavailable: {font_path}",
        )
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(
            prefix=f".{destination.name}.staging-",
            dir=destination.parent,
        )
    )
    try:
        truth = load_lc7_visual_truth(evidence_dir)
        prior_root = Path(prior_packet_dir).resolve()
        prior_manifest = _read_json(
            prior_root / "lc7-adaptive-layout-manifest.json"
        )
        manifest = build_selected_manifest(
            truth,
            prior_manifest,
            prior_root,
        )
        prior_lock = build_prior_packet_lock(prior_root)
        scenes = [
            build_selected_scene(truth, width, state)
            for width in TARGET_WIDTHS
            for state in DISCLOSURE_STATES
        ]
        checks = {
            scene["scene_id"]: selected_scene_checks(scene, truth)
            for scene in scenes
        }
        _require(
            all(check["pass"] for check in checks.values()),
            "selected LC7 scene geometry/identity oracle failed",
        )
        overview_hashes_by_width = {
            str(width): sorted(
                {
                    scene["overview_sha256"]
                    for scene in scenes
                    if scene["width"] == width
                }
            )
            for width in TARGET_WIDTHS
        }
        _require(
            all(len(hashes) == 1 for hashes in overview_hashes_by_width.values()),
            "selected LC7 overview moved between disclosure states",
        )
        effect_paths: dict[str, Path] = {}
        for scene in scenes:
            state = next(
                state
                for state in manifest["states"]
                if state["state_id"] == scene["scene_id"]
            )
            svg_path = staging / state["effect_paths"]["svg"]
            png_path = staging / state["effect_paths"]["png"]
            svg_path.write_text(
                svg_for_selected_scene(scene, truth),
                encoding="utf-8",
                newline="\n",
            )
            png_for_selected_scene(scene, truth, png_path)
            effect_paths[scene["scene_id"]] = png_path
        (staging / BOARD_SVG_NAME).write_text(
            _responsive_board_svg(scenes, truth),
            encoding="utf-8",
            newline="\n",
        )
        _responsive_board_png(
            scenes,
            effect_paths,
            staging / BOARD_PNG_NAME,
        )
        (staging / MANIFEST_NAME).write_bytes(_json_bytes(manifest))
        oracle = {
            "format": "blueprint-lens-lc7-selected-responsive-oracle",
            "format_version": "1.0.0",
            "status": "PASS",
            "state_count": len(scenes),
            "overview_sha256_by_width": overview_hashes_by_width,
            "checks": checks,
        }
        (staging / ORACLE_NAME).write_bytes(_json_bytes(oracle))
        (staging / PRIOR_LOCK_NAME).write_bytes(_json_bytes(prior_lock))
        expected_names = _manifest_file_names(manifest)
        pre_hash_names = expected_names - {HASHES_NAME}
        _require(
            len(pre_hash_names) == 35
            and {path.name for path in staging.iterdir()} == pre_hash_names,
            "selected LC7 staging packet is partial",
        )
        (staging / HASHES_NAME).write_bytes(
            _json_bytes(_packet_hashes(staging, pre_hash_names))
        )
        validate_packet(staging)
        if destination.exists():
            _require(
                _directories_identical(staging, destination),
                "selected LC7 destination exists with different bytes",
            )
            shutil.rmtree(staging)
        else:
            staging.replace(destination)
    except LC7SelectedResponsiveError:
        if staging.exists():
            shutil.rmtree(staging)
        raise
    except Exception as error:
        if staging.exists():
            shutil.rmtree(staging)
        raise LC7SelectedResponsiveError(
            "selected LC7 target publication failed"
        ) from error
    return {name: destination / name for name in sorted(_manifest_file_names(manifest))}
