from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis"))

from blueprint_lens import (  # noqa: E402
    BlueprintDocument,
    Edge,
    Graph,
    Node,
    Pin,
    compute_execution_slice,
    load_raw_probe,
)


FIXTURE = ROOT / "fixtures" / "raw" / "BP_SlicingProbe.raw-0.2.json"
GROUND_TRUTH = (
    ROOT
    / "fixtures"
    / "ground_truth"
    / "BP_SlicingProbe.execution.candidate-0.1.json"
)
LC7_RAW = (
    ROOT
    / "artifacts"
    / "r1"
    / "lc7-static-scc-truth"
    / "run1"
    / "BP_LC7_StaticSCC.raw-0.2.json"
)
LC7_SOURCE = LC7_RAW.with_name("BP_LC7_StaticSCC.scc-source.json")


class ExecutionSliceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = load_raw_probe(FIXTURE)
        cls.ground_truth = json.loads(GROUND_TRUTH.read_text(encoding="utf-8"))

    def test_probe_matches_reviewed_ground_truth(self) -> None:
        expected = self.ground_truth["expected"]
        result = compute_execution_slice(
            self.document, self.ground_truth["criterion"]["node_id"]
        )
        self.assertEqual(set(result.node_ids), set(expected["node_ids"]))
        self.assertEqual(set(result.edge_ids), set(expected["edge_ids"]))

    def test_explicit_execution_cycle_terminates(self) -> None:
        graph_id = self.ground_truth["criterion"]["graph_id"]
        graph = next(graph for graph in self.document.graphs if graph.id == graph_id)
        criterion = self.ground_truth["criterion"]["node_id"]
        branch = next(
            node.id for node in graph.nodes if node.class_path.endswith("K2Node_IfThenElse")
        )
        criterion_node = next(node for node in graph.nodes if node.id == criterion)
        branch_node = next(node for node in graph.nodes if node.id == branch)
        source_pin = next(
            pin
            for pin in criterion_node.pins
            if pin.kind == "execution" and pin.direction == "output"
        )
        target_pin = next(
            pin
            for pin in branch_node.pins
            if pin.kind == "execution" and pin.direction == "input"
        )
        cycle_edge = Edge(
            id=f"{graph.id}::test-cycle",
            graph_id=graph.id,
            kind="execution",
            source_node_id=criterion,
            source_pin_id=source_pin.id,
            target_node_id=branch,
            target_pin_id=target_pin.id,
        )
        cyclic_graph = replace(graph, edges=graph.edges + (cycle_edge,))
        cyclic_document = replace(
            self.document,
            graphs=tuple(
                cyclic_graph if candidate.id == graph.id else candidate
                for candidate in self.document.graphs
            ),
        )
        result = compute_execution_slice(cyclic_document, criterion)
        self.assertEqual(len(result.node_ids), len(set(result.node_ids)))
        self.assertIn(cycle_edge.id, result.edge_ids)

    def test_real_lc7_cycle_terminates_with_complete_eight_by_eight_slice(self) -> None:
        source = json.loads(LC7_SOURCE.read_text(encoding="utf-8"))
        document = load_raw_probe(LC7_RAW)
        result = compute_execution_slice(document, source["criterion_node_id"])

        self.assertEqual(len(result.node_ids), 8)
        self.assertEqual(len(result.edge_ids), 8)
        self.assertEqual(len(result.node_ids), len(set(result.node_ids)))
        self.assertEqual(len(result.edge_ids), len(set(result.edge_ids)))
        self.assertEqual(len(source["scc"]["returning_edge_ids"]), 1)
        self.assertIn(source["scc"]["returning_edge_ids"][0], result.edge_ids)

        shuffled = json.loads(LC7_RAW.read_text(encoding="utf-8"))
        for graph in shuffled["blueprint"]["graphs"]:
            graph["nodes"].reverse()
            graph["edges"].reverse()
        with tempfile.TemporaryDirectory() as temporary_directory:
            shuffled_path = Path(temporary_directory) / "shuffled.raw.json"
            shuffled_path.write_text(
                json.dumps(shuffled, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            shuffled_result = compute_execution_slice(
                load_raw_probe(shuffled_path), source["criterion_node_id"]
            )
        self.assertEqual(result, shuffled_result)

    def test_unsupported_execution_node_is_a_boundary(self) -> None:
        graph = next(graph for graph in self.document.graphs if graph.name == "EventGraph")
        nodes = {node.id: node for node in graph.nodes}
        unsupported = next(
            node for node in graph.nodes if node.semantic_status == "unsupported"
        )
        outgoing = next(
            edge
            for edge in graph.edges
            if edge.kind == "execution" and edge.source_node_id == unsupported.id
        )
        predecessor = next(
            edge.source_node_id
            for edge in graph.edges
            if edge.kind == "execution" and edge.target_node_id == unsupported.id
        )
        result = compute_execution_slice(self.document, outgoing.target_node_id)
        self.assertIn(unsupported.id, result.node_ids)
        self.assertIn("unsupported_boundary", result.inclusion_reasons[unsupported.id])
        self.assertEqual(
            [unsupported.id],
            [boundary.node_id for boundary in result.boundaries],
        )
        self.assertNotIn(predecessor, result.node_ids)
        self.assertEqual(nodes[unsupported.id].semantic_reason, "latent_function")

    def test_pure_node_is_rejected_as_execution_criterion(self) -> None:
        graph = next(graph for graph in self.document.graphs if graph.name == "EventGraph")
        pure_node = next(
            node
            for node in graph.nodes
            if node.class_path.endswith("K2Node_PromotableOperator")
        )
        with self.assertRaises(ValueError):
            compute_execution_slice(self.document, pure_node.id)

    def test_uncertain_data_producer_is_visible_and_not_crossed(self) -> None:
        graph = next(graph for graph in self.document.graphs if graph.name == "EventGraph")
        criterion = self.ground_truth["criterion"]["node_id"]
        value_edge = next(
            edge
            for edge in graph.edges
            if edge.kind == "data" and edge.target_node_id == criterion
        )
        producer = next(
            node for node in graph.nodes if node.id == value_edge.source_node_id
        )
        upstream = next(
            edge.source_node_id
            for edge in graph.edges
            if edge.kind == "data" and edge.target_node_id == producer.id
        )
        uncertain = replace(
            producer,
            semantic_status="uncertain",
            semantic_reason="test_uncertain",
        )
        changed_graph = replace(
            graph,
            nodes=tuple(
                uncertain if node.id == producer.id else node for node in graph.nodes
            ),
        )
        changed_document = replace(
            self.document,
            graphs=tuple(
                changed_graph if candidate.id == graph.id else candidate
                for candidate in self.document.graphs
            ),
        )
        result = compute_execution_slice(changed_document, criterion)
        self.assertIn(producer.id, result.node_ids)
        self.assertIn("uncertain_boundary", result.inclusion_reasons[producer.id])
        self.assertIn(producer.id, [boundary.node_id for boundary in result.boundaries])
        self.assertNotIn(upstream, result.node_ids)

    def test_lc4_non_reconverging_branch_is_outside_backward_slice(self) -> None:
        graph_id = "Graph::LC4"
        node_ids = (
            "Event",
            "Sequence",
            "Set LC4BranchA1",
            "Set LC4BranchA2",
            "Set LC4BranchB1",
            "Set LC4BranchB2",
            "Set LC4SideEffect",
            "Set LC4Reconverged",
            "Set LC4Complete",
        )

        def node(node_id: str) -> Node:
            pins = tuple(
                Pin(
                    id=f"{node_id}::{direction}",
                    node_id=node_id,
                    persistent_guid="",
                    identity_source="synthetic_test",
                    name="execute" if direction == "in" else "then",
                    direction="input" if direction == "in" else "output",
                    kind="execution",
                    pin_role=(
                        "execution_input"
                        if direction == "in"
                        else "execution_output"
                    ),
                    type={},
                    default={},
                )
                for direction in ("in", "out")
            )
            return Node(
                id=node_id,
                graph_id=graph_id,
                native_guid="",
                identity_source="synthetic_test",
                class_path="/Script/BlueprintGraph.K2Node_CallFunction",
                title=node_id,
                semantic_status="supported",
                semantic_reason="synthetic_lc4_contract_test",
                symbol=None,
                pins=pins,
            )

        edge_pairs = (
            ("Event", "Sequence"),
            ("Sequence", "Set LC4BranchA1"),
            ("Set LC4BranchA1", "Set LC4BranchA2"),
            ("Sequence", "Set LC4BranchB1"),
            ("Set LC4BranchB1", "Set LC4BranchB2"),
            ("Sequence", "Set LC4SideEffect"),
            ("Set LC4BranchA2", "Set LC4Reconverged"),
            ("Set LC4BranchB2", "Set LC4Reconverged"),
            ("Set LC4Reconverged", "Set LC4Complete"),
        )
        edges = tuple(
            Edge(
                id=f"edge::{index}",
                graph_id=graph_id,
                kind="execution",
                source_node_id=source,
                source_pin_id=f"{source}::out",
                target_node_id=target,
                target_pin_id=f"{target}::in",
            )
            for index, (source, target) in enumerate(edge_pairs)
        )
        document = BlueprintDocument(
            format="blueprint-lens-raw",
            format_version="0.2",
            schema_status="synthetic_test",
            engine_version="test",
            blueprint_id="Blueprint::LC4",
            blueprint_name="BP_LC4_SequenceFanout",
            blueprint_path="/Game/BlueprintLens/Fixtures/BP_LC4_SequenceFanout",
            parent_class="/Script/Engine.Actor",
            graphs=(
                Graph(
                    id=graph_id,
                    name="EventGraph",
                    kind="ubergraph",
                    class_path="/Script/Engine.EdGraph",
                    nodes=tuple(node(node_id) for node_id in node_ids),
                    edges=edges,
                ),
            ),
        )

        result = compute_execution_slice(document, "Set LC4Complete")

        self.assertNotIn("Set LC4SideEffect", result.node_ids)
        self.assertNotIn("edge::5", result.edge_ids)
        self.assertEqual(8, len(result.node_ids))
        self.assertEqual(8, len(result.edge_ids))


if __name__ == "__main__":
    unittest.main()
