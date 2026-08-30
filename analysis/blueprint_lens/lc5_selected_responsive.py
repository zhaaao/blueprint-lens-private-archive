"""Selected responsive authoring packet for the LC5 Typed Portal Bridge."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
from typing import Any, Mapping

from PIL import Image

from blueprint_lens import lc5_visual as _base


class LC5SelectedResponsiveError(ValueError):
    """Raised when the selected LC5 responsive contract is violated."""


SELECTED_CONDITION_ID = "LC5_TYPED_PORTAL_BRIDGE"
TARGET_WIDTHS = (430, 480, 700)
MODES = {430: "single_column", 480: "stacked_detail", 700: "side_by_side"}
_SELECTION_STATUS = "OWNER_SELECTED__IMPLEMENTATION_CONDITION"
_FRONTIER = (
    "Frontier · depth 1 · macro, impure, latent, cross-Blueprint and dynamic dispatch excluded"
)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC5SelectedResponsiveError(message)


def _effect_stem(width: int) -> str:
    return f"lc5-typed-portal-bridge-selected-effect-{width}"


def _manifest_state(ledger: Mapping[str, Any], width: int) -> dict[str, Any]:
    stem = _effect_stem(width)
    return {
        "state_id": f"{SELECTED_CONDITION_ID}__W{width}",
        "condition_id": SELECTED_CONDITION_ID,
        "condition_label": "Typed Portal Bridge",
        "width": width,
        "responsive_mode": MODES[width],
        "information_set": _base.information_set(ledger),
        "effect_paths": {"svg": f"{stem}.svg", "png": f"{stem}.png"},
        "evidence_state": "authoring_design_target",
    }


def _expected_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    _base.validate_lc5_visual_ledger(ledger)
    return {
        "format": "blueprint-lens-lc5-selected-responsive-manifest",
        "schema_version": "1.0.0",
        "status": "TYPED_PORTAL_BRIDGE__THREE_WIDTH_AUTHORING_TARGETS",
        "selected_condition_id": SELECTED_CONDITION_ID,
        "selection_status": _SELECTION_STATUS,
        "default_condition_id": None,
        "profile_binding": deepcopy(dict(ledger["profile_binding"])),
        "target_widths_logical_px": list(TARGET_WIDTHS),
        "states": [_manifest_state(ledger, width) for width in TARGET_WIDTHS],
        "frontier_variants_deferred": True,
        "non_claims": [
            "owner selection is not a product default",
            "responsive effects are not Slate or UE-visible evidence",
            "no human comprehension, preference or scalability result is established",
        ],
    }


def build_selected_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Build the owner-selected, null-default three-width contract."""

    manifest = _expected_manifest(ledger)
    validate_selected_manifest(manifest, ledger)
    return manifest


def validate_selected_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    """Reject any drift from the selected responsive contract."""

    _require(
        manifest == _expected_manifest(ledger),
        "LC5 selected responsive manifest differs from the frozen contract",
    )


def structural_signature(scene: Mapping[str, Any]) -> str:
    """Return the frozen scene signature, excluding selection-only annotations."""

    value = deepcopy(dict(scene))
    value.pop("responsive_mode", None)
    value["commands"] = [
        command
        for command in value.get("commands", [])
        if command.get("id") not in {"selected.scheme", "selected.width"}
    ]
    payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _text(
    command_id: str,
    value: str,
    x: int,
    y: int,
    *,
    size: int = 9,
    weight: str = "regular",
    fill: str | None = None,
    **extra: Any,
) -> dict[str, Any]:
    return _base._text_command(
        command_id, value, x, y, size=size, weight=weight, fill=fill, **extra
    )


def _rect(
    command_id: str,
    bounds: tuple[int, int, int, int],
    *,
    fill: str,
    stroke: str | None = None,
    stroke_width: int = 0,
    radius: int = 0,
    **extra: Any,
) -> dict[str, Any]:
    return _base._rect_command(
        command_id,
        bounds,
        fill=fill,
        stroke=stroke,
        stroke_width=stroke_width,
        radius=radius,
        **extra,
    )


def _route(
    command_id: str,
    points: list[tuple[int, int]],
    *,
    stroke: str,
    line_style: str,
    marker: str,
    relation_id: str,
    label_command_id: str,
) -> dict[str, Any]:
    return _base._polyline_command(
        command_id,
        points,
        stroke=stroke,
        line_style=line_style,
        marker=marker,
        relation_id=relation_id,
        label_command_id=label_command_id,
    )


def _narrow_shared_commands(width: int, height: int) -> tuple[list[dict[str, Any]], dict[str, list[int]]]:
    tokens = _base.TOKENS
    bands = {
        "header": [24, 24, width - 48, 132],
        "plot": [24, 176, width - 48, height - 470],
        "frontier": [24, height - 270, width - 48, 118],
        "actions": [24, height - 132, width - 48, 108],
    }
    commands = [
        _rect("canvas.background", (0, 0, width, height), fill=tokens["background"]),
        _rect("shared.header", tuple(bands["header"]), fill=tokens["surface"], stroke="#2A3340", stroke_width=2, radius=10),
        _text("shared.question.1", "How does CalculateRecovery use CurrentHealth and Bonus", 36, 38, size=9, weight="bold"),
        _text("shared.question.2", "to produce NewHealth across the call boundary?", 36, 56, size=9, weight="bold"),
        _rect("shared.criterion.chip", (36, 86, 170, 34), fill="#26364B", stroke=tokens["enter"], stroke_width=1, radius=8),
        _text("shared.criterion", "NewHealth · criterion", 48, 95, size=12, weight="bold"),
        _text("shared.scope", "Static contextual slice · depth 1", 36, 132, size=9, fill=tokens["muted"]),
        _text("selected.scheme", "Selected · Typed Portal Bridge", 226, 96, size=9, weight="bold", fill=tokens["result"]),
        _text("selected.width", f"{width}px · {MODES[width].replace('_', ' ')}", 226, 120, size=8, fill=tokens["muted"]),
        _rect("shared.plot", tuple(bands["plot"]), fill=tokens["surface"], stroke="#2A3340", stroke_width=1, radius=10),
        _rect("shared.frontier", tuple(bands["frontier"]), fill="#241A22", stroke=tokens["frontier"], stroke_width=2, radius=10),
        _text("shared.frontier.title", "Frontier · depth 1", 40, height - 250, size=10, weight="bold", fill=tokens["frontier"]),
        _text("shared.frontier.reason.1", "macro, impure, latent, cross-Blueprint", 40, height - 224, size=9, fill=tokens["frontier"]),
        _text("shared.frontier.reason.2", "and dynamic dispatch excluded", 40, height - 204, size=9, fill=tokens["frontier"]),
        _text("shared.frontier.boundary.1", "Static occurrences; not runtime invocations.", 40, height - 190, size=9, fill=tokens["muted"]),
        _text("shared.frontier.boundary.2", "No runtime order is claimed.", 40, height - 170, size=9, fill=tokens["muted"]),
        _rect("shared.actions", tuple(bands["actions"]), fill=tokens["surface"], stroke="#2A3340", stroke_width=1, radius=10),
    ]
    action_width = (width - 84) // 2
    action_bounds = [
        (36, height - 116, action_width, 34),
        (48 + action_width, height - 116, action_width, 34),
        (36, height - 70, action_width, 34),
        (48 + action_width, height - 70, action_width, 34),
    ]
    for action, bounds in zip(_base._ACTIONS, action_bounds, strict=True):
        action_id = action["action_id"]
        commands.extend(
            (
                _rect(f"action.{action_id}.button", bounds, fill="#202834", stroke="#465466", stroke_width=1, radius=7, action_id=action_id),
                _text(f"action.{action_id}.label", action["label"], bounds[0] + 10, bounds[1] + 9, size=9, weight="bold", action_id=action_id),
            )
        )
    return commands, bands


def _semantic_unit(
    commands: list[dict[str, Any]],
    occurrence: Mapping[str, Any],
    command_id: str,
    position: tuple[int, int],
) -> dict[str, Any]:
    commands.append(
        _text(
            command_id,
            occurrence["reader_label"],
            *position,
            size=10,
            weight="bold",
            semantic_occurrence_id=occurrence["occurrence_id"],
        )
    )
    return {
        "occurrence_id": occurrence["occurrence_id"],
        "source_node_id": occurrence["source_node_id"],
        "role": occurrence["role"],
        "label": occurrence["reader_label"],
        "command_id": command_id,
    }


def _narrow_scene(ledger: Mapping[str, Any], width: int) -> dict[str, Any]:
    height = 1280 if width == 430 else 1200
    commands, bands = _narrow_shared_commands(width, height)
    tokens = _base.TOKENS
    roles = _base._occurrence_roles(ledger)
    left = 46 if width == 430 else 50
    node_x = left + 14
    node_w = width - 2 * node_x
    caller_region = (left, 196, width - 2 * left, 178)
    call_node = (node_x, 236, node_w, 116)
    portal_y = 400
    callee_region = (left, 424, width - 2 * left, 482)
    entry_node = (node_x, 460, node_w, 116)
    addition_node = (node_x + 34, 666, node_w - 68, 74)
    result_node = (node_x + 34, 812, node_w - 68, 80)
    commands.extend(
        (
            _rect("portal.caller.region", caller_region, fill=tokens["caller_fill"], stroke="#31506F", stroke_width=2, radius=10),
            _text("portal.caller.title", "CALLER · EventGraph", left + 12, 208, size=9, weight="bold", fill=tokens["enter"]),
            _rect("portal.call.node", call_node, fill="#1E2E42", stroke=tokens["enter"], stroke_width=2, radius=8),
            _text("portal.call.current", "CurrentHealth: int32", node_x + 14, 286, size=9, fill=tokens["argument"]),
            _text("portal.call.bonus", "Bonus: int32", node_x + 14, 308, size=9, fill=tokens["argument"]),
            _text("portal.call.result", "NewHealth: int32", node_x + 14, 330, size=9, fill=tokens["result"]),
            _base._polyline_command("portal.boundary", [(left + 8, portal_y), (width - left - 8, portal_y)], stroke="#7F8B99", stroke_width=2, line_style="dashed"),
            _text("portal.boundary.label", "STATIC TYPED PORTAL", 100, 382, size=8, weight="bold", fill=tokens["muted"]),
            _rect("portal.callee.region", callee_region, fill=tokens["callee_fill"], stroke="#584170", stroke_width=2, radius=10),
            _text("portal.callee.title", "CALLEE · CalculateRecovery", 98, 436, size=9, weight="bold", fill=tokens["result"]),
            _rect("portal.entry.node", entry_node, fill="#2A2339", stroke="#725795", stroke_width=2, radius=8),
            _text("portal.entry.current", "CurrentHealth: int32", node_x + 14, 510, size=9, fill=tokens["argument"]),
            _text("portal.entry.bonus", "Bonus: int32", node_x + 14, 532, size=9, fill=tokens["argument"]),
            _rect("portal.addition.node", addition_node, fill="#263126", stroke=tokens["internal"], stroke_width=2, radius=8),
            _rect("portal.result.node", result_node, fill="#32233D", stroke=tokens["result"], stroke_width=2, radius=8),
            _text("portal.result.type", "NewHealth: int32", result_node[0] + 14, 858, size=9, fill=tokens["result"]),
        )
    )
    semantic_units = [
        _semantic_unit(commands, roles["call"], "portal.unit.call", (node_x + 14, 252)),
        _semantic_unit(commands, roles["entry"], "portal.unit.entry", (node_x + 14, 476)),
        _semantic_unit(commands, roles["addition"], "portal.unit.addition", (addition_node[0] + 14, 690)),
        _semantic_unit(commands, roles["result"], "portal.unit.result", (result_node[0] + 14, 830)),
    ]

    call_left, call_top, call_width, call_height = call_node
    entry_left, entry_top, entry_width, entry_height = entry_node
    add_left, add_top, add_width, add_height = addition_node
    result_left, result_top, result_width, result_height = result_node
    lanes = [call_left + 30, call_left + call_width // 2, call_left + call_width - 30]
    relation_specs = [
        ([(lanes[0], call_top + call_height), (lanes[0], entry_top)], (lanes[0] + 8, 408)),
        (
            [(lanes[1], call_top + call_height), (lanes[1], entry_top)],
            ((228, 586) if width == 430 else (278, 586)),
        ),
        ([(lanes[2], call_top + call_height), (lanes[2], entry_top)], (width - left - 128, 574)),
        ([(entry_left + 54, entry_top + entry_height), (entry_left + 54, add_top), (add_left + 54, add_top)], (entry_left + 64, 600)),
        ([(entry_left + entry_width // 2, entry_top + entry_height), (entry_left + entry_width // 2, add_top)], (entry_left + entry_width // 2 + 8, 622)),
        ([(entry_left + entry_width, entry_top + 74), (width - left - 4, entry_top + 74), (width - left - 4, result_top + 38), (result_left + result_width, result_top + 38)], (width - left - 142, 632)),
        ([(add_left + add_width // 2, add_top + add_height), (add_left + add_width // 2, result_top)], (add_left + add_width // 2 + 10, 762)),
        ([(result_left, result_top + 32), (left + 4, result_top + 32), (left + 4, call_top + 76), (call_left, call_top + 76)], (left + 16, 930)),
        ([(result_left + result_width, result_top + 58), (width - left + 4, result_top + 58), (width - left + 4, call_top + 98), (call_left + call_width, call_top + 98)], (width - left - 140, 930)),
    ]
    relation_coverage: dict[str, dict[str, Any]] = {}
    semantic_command_for_occurrence = {
        roles["call"]["occurrence_id"]: "portal.call.node",
        roles["entry"]["occurrence_id"]: "portal.entry.node",
        roles["addition"]["occurrence_id"]: "portal.addition.node",
        roles["result"]["occurrence_id"]: "portal.result.node",
    }
    for index, (role, spec) in enumerate(zip(_base._relation_roles(ledger), relation_specs, strict=True)):
        relation, family, label = role
        points, label_position = spec
        route_id = f"selected.relation.{index}"
        label_id = f"{route_id}.label"
        colour, line_style, marker = _base._RELATION_STYLE[family]
        commands.extend(
            (
                _route(route_id, points, stroke=colour, line_style=line_style, marker=marker, relation_id=relation["relation_id"], label_command_id=label_id),
                _text(label_id, label, *label_position, size=7, fill=colour),
            )
        )
        relation_coverage[relation["relation_id"]] = {
            "family": family,
            "route_command_ids": [route_id],
            "label_command_id": label_id,
            "source_command_id": semantic_command_for_occurrence[relation["source_occurrence_id"]],
            "target_command_id": semantic_command_for_occurrence[relation["target_occurrence_id"]],
        }
    return {
        "format": "blueprint-lens-lc5-selected-responsive-scene",
        "schema_version": "1.0.0",
        "condition_id": SELECTED_CONDITION_ID,
        "condition_label": "Typed Portal Bridge",
        "responsive_mode": MODES[width],
        "canvas": {"width": width, "height": height},
        "bands": bands,
        "tokens": deepcopy(_base.TOKENS),
        "semantic_units": semantic_units,
        "relation_coverage": relation_coverage,
        "action_coverage": [deepcopy(item) for item in ledger["actions"]],
        "commands": commands,
        "regions": [
            {"id": "portal.caller", "bounds": list(caller_region)},
            {"id": "portal.callee", "bounds": list(callee_region)},
        ],
        "information_set": _base.information_set(ledger),
        "copy_claims": {
            "question": _base._QUESTION,
            "criterion": "NewHealth · criterion",
            "scope": "Static contextual slice · depth 1",
            "frontier": _FRONTIER,
            "static_only": True,
            "runtime_order_claimed": False,
        },
    }


def build_selected_scene(ledger: Mapping[str, Any], width: int) -> dict[str, Any]:
    """Build one selected Typed Portal Bridge scene at an authorized width."""

    _base.validate_lc5_visual_ledger(ledger)
    _require(width in TARGET_WIDTHS, "selected LC5 responsive width is not authorized")
    if width == 700:
        scene = _base.build_lc5_scene(ledger, SELECTED_CONDITION_ID, 700)
        scene["responsive_mode"] = MODES[width]
        scene["commands"].extend(
            (
                _text("selected.scheme", "Selected · Typed Portal Bridge", 436, 58, size=9, weight="bold", fill=_base.TOKENS["result"]),
                _text("selected.width", "700px · side by side", 436, 108, size=8, fill=_base.TOKENS["muted"]),
            )
        )
    else:
        scene = _narrow_scene(ledger, width)
    checks = validate_selected_scene(scene, ledger)
    _require(checks["pass"], f"LC5 selected scene failed: {checks}")
    return scene


def _point_on_rect_boundary(command: Mapping[str, Any], point: list[int]) -> bool:
    bounds = _base._command_bounds(command)
    if bounds is None:
        return False
    x, y = point
    left, top, right, bottom = bounds
    return (
        left <= x <= right
        and y in {top, bottom}
        or top <= y <= bottom
        and x in {left, right}
    )


def validate_selected_scene(
    scene: Mapping[str, Any], ledger: Mapping[str, Any]
) -> dict[str, Any]:
    """Validate selected semantic coverage, physical attachment and geometry."""

    width = scene.get("canvas", {}).get("width")
    if width == 700:
        base_scene = deepcopy(dict(scene))
        base_scene.pop("responsive_mode", None)
        base_scene["commands"] = [
            command
            for command in base_scene["commands"]
            if command.get("id") not in {"selected.scheme", "selected.width"}
        ]
        base_checks = _base.validate_lc5_scene(base_scene)
        header = _base._command_bounds(next(command for command in scene["commands"] if command["id"] == "shared.header"))
        header_text = [
            command for command in scene["commands"]
            if command.get("kind") == "text"
            and (command["id"].startswith("shared.") or command["id"].startswith("selected."))
            and "frontier" not in command["id"]
        ]
        header_text_containment = all(
            _bounds_inside(_base._command_bounds(command), header) for command in header_text
        )
        return {
            **base_checks,
            "canvas_containment": not base_checks["out_of_bounds_ids"],
            "text_text_overlap_count": len(base_checks["text_overlap_pairs"]),
            "route_text_collision_count": len(base_checks["route_text_collision_pairs"]),
            "physical_attachment_pass": not (
                base_checks["relation_attachment_errors"]
                or base_checks["outside_attachment_errors"]
                or base_checks["socket_attachment_errors"]
            ),
            "header_text_containment": header_text_containment,
            "frontier_text_containment": True,
            "action_text_containment": True,
            "pass": base_checks["pass"] and header_text_containment,
        }
    commands = scene.get("commands", [])
    commands_by_id = {item.get("id"): item for item in commands if isinstance(item, Mapping)}
    height = scene.get("canvas", {}).get("height")
    out_of_bounds: list[str] = []
    for command in commands:
        bounds = _base._paint_bounds(command)
        if (
            bounds is None
            or not isinstance(width, int)
            or not isinstance(height, int)
            or bounds[0] < 0
            or bounds[1] < 0
            or bounds[2] > width
            or bounds[3] > height
        ):
            out_of_bounds.append(str(command.get("id")))
    text_commands = [item for item in commands if item.get("kind") == "text"]
    text_overlaps: list[list[str]] = []
    for index, first in enumerate(text_commands):
        first_bounds = _base._command_bounds(first)
        for second in text_commands[index + 1 :]:
            second_bounds = _base._command_bounds(second)
            if (
                first_bounds is not None
                and second_bounds is not None
                and _base._rectangles_overlap(first_bounds, second_bounds)
            ):
                text_overlaps.append([first["id"], second["id"]])
    route_collisions: list[list[str]] = []
    for route in (item for item in commands if item.get("kind") == "polyline" and item.get("relation_id")):
        for text_command in text_commands:
            bounds = _base._command_bounds(text_command)
            if bounds is None:
                continue
            if any(
                _base._segment_intersects_rect(tuple(start), tuple(end), bounds)
                for start, end in zip(route["points"], route["points"][1:])
            ):
                route_collisions.append([route["id"], text_command["id"]])
    attachment_errors: list[str] = []
    for relation_id, coverage in scene.get("relation_coverage", {}).items():
        route = commands_by_id.get(coverage["route_command_ids"][0], {})
        source = commands_by_id.get(coverage["source_command_id"], {})
        target = commands_by_id.get(coverage["target_command_id"], {})
        points = route.get("points", [])
        if (
            not points
            or not _point_on_rect_boundary(source, points[0])
            or not _point_on_rect_boundary(target, points[-1])
        ):
            attachment_errors.append(relation_id)
    expected_occurrences = {item["occurrence_id"] for item in ledger["occurrences"]}
    actual_occurrences = {item["occurrence_id"] for item in scene.get("semantic_units", [])}
    expected_relations = {item["relation_id"] for item in ledger["relations"]}
    actual_relations = set(scene.get("relation_coverage", {}))
    expected_actions = {item["action_id"] for item in ledger["actions"]}
    actual_actions = {item["action_id"] for item in scene.get("action_coverage", [])}
    header_text_containment = _container_text_containment(
        commands_by_id,
        "shared.header",
        (
            "shared.question",
            "shared.criterion",
            "shared.scope",
            "selected.scheme",
            "selected.width",
        ),
    )
    frontier_text_containment = _container_text_containment(
        commands_by_id, "shared.frontier", ("shared.frontier",)
    )
    action_text_containment = all(
        _bounds_inside(
            _base._command_bounds(command),
            _base._command_bounds(commands_by_id[command["id"].replace(".label", ".button")]),
        )
        for command in text_commands
        if command["id"].startswith("action.") and command["id"].endswith(".label")
    )
    errors = {
        "out_of_bounds_ids": out_of_bounds,
        "text_overlap_pairs": text_overlaps,
        "route_text_collision_pairs": route_collisions,
        "physical_attachment_errors": attachment_errors,
        "semantic_inventory_match": actual_occurrences == expected_occurrences,
        "relation_inventory_match": actual_relations == expected_relations,
        "action_inventory_match": actual_actions == expected_actions,
        "information_set_match": scene.get("information_set") == _base.information_set(ledger),
        "copy_claims_match": scene.get("copy_claims", {}).get("frontier") == _FRONTIER,
        "header_text_containment": header_text_containment,
        "frontier_text_containment": frontier_text_containment,
        "action_text_containment": action_text_containment,
    }
    return {
        **errors,
        "canvas_containment": not out_of_bounds,
        "text_text_overlap_count": len(text_overlaps),
        "route_text_collision_count": len(route_collisions),
        "physical_attachment_pass": not attachment_errors,
        "pass": (
            not out_of_bounds
            and not text_overlaps
            and not route_collisions
            and not attachment_errors
            and header_text_containment
            and frontier_text_containment
            and action_text_containment
            and all(
                errors[key]
                for key in (
                    "semantic_inventory_match",
                    "relation_inventory_match",
                    "action_inventory_match",
                    "information_set_match",
                    "copy_claims_match",
                )
            )
        ),
    }


def _bounds_inside(inner: list[int] | None, outer: list[int] | None) -> bool:
    return bool(
        inner
        and outer
        and outer[0] <= inner[0] <= inner[2] <= outer[2]
        and outer[1] <= inner[1] <= inner[3] <= outer[3]
    )


def _container_text_containment(
    commands_by_id: Mapping[str, Mapping[str, Any]],
    container_id: str,
    prefixes: tuple[str, ...],
) -> bool:
    container_bounds = _base._command_bounds(commands_by_id[container_id])
    return all(
        _bounds_inside(_base._command_bounds(command), container_bounds)
        for command_id, command in commands_by_id.items()
        if command.get("kind") == "text"
        and any(command_id.startswith(prefix) for prefix in prefixes)
    )


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.write_bytes(_json_bytes(value))


def _prior_packet_lock() -> dict[str, Any]:
    artifact_dir = Path(__file__).resolve().parents[2] / "artifacts/r1/lc5-visual-candidates"
    inventory_path = artifact_dir / "lc5-visual-hashes.v1.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    expected = dict(inventory["files"])
    expected["lc5-visual-hashes.v1.json"] = hashlib.sha256(
        inventory_path.read_bytes()
    ).hexdigest()
    actual = {
        name: hashlib.sha256((artifact_dir / name).read_bytes()).hexdigest()
        for name in expected
    }
    _require(actual == expected, "prior LC5 five-condition packet differs")
    return {
        "format": "blueprint-lens-lc5-selected-responsive-prior-lock",
        "schema_version": "1.0.0",
        "status": "PASS__FOURTEEN_FILE_PACKET_BYTE_PRESERVED",
        "prior_file_count": 14,
        "prior_inventory_path": "artifacts/r1/lc5-visual-candidates/lc5-visual-hashes.v1.json",
        "prior_inventory_sha256": expected["lc5-visual-hashes.v1.json"],
    }


def _responsive_board(paths: list[Path], output: Path) -> None:
    images = [Image.open(path).convert("RGB") for path in paths]
    gap = 24
    width = max(image.width for image in images) + 2 * gap
    height = sum(image.height for image in images) + gap * (len(images) + 1)
    board = Image.new("RGB", (width, height), _base.TOKENS["background"])
    y = gap
    for image in images:
        x = (width - image.width) // 2
        board.paste(image, (x, y))
        y += image.height + gap
    board.save(output, format="PNG", optimize=False, compress_level=9)


def _install_evidence(staging: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for source in sorted(staging.iterdir(), key=lambda path: path.name):
        target = destination / source.name
        if target.exists():
            _require(
                target.is_file() and target.read_bytes() == source.read_bytes(),
                f"refusing to overwrite different LC5 selected evidence: {target}",
            )
            continue
        source.replace(target)


def build_selected_artifacts(
    contextual_path: str | Path,
    readiness_path: str | Path,
    fixture_path: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Publish only the owner-selected three-width responsive packet."""

    ledger = _base.load_lc5_visual_ledger(
        contextual_path, readiness_path, fixture_path
    )
    manifest = build_selected_manifest(ledger)
    destination = Path(output_dir)
    staging = Path(tempfile.mkdtemp(prefix="lc5-selected-", dir=destination.parent))
    try:
        manifest_path = staging / "lc5-typed-portal-bridge-selected-manifest.v1.json"
        _write_json(manifest_path, manifest)
        states = []
        png_paths: list[Path] = []
        for state in manifest["states"]:
            scene = build_selected_scene(ledger, state["width"])
            svg_path = staging / state["effect_paths"]["svg"]
            png_path = staging / state["effect_paths"]["png"]
            svg_path.write_text(_base._svg_for_scene(scene), encoding="utf-8", newline="\n")
            _base._png_for_scene(scene, png_path)
            states.append(
                {
                    "state_id": state["state_id"],
                    "width": state["width"],
                    "responsive_mode": state["responsive_mode"],
                    "checks": validate_selected_scene(scene, ledger),
                }
            )
            png_paths.append(png_path)
        oracle_path = staging / "lc5-typed-portal-bridge-selected-geometry-oracle.v1.json"
        _write_json(
            oracle_path,
            {
                "format": "blueprint-lens-lc5-selected-responsive-geometry-oracle",
                "schema_version": "1.0.0",
                "states": states,
            },
        )
        prior_lock_path = staging / "lc5-typed-portal-bridge-selected-prior-packet-lock.json"
        _write_json(prior_lock_path, _prior_packet_lock())
        board_path = staging / "lc5-typed-portal-bridge-selected-responsive-board.png"
        _responsive_board(png_paths, board_path)
        hashed_paths = sorted(staging.iterdir(), key=lambda path: path.name)
        hashes_path = staging / "lc5-typed-portal-bridge-selected-hashes.v1.json"
        _write_json(
            hashes_path,
            {
                "format": "blueprint-lens-lc5-selected-responsive-hashes",
                "schema_version": "1.0.0",
                "file_count": len(hashed_paths),
                "files": {
                    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in hashed_paths
                },
            },
        )
        _install_evidence(staging, destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return {
        "manifest": destination / "lc5-typed-portal-bridge-selected-manifest.v1.json",
        "oracle": destination / "lc5-typed-portal-bridge-selected-geometry-oracle.v1.json",
        "prior_packet_lock": destination / "lc5-typed-portal-bridge-selected-prior-packet-lock.json",
        "hashes": destination / "lc5-typed-portal-bridge-selected-hashes.v1.json",
        "board": destination / "lc5-typed-portal-bridge-selected-responsive-board.png",
    }
