from __future__ import annotations

import json
from pathlib import Path
import sys
from types import MappingProxyType
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis"))

from blueprint_lens import (  # noqa: E402
    compute_member_variable_data_slice,
    load_raw_probe,
)


FIXTURE = ROOT / "fixtures" / "raw" / "BP_SlicingProbe.raw-0.2.json"
GROUND_TRUTH = (
    ROOT
    / "fixtures"
    / "ground_truth"
    / "BP_SlicingProbe.health-data.candidate-0.1.json"
)


class MemberVariableDataSliceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = load_raw_probe(FIXTURE)
        cls.ground_truth = json.loads(GROUND_TRUTH.read_text(encoding="utf-8"))
        criterion = cls.ground_truth["criterion"]
        cls.result = compute_member_variable_data_slice(
            cls.document, criterion["graph_id"], criterion["member_guid"]
        )

    def test_probe_matches_reviewed_ground_truth(self) -> None:
        expected = self.ground_truth["expected"]
        self.assertEqual(set(self.result.node_ids), set(expected["node_ids"]))
        self.assertEqual(set(self.result.edge_ids), set(expected["edge_ids"]))

    def test_result_retains_old_fields_and_freezes_new_accountability(self) -> None:
        self.assertEqual(self.result.graph_id, self.ground_truth["criterion"]["graph_id"])
        self.assertEqual(
            self.result.member_guid,
            self.ground_truth["criterion"]["member_guid"],
        )
        self.assertEqual(self.result.member_name, "Health")
        self.assertIsInstance(self.result.node_ids, tuple)
        self.assertIsInstance(self.result.edge_ids, tuple)
        self.assertIsInstance(self.result.inclusion_reasons, MappingProxyType)
        self.assertIsInstance(self.result.edge_inclusion_reasons, MappingProxyType)
        self.assertIsInstance(self.result.boundaries, tuple)
        with self.assertRaises(TypeError):
            self.result.inclusion_reasons["new"] = ("member_get",)

    def test_all_health_accesses_are_selected(self) -> None:
        graph = next(
            graph for graph in self.document.graphs if graph.id == self.result.graph_id
        )
        guid = self.ground_truth["criterion"]["member_guid"]
        accesses = {
            node.id
            for node in graph.nodes
            if node.symbol
            and node.symbol.get("kind") == "variable"
            and node.symbol.get("guid") == guid
        }
        self.assertEqual(len(accesses), 5)
        self.assertTrue(accesses <= set(self.result.node_ids))

    def test_delay_is_visible_boundary_without_its_predecessor(self) -> None:
        graph = next(
            graph for graph in self.document.graphs if graph.id == self.result.graph_id
        )
        delay = next(
            node for node in graph.nodes if node.semantic_reason == "latent_function"
        )
        predecessor = next(
            edge.source_node_id
            for edge in graph.edges
            if edge.kind == "execution" and edge.target_node_id == delay.id
        )
        self.assertIn(delay.id, self.result.node_ids)
        self.assertIn("unsupported_boundary", self.result.inclusion_reasons[delay.id])
        self.assertNotIn(predecessor, self.result.node_ids)

    def test_function_call_is_not_expanded_interprocedurally(self) -> None:
        event_graph = next(
            graph for graph in self.document.graphs if graph.id == self.result.graph_id
        )
        recovery_call = next(
            node
            for node in event_graph.nodes
            if node.symbol and node.symbol.get("name") == "CalculateRecovery"
        )
        function_graph = next(
            graph for graph in self.document.graphs if graph.name == "CalculateRecovery"
        )
        self.assertIn(recovery_call.id, self.result.node_ids)
        self.assertTrue(
            set(self.result.node_ids).isdisjoint(node.id for node in function_graph.nodes)
        )


if __name__ == "__main__":
    unittest.main()
