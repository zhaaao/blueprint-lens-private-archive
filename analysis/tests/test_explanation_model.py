from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis"))

from blueprint_lens import (  # noqa: E402
    CompleteDagRelationProjection,
    CompleteDagUnitProjection,
    ExplanationModelError,
    build_complete_cyclic_explanation,
    build_complete_dag_explanation,
    build_linear_execution_explanation,
    build_probe_explanation,
    canonical_explanation_bytes,
    load_blueprint_lens_v1,
    validate_explanation_model,
)

IR = ROOT / "fixtures/v1/BP_SlicingProbe.v1.json"
SLICE = ROOT / "artifacts/v1/BP_SlicingProbe.execution.slice.v1.json"
ASSET = ROOT / "unreal/BlueprintLensProbe/Content/Probe/BP_SlicingProbe.uasset"
SCHEMA = ROOT / "schemas/blueprint-lens-explanation-v1.schema.json"

EXPECTED_LANES = [
    ("criterion", "populated", ["unit.criterion.set-health"]),
    (
        "control",
        "populated",
        [
            "unit.control.event-begin-play",
            "unit.control.sequence",
            "unit.control.branch",
        ],
    ),
    ("predicate", "populated", ["unit.predicate.health-positive"]),
    ("value", "populated", ["unit.value.health-minus-ten"]),
    ("consequence", "not_enabled", []),
    ("boundary", "empty", []),
]


class ExplanationModelTests(unittest.TestCase):
    def build_clean_model(self) -> dict[str, Any]:
        return build_probe_explanation(IR, SLICE, ASSET)

    def add_exact_endpoint_ledgers(self, model: dict[str, Any]) -> None:
        ir_value = json.loads(IR.read_text(encoding="utf-8"))
        graph = next(
            graph
            for graph in ir_value["blueprint"]["graphs"]
            if graph["id"] == model["source"]["graph_id"]
        )
        edges_by_id = {edge["id"]: edge for edge in graph["edges"]}
        pins_by_id = {
            pin["id"]: pin
            for node in graph["nodes"]
            for pin in node["pins"]
        }
        for relation in model["relations"]:
            relation["source_edge_endpoints"] = [
                {
                    "source_edge_id": edge_id,
                    "source_node_id": edges_by_id[edge_id]["source_node_id"],
                    "source_pin_id": edges_by_id[edge_id]["source_pin_id"],
                    "source_port_label": pins_by_id[
                        edges_by_id[edge_id]["source_pin_id"]
                    ]["name"],
                    "target_node_id": edges_by_id[edge_id]["target_node_id"],
                    "target_pin_id": edges_by_id[edge_id]["target_pin_id"],
                    "target_port_label": pins_by_id[
                        edges_by_id[edge_id]["target_pin_id"]
                    ]["name"],
                }
                for edge_id in relation["source_edge_ids"]
            ]

    def test_probe_builder_emits_fixed_semantic_lanes(self) -> None:
        model = self.build_clean_model()
        validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)
        self.assertEqual(model["format"], "blueprint-lens-explanation")
        self.assertEqual(model["schema_version"], "1.0.0")
        self.assertEqual(
            [
                (lane["role"], lane["state"], lane["unit_ids"])
                for lane in model["lanes"]
            ],
            EXPECTED_LANES,
        )
        self.assertEqual(model["criterion_unit_id"], "unit.criterion.set-health")
        self.assertEqual(model["counts"], {
            "lanes": 6,
            "units": 6,
            "relations": 5,
            "source_nodes": 8,
            "source_edges": 7,
        })

    def test_accepts_exact_relation_endpoint_ledger(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)

        validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_missing_relation_endpoint_entry(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        model["relations"][3]["source_edge_endpoints"].pop()

        with self.assertRaisesRegex(
            ExplanationModelError,
            "source_edge_endpoints must bijectively match source_edge_ids",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_duplicate_relation_endpoint_entry(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        endpoints = model["relations"][3]["source_edge_endpoints"]
        endpoints[1] = deepcopy(endpoints[0])

        with self.assertRaisesRegex(
            ExplanationModelError,
            "source_edge_endpoints must bijectively match source_edge_ids",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_explicit_empty_endpoint_ledger_is_not_treated_as_absent(self) -> None:
        model = self.build_clean_model()
        model["relations"][0]["source_edge_endpoints"] = []

        with self.assertRaisesRegex(
            ExplanationModelError,
            "source_edge_endpoints must bijectively match source_edge_ids",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_dangling_relation_endpoint_pin(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        model["relations"][0]["source_edge_endpoints"][0][
            "source_pin_id"
        ] = "pin.missing"

        with self.assertRaisesRegex(
            ExplanationModelError,
            "endpoint source pin does not resolve in IR: pin.missing",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_swapped_relation_endpoint_pins(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        endpoint = model["relations"][0]["source_edge_endpoints"][0]
        endpoint["source_pin_id"], endpoint["target_pin_id"] = (
            endpoint["target_pin_id"],
            endpoint["source_pin_id"],
        )

        with self.assertRaisesRegex(
            ExplanationModelError,
            "endpoint provenance disagrees with IR edge",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_relation_endpoint_node_mismatch(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        endpoint = model["relations"][0]["source_edge_endpoints"][0]
        endpoint["source_node_id"] = endpoint["target_node_id"]

        with self.assertRaisesRegex(
            ExplanationModelError,
            "endpoint provenance disagrees with IR edge",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_relation_unit_direction_swap(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        relation = model["relations"][0]
        relation["source_unit_id"], relation["target_unit_id"] = (
            relation["target_unit_id"],
            relation["source_unit_id"],
        )

        with self.assertRaisesRegex(
            ExplanationModelError,
            "relation source edge disagrees with endpoints",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_relation_endpoint_port_label_mismatch(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        model["relations"][0]["source_edge_endpoints"][0][
            "source_port_label"
        ] = "edited label"

        with self.assertRaisesRegex(
            ExplanationModelError,
            "endpoint port label disagrees with IR pin",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_endpoint_ledger_validation_ignores_serialization_order(self) -> None:
        model = self.build_clean_model()
        self.add_exact_endpoint_ledgers(model)
        model["relations"].reverse()
        for relation in model["relations"]:
            relation["source_edge_endpoints"].reverse()

        validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_duplicate_source_node_ownership(self) -> None:
        model = self.build_clean_model()
        duplicate = deepcopy(model["units"][0]["source_references"][0])
        model["units"][1]["source_references"].append(duplicate)
        with self.assertRaisesRegex(
            ExplanationModelError, "source node ownership must not overlap"
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_dangling_relation_endpoint(self) -> None:
        model = self.build_clean_model()
        model["relations"][0]["target_unit_id"] = "unit.missing"
        with self.assertRaisesRegex(ExplanationModelError, "relation endpoint"):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_duplicate_source_edge_ownership(self) -> None:
        model = self.build_clean_model()
        model["relations"][1]["source_edge_ids"].append(
            model["relations"][0]["source_edge_ids"][0]
        )
        with self.assertRaisesRegex(
            ExplanationModelError, "source edge ownership must not overlap"
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_unknown_role_without_coercion(self) -> None:
        model = self.build_clean_model()
        model["lanes"][0]["role"] = "mystery"
        with self.assertRaises(ExplanationModelError):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_populated_lane_changed_to_empty(self) -> None:
        model = self.build_clean_model()
        model["lanes"][0]["state"] = "empty"
        with self.assertRaisesRegex(
            ExplanationModelError,
            "criterion lane must be populated with units and no empty message",
        ):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_consequence_lane_state_or_occupancy_inconsistency(
        self,
    ) -> None:
        mutations = {
            "state": lambda lane: lane.__setitem__("state", "empty"),
            "occupancy": lambda lane: lane["unit_ids"].append("unit.missing"),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                model = self.build_clean_model()
                mutate(model["lanes"][4])
                with self.assertRaisesRegex(
                    ExplanationModelError,
                    "consequence lane must be not_enabled with no units "
                    "and the backward-only message",
                ):
                    validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_boundary_lane_state_or_occupancy_inconsistency(
        self,
    ) -> None:
        mutations = {
            "state": lambda lane: lane.__setitem__("state", "not_enabled"),
            "occupancy": lambda lane: lane["unit_ids"].append("unit.missing"),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                model = self.build_clean_model()
                mutate(model["lanes"][5])
                with self.assertRaisesRegex(
                    ExplanationModelError,
                    "boundary lane must be empty with no units "
                    "and the supported-constructs message",
                ):
                    validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_accepts_empty_control_and_populated_boundary_lanes(self) -> None:
        model = self.build_clean_model()
        control_units = [
            unit for unit in model["units"] if unit["role"] == "control"
        ]
        control_ids = [unit["id"] for unit in control_units]
        for unit in control_units:
            unit["role"] = "boundary"
        model["lanes"][1] = {
            "role": "control",
            "state": "empty",
            "unit_ids": [],
            "empty_message": "No control facts in this explanation",
        }
        model["lanes"][5] = {
            "role": "boundary",
            "state": "populated",
            "unit_ids": control_ids,
            "empty_message": "",
        }

        validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_unknown_semantic_status_without_coercion(self) -> None:
        model = self.build_clean_model()
        model["units"][0]["semantic_status"] = "mystery"
        with self.assertRaises(ExplanationModelError):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_unknown_relation_kind_without_coercion(self) -> None:
        model = self.build_clean_model()
        model["relations"][0]["kind"] = "mystery"
        with self.assertRaises(ExplanationModelError):
            validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_rejects_wrong_ir_slice_or_asset_hash(self) -> None:
        fields = ("ir_sha256", "slice_sha256", "blueprint_package_sha256")
        for field in fields:
            with self.subTest(field=field):
                model = self.build_clean_model()
                model["source"][field] = "0" * 64
                with self.assertRaisesRegex(
                    ExplanationModelError, "SHA-256 mismatch"
                ):
                    validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def test_canonical_serialization_is_byte_deterministic(self) -> None:
        first = canonical_explanation_bytes(self.build_clean_model())
        second = canonical_explanation_bytes(self.build_clean_model())
        self.assertEqual(first, second)
        self.assertEqual(
            hashlib.sha256(first).hexdigest(),
            hashlib.sha256(second).hexdigest(),
        )

    def complete_projection_specs(
        self,
        ir_path: Path,
        slice_path: Path,
    ) -> tuple[dict[str, CompleteDagUnitProjection], dict[str, CompleteDagRelationProjection]]:
        document = load_blueprint_lens_v1(ir_path)
        slice_value = json.loads(slice_path.read_text(encoding="utf-8"))
        graph = next(
            graph for graph in document.graphs if graph.id == slice_value["graph_id"]
        )
        selected_nodes = {
            node.id: node for node in graph.nodes if node.id in slice_value["node_ids"]
        }
        selected_edges = {
            edge.id: edge for edge in graph.edges if edge.id in slice_value["edge_ids"]
        }
        criterion_id = slice_value["criterion"]["node_id"]
        units = {
            node_id: CompleteDagUnitProjection(
                role="criterion" if node_id == criterion_id else "control",
                kind="node",
                title=node.title or node.class_path,
            )
            for node_id, node in selected_nodes.items()
        }
        relations = {
            edge_id: CompleteDagRelationProjection(
                kind=(
                    "execution_predecessor"
                    if edge.kind == "execution"
                    else "provides_value"
                ),
                label=edge_id,
            )
            for edge_id, edge in selected_edges.items()
        }
        return units, relations

    def write_complete_cycle(self, directory: Path) -> tuple[Path, Path, dict[str, Any]]:
        ir_value = json.loads(IR.read_text(encoding="utf-8"))
        slice_value = json.loads(SLICE.read_text(encoding="utf-8"))
        graph = next(
            graph
            for graph in ir_value["blueprint"]["graphs"]
            if graph["id"] == slice_value["graph_id"]
        )
        nodes = {node["id"]: node for node in graph["nodes"]}
        criterion_id = slice_value["criterion"]["node_id"]
        branch_id = next(
            node_id
            for node_id, node in nodes.items()
            if node["class"].endswith("K2Node_IfThenElse")
        )
        source_pin = next(
            pin
            for pin in nodes[criterion_id]["pins"]
            if pin["kind"] == "execution" and pin["direction"] == "output"
        )
        target_pin = next(
            pin
            for pin in nodes[branch_id]["pins"]
            if pin["kind"] == "execution" and pin["direction"] == "input"
        )
        return_edge_id = (
            f'{graph["id"]}::edge::{source_pin["id"]}->{target_pin["id"]}'
        )
        graph["edges"].append(
            {
                "id": return_edge_id,
                "kind": "execution",
                "source_node_id": criterion_id,
                "source_pin_id": source_pin["id"],
                "target_node_id": branch_id,
                "target_pin_id": target_pin["id"],
                "direction_is_valid": True,
            }
        )
        ir_value["counts"]["edges"] += 1
        slice_value["edge_ids"].append(return_edge_id)
        slice_value["counts"]["edges"] += 1
        ir_path = directory / "cycle.ir.json"
        slice_path = directory / "cycle.slice.json"
        ir_path.write_text(json.dumps(ir_value, indent=2) + "\n", encoding="utf-8")
        slice_path.write_text(
            json.dumps(slice_value, indent=2) + "\n", encoding="utf-8"
        )
        return ir_path, slice_path, {
            "criterion_id": criterion_id,
            "branch_id": branch_id,
            "return_edge_id": return_edge_id,
        }

    def test_complete_dag_canonical_bytes_remain_locked(self) -> None:
        units, relations = self.complete_projection_specs(IR, SLICE)
        model = build_complete_dag_explanation(
            IR,
            SLICE,
            ASSET,
            units,
            relations,
            question="Why?",
        )
        # The model intentionally records the paths supplied by its caller.  Keep
        # this byte lock independent of the repository/worktree location while
        # retaining every semantic field and source digest in the oracle.
        model["source"]["ir_path"] = IR.relative_to(ROOT).as_posix()
        model["source"]["slice_path"] = SLICE.relative_to(ROOT).as_posix()
        self.assertEqual(
            hashlib.sha256(canonical_explanation_bytes(model)).hexdigest(),
            "1f7c261a58edff5ba61ed7ff1930a3e9d498e7e01a43de308b911fcbd9614be2",
        )

    def test_cyclic_entry_accepts_resolved_group_while_dag_rejects_cycle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            ir_path, slice_path, anchors = self.write_complete_cycle(
                Path(temporary_directory)
            )
            units, relations = self.complete_projection_specs(ir_path, slice_path)
            with self.assertRaisesRegex(
                ExplanationModelError,
                "complete DAG projection contains a cycle",
            ):
                build_complete_dag_explanation(
                    ir_path,
                    slice_path,
                    ASSET,
                    units,
                    relations,
                    question="Why?",
                )

            document = load_blueprint_lens_v1(ir_path)
            graph = next(graph for graph in document.graphs if graph.id.endswith(":EventGraph"))
            nodes = {node.id: node for node in graph.nodes}
            branch_unit = f'unit.control.{nodes[anchors["branch_id"]].native_guid.lower()}'
            criterion_unit = f'unit.criterion.{nodes[anchors["criterion_id"]].native_guid.lower()}'
            selected_execution = [
                edge
                for edge in graph.edges
                if edge.id in relations
                and edge.kind == "execution"
                and {edge.source_node_id, edge.target_node_id}
                == {anchors["branch_id"], anchors["criterion_id"]}
            ]
            relation_ids = [
                "relation.execution_predecessor."
                + hashlib.sha256(edge.id.encode("utf-8")).hexdigest()[:16]
                for edge in sorted(selected_execution, key=lambda edge: edge.id)
            ]
            group = {
                "id": "group.synthetic-cycle",
                "kind": "scc",
                "title": "",
                "ordered_unit_ids": [branch_unit, criterion_unit],
                "ordered_relation_ids": relation_ids,
                "entry_unit_id": branch_unit,
                "exit_unit_id": criterion_unit,
                "parent_group_id": None,
                "entered_by": None,
                "member_count": 2,
                "projection_status": "STRUCTURAL_ONLY",
                "diagnostic_code": "",
                "claim_evidence": [],
            }
            model = build_complete_cyclic_explanation(
                ir_path,
                slice_path,
                ASSET,
                units,
                relations,
                question="Why?",
                groups=[group],
            )
            self.assertEqual(model["groups"], [group])

    def write_linear_slice(
        self,
        directory: Path,
        *,
        reverse_arrays: bool = False,
    ) -> Path:
        slice_value = json.loads(SLICE.read_text(encoding="utf-8"))
        selected_guids = {
            "0a170a17-4a0e-e306-76df-ac876e3d5f01",
            "ff067901-4f51-b8c2-4b11-cda9a6bde21e",
            "cbbd5a81-42fa-5ea1-6363-c09cebb1efb2",
            "63180c9c-4c18-1a57-ca71-60b47521033c",
        }
        selected_node_ids = [
            node_id
            for node_id in slice_value["node_ids"]
            if any(guid in node_id for guid in selected_guids)
        ]
        selected_edges = [
            edge_id
            for edge_id in slice_value["edge_ids"]
            if all(
                any(guid in endpoint for guid in selected_guids)
                for endpoint in (
                    edge_id.split("::edge::", 1)[1].split("->", 1)[0],
                    edge_id.split("->", 1)[1],
                )
            )
        ]
        self.assertEqual(len(selected_node_ids), 4)
        self.assertEqual(len(selected_edges), 3)
        if reverse_arrays:
            selected_node_ids.reverse()
            selected_edges.reverse()
        slice_value["node_ids"] = selected_node_ids
        slice_value["edge_ids"] = selected_edges
        slice_value["inclusion_reasons"] = {
            node_id: slice_value["inclusion_reasons"][node_id]
            for node_id in selected_node_ids
        }
        slice_value["boundaries"] = []
        slice_value["counts"] = {
            "nodes": len(selected_node_ids),
            "edges": len(selected_edges),
        }
        output = directory / (
            "linear-reversed.json" if reverse_arrays else "linear.json"
        )
        output.write_text(
            json.dumps(slice_value, indent=2) + "\n",
            encoding="utf-8",
        )
        return output

    def test_linear_builder_projects_relation_derived_chain(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            slice_path = self.write_linear_slice(Path(temporary_directory))
            model = build_linear_execution_explanation(
                IR,
                slice_path,
                ASSET,
            )
            validate_explanation_model(
                model,
                IR,
                slice_path,
                ASSET,
                SCHEMA,
            )

        self.assertEqual(model["counts"]["units"], 4)
        self.assertEqual(model["counts"]["relations"], 3)
        self.assertEqual(
            [
                unit["source_references"][0]["native_node_guid"]
                for unit in model["units"]
            ],
            [
                "0a170a17-4a0e-e306-76df-ac876e3d5f01",
                "ff067901-4f51-b8c2-4b11-cda9a6bde21e",
                "cbbd5a81-42fa-5ea1-6363-c09cebb1efb2",
                "63180c9c-4c18-1a57-ca71-60b47521033c",
            ],
        )
        self.assertTrue(
            all(
                relation["kind"] == "execution_predecessor"
                for relation in model["relations"]
            )
        )
        self.assertEqual(
            (model["lanes"][2]["state"], model["lanes"][2]["unit_ids"]),
            ("empty", []),
        )
        self.assertEqual(
            (model["lanes"][3]["state"], model["lanes"][3]["unit_ids"]),
            ("empty", []),
        )

    def test_linear_builder_ignores_input_array_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            ordered = build_linear_execution_explanation(
                IR,
                self.write_linear_slice(directory),
                ASSET,
            )
            reversed_model = build_linear_execution_explanation(
                IR,
                self.write_linear_slice(directory, reverse_arrays=True),
                ASSET,
            )
        self.assertEqual(ordered["units"], reversed_model["units"])
        self.assertEqual(ordered["relations"], reversed_model["relations"])
        self.assertEqual(ordered["criterion_unit_id"], reversed_model["criterion_unit_id"])

    def test_linear_builder_rejects_data_attachments(self) -> None:
        with self.assertRaisesRegex(
            ExplanationModelError,
            "does not accept data attachments",
        ):
            build_linear_execution_explanation(IR, SLICE, ASSET)

    def test_cli_writes_the_validated_canonical_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "nested" / "explanation.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "analysis/build_probe_explanation.py"),
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            payload = output.read_bytes()
            expected = canonical_explanation_bytes(self.build_clean_model())
            self.assertEqual(payload, expected)
            digest = hashlib.sha256(payload).hexdigest().upper()
            self.assertEqual(
                result.stdout.strip(),
                f'EXPLANATION_MODEL_SUCCESS output="{output.resolve()}" '
                f"units=6 relations=5 source_nodes=8 source_edges=7 "
                f"sha256={digest}",
            )


if __name__ == "__main__":
    unittest.main()
