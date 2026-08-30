"""Tests for selected Explanation semantic-extension validation rules.

The suite covers relation labels, disambiguators, group structure,
partial-order checks, claim-evidence coverage and the real LC2 fixture. It is
not an exhaustive negative for every rule in design sections 4-6.
"""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import sys
from typing import Any
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis"))

from blueprint_lens import (  # noqa: E402
    ExplanationModelError,
    SchemaValidationError,
    build_lc2_explanation,
    validate_instance,
    validate_explanation_model,
)
import blueprint_lens.explanation_model as explanation_model  # noqa: E402
import blueprint_lens.lc2_explanation as lc2_explanation  # noqa: E402

EVIDENCE = ROOT / "artifacts/r1/lc2-guard-truth"
IR = EVIDENCE / "BP_LC2_NestedGuards.ir.v1.json"
SLICE = EVIDENCE / "BP_LC2_NestedGuards.execution.slice.v1.json"
GUARD_TRUTH = EVIDENCE / "BP_LC2_NestedGuards.guard-truth.v1.json"
ASSET = (
    ROOT
    / "unreal/BlueprintLensProbe/Content/LensCorpus"
    / "BP_LC2_NestedGuards.uasset"
)
SCHEMA = ROOT / "schemas/blueprint-lens-explanation-v1.schema.json"

RESOURCES = (
    ROOT
    / "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Resources/Explanation"
)


class SemanticExtensionTests(unittest.TestCase):
    """Validate the v1.1 contract on the real LC2 truth."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.base_model = build_lc2_explanation(IR, SLICE, ASSET, GUARD_TRUTH)

    # ---------------------------------------------------------------- helpers

    def unit_by_title(self, model: dict[str, Any], title: str) -> dict[str, Any]:
        matches = [unit for unit in model["units"] if unit["title"] == title]
        self.assertEqual(len(matches), 1, f"expected one {title!r} unit")
        return matches[0]

    def branch_units(self, model: dict[str, Any]) -> tuple[str, str]:
        """Return (outer_branch_unit_id, inner_branch_unit_id)."""

        outer_predicate = self.unit_by_title(model, "Get OuterEnabled")["id"]
        inner_predicate = self.unit_by_title(model, "Get InnerEnabled")["id"]
        outer = inner = ""
        for relation in model["relations"]:
            if relation["kind"] != "predicate_for":
                continue
            if relation["source_unit_id"] == outer_predicate:
                outer = relation["target_unit_id"]
            elif relation["source_unit_id"] == inner_predicate:
                inner = relation["target_unit_id"]
        self.assertTrue(outer and inner and outer != inner)
        return outer, inner

    def relation_between(
        self, model: dict[str, Any], source_unit_id: str, target_unit_id: str
    ) -> dict[str, Any]:
        matches = [
            relation
            for relation in model["relations"]
            if relation["source_unit_id"] == source_unit_id
            and relation["target_unit_id"] == target_unit_id
        ]
        self.assertEqual(len(matches), 1)
        return matches[0]

    def augmented(self) -> dict[str, Any]:
        """Build a fully valid v1.1 model from the frozen LC2 truth."""
        return deepcopy(self.base_model)

    def nested(self) -> dict[str, Any]:
        """Add a parent/child guard_nest pair on top of the valid model."""

        model = self.augmented()
        outer_branch, inner_branch = self.branch_units(model)
        accepted = self.unit_by_title(model, "Set Accepted")["id"]
        inner_rejected = self.unit_by_title(model, "Set InnerRejected")["id"]
        outer_rejected = self.unit_by_title(model, "Set OuterRejected")["id"]
        model["groups"].extend(
            [
                {
                    "id": "group.guard_nest.outer",
                    "kind": "guard_nest",
                    "title": "",
                    "ordered_unit_ids": [
                        outer_branch,
                        inner_branch,
                        accepted,
                        inner_rejected,
                        outer_rejected,
                    ],
                    "ordered_relation_ids": [],
                    "entry_unit_id": outer_branch,
                    "parent_group_id": None,
                    "entered_by": None,
                    "member_count": 5,
                    "projection_status": "STRUCTURAL_ONLY",
                    "diagnostic_code": "",
                    "claim_evidence": [],
                },
                {
                    "id": "group.guard_nest.inner",
                    "kind": "guard_nest",
                    "title": "",
                    "ordered_unit_ids": [inner_branch, accepted, inner_rejected],
                    "ordered_relation_ids": [],
                    "entry_unit_id": inner_branch,
                    "parent_group_id": "group.guard_nest.outer",
                    "entered_by": "condition_true",
                    "member_count": 3,
                    "projection_status": "STRUCTURAL_ONLY",
                    "diagnostic_code": "",
                    "claim_evidence": [],
                },
            ]
        )
        return model

    def validate(self, model: dict[str, Any]) -> None:
        validate_explanation_model(model, IR, SLICE, ASSET, SCHEMA)

    def assert_rejected(self, model: dict[str, Any], fragment: str) -> None:
        with self.assertRaises(ExplanationModelError) as caught:
            self.validate(model)
        self.assertIn(fragment, str(caught.exception))

    # --------------------------------------------------------------- positive

    def test_fully_augmented_model_validates(self) -> None:
        self.validate(self.augmented())

    def test_nested_guard_groups_validate(self) -> None:
        self.validate(self.nested())

    def test_stripping_groups_leaves_a_valid_model(self) -> None:
        model = self.augmented()
        model.pop("groups")
        model.pop("group_partial_order")
        self.validate(model)

    def test_frozen_v1_artifacts_remain_valid_without_extension_fields(self) -> None:
        model = deepcopy(self.base_model)
        for unit in model["units"]:
            unit.pop("disambiguator", None)
            self.assertNotIn("disambiguator", unit)
        for relation in model["relations"]:
            relation.pop("port_label", None)
            relation.pop("semantic_label", None)
            self.assertNotIn("port_label", relation)
            self.assertNotIn("semantic_label", relation)
        model.pop("groups", None)
        model.pop("group_partial_order", None)
        self.validate(model)
        self.assertNotIn("groups", model)

    # ------------------------------------------------- relation label rules

    def test_semantic_label_requires_port_label(self) -> None:
        model = self.augmented()
        del model["relations"][0]["port_label"]
        self.assert_rejected(model, "semantic_label requires port_label")

    def test_port_label_requires_endpoint_ledger(self) -> None:
        model = self.augmented()
        del model["relations"][0]["source_edge_endpoints"]
        self.assert_rejected(model, "port_label requires source_edge_endpoints")

    def test_port_label_must_match_endpoint_provenance(self) -> None:
        model = self.augmented()
        model["relations"][0]["port_label"] = "not-the-exported-pin"
        self.assert_rejected(model, "port_label disagrees with endpoint provenance")

    def test_semantic_label_must_match_the_frozen_table(self) -> None:
        model = self.augmented()
        relation = next(
            candidate
            for candidate in model["relations"]
            if candidate["kind"] == "controls_execution"
            and candidate["port_label"] == "then"
        )
        relation["semantic_label"] = "condition_false"
        self.assert_rejected(model, "semantic_label must be 'condition_true'")

    def test_unlisted_kind_and_port_pair_has_no_admissible_label(self) -> None:
        # Exercised directly: mutating kind or the exported pin on the real
        # fixture would trip the earlier IR provenance checks first.
        with self.assertRaises(ExplanationModelError) as caught:
            explanation_model._validate_relation_labels(
                {
                    "relations": [
                        {
                            "id": "relation.controls_execution.synthetic",
                            "kind": "controls_execution",
                            "port_label": "execute",
                            "semantic_label": "condition_true",
                            "source_edge_endpoints": [
                                {"source_port_label": "execute"}
                            ],
                        }
                    ]
                }
            )
        self.assertIn("no admissible semantic label", str(caught.exception))

    # --------------------------------------------------- disambiguator rules

    def test_disambiguator_text_must_equal_the_exported_port_label(self) -> None:
        model = self.augmented()
        outer_branch, inner_branch = self.branch_units(model)
        # Both guards carry the identical raw title; the disambiguator is the
        # only thing that separates them, so it may not be free text.
        self.assertEqual(
            [unit["title"] for unit in model["units"] if unit["id"] in
             {outer_branch, inner_branch}],
            ["Branch", "Branch"],
        )
        unit = next(item for item in model["units"] if item["id"] == outer_branch)
        unit["disambiguator"]["text"] = "SomethingElse"
        self.assert_rejected(
            model, "disambiguator text must equal the exported source port label"
        )

    def test_disambiguator_evidence_must_name_its_deriving_relation(self) -> None:
        model = self.augmented()
        outer_branch, inner_branch = self.branch_units(model)
        unit = next(item for item in model["units"] if item["id"] == outer_branch)
        other = next(
            candidate
            for candidate in model["relations"]
            if candidate["kind"] == "predicate_for"
            and candidate["target_unit_id"] == inner_branch
        )
        unit["disambiguator"]["evidence_relation_ids"] = [other["id"]]
        self.assert_rejected(
            model, "disambiguator evidence must name its deriving relation"
        )

    def test_disambiguator_evidence_must_resolve(self) -> None:
        model = self.augmented()
        outer_branch, _ = self.branch_units(model)
        unit = next(item for item in model["units"] if item["id"] == outer_branch)
        unit["disambiguator"]["evidence_relation_ids"] = ["relation.does.not.exist"]
        self.assert_rejected(model, "disambiguator evidence does not resolve")

    def test_disambiguator_requires_exactly_one_predicate_relation(self) -> None:
        model = self.augmented()
        begin_play = self.unit_by_title(model, "BeginPlay")
        relation = model["relations"][0]
        begin_play["disambiguator"] = {
            "text": "anything",
            "rule_id": "unit.branch.from_predicate_for",
            "evidence_relation_ids": [relation["id"]],
        }
        self.assert_rejected(
            model, "requires exactly one predicate_for relation, got 0"
        )

    def test_disambiguator_must_abstain_when_two_predicates_match(self) -> None:
        model = self.augmented()
        outer_branch, _ = self.branch_units(model)
        duplicate = deepcopy(
            next(
                relation
                for relation in model["relations"]
                if relation["kind"] == "predicate_for"
                and relation["target_unit_id"] == outer_branch
            )
        )
        duplicate["id"] = "relation.predicate_for.synthetic-duplicate"
        model["relations"].append(duplicate)
        lc2_explanation._populate_lc2_disambiguators(model)
        self.assertNotIn(
            "disambiguator",
            next(unit for unit in model["units"] if unit["id"] == outer_branch),
        )
        _, inner_branch = self.branch_units(model)
        self.assertIn(
            "disambiguator",
            next(unit for unit in model["units"] if unit["id"] == inner_branch),
        )

    def test_disambiguator_rule_enum_is_frozen(self) -> None:
        model = self.augmented()
        outer_branch, _ = self.branch_units(model)
        unit = next(item for item in model["units"] if item["id"] == outer_branch)
        unit["disambiguator"]["rule_id"] = "unit.branch.from_title_guess"
        with self.assertRaises(ExplanationModelError):
            self.validate(model)

    def test_disambiguator_rule_requires_a_control_unit(self) -> None:
        model = self.augmented()
        outer_branch, _ = self.branch_units(model)
        unit = next(item for item in model["units"] if item["id"] == outer_branch)
        unit["role"] = "predicate"
        with self.assertRaisesRegex(
            ExplanationModelError,
            "disambiguator rule requires control role",
        ):
            explanation_model._validate_disambiguators(model)

    # ----------------------------------------------------------- group rules

    def test_group_unit_must_resolve(self) -> None:
        model = self.augmented()
        model["groups"][0]["ordered_unit_ids"][0] = "unit.does.not.exist"
        self.assert_rejected(model, "group unit does not resolve")

    def test_group_relation_must_resolve(self) -> None:
        model = self.augmented()
        model["groups"][0]["ordered_relation_ids"][0] = "relation.does.not.exist"
        self.assert_rejected(model, "group relation does not resolve")

    def test_group_member_count_must_match(self) -> None:
        model = self.augmented()
        model["groups"][0]["member_count"] += 1
        self.assert_rejected(model, "member_count does not match")

    def test_group_entry_and_exit_must_be_members(self) -> None:
        for field in ("entry_unit_id", "exit_unit_id"):
            with self.subTest(field=field):
                model = self.augmented()
                model["groups"][0][field] = self.unit_by_title(
                    model, "Get OuterEnabled"
                )["id"]
                self.assert_rejected(model, f"group {field} must be a member")

    def test_group_ids_must_be_unique(self) -> None:
        model = self.augmented()
        model["groups"][1]["id"] = model["groups"][0]["id"]
        self.assert_rejected(model, "group IDs must be unique")

    def test_group_members_must_be_unique(self) -> None:
        model = self.augmented()
        group = model["groups"][0]
        group["ordered_unit_ids"].append(group["ordered_unit_ids"][0])
        group["member_count"] += 1
        self.assert_rejected(model, "group members must be unique")

    def test_outcome_path_must_chain_every_member(self) -> None:
        model = self.augmented()
        model["groups"][0]["ordered_relation_ids"].pop()
        self.assert_rejected(model, "must chain every member exactly once")

    def test_outcome_path_relation_order_must_be_consistent(self) -> None:
        model = self.augmented()
        relations = model["groups"][0]["ordered_relation_ids"]
        relations[0], relations[1] = relations[1], relations[0]
        self.assert_rejected(model, "relation order is inconsistent")

    def test_outcome_path_exit_must_not_be_criterion(self) -> None:
        model = deepcopy(self.base_model)
        guard = json.loads(GUARD_TRUTH.read_text(encoding="utf-8"))
        relations_by_edge = {
            edge_id: relation
            for relation in model["relations"]
            for edge_id in relation["source_edge_ids"]
        }
        criterion = model["criterion_unit_id"]
        for group in model["groups"]:
            if group["kind"] != "outcome_path":
                continue
            path_id = group["id"].split("group.outcome_path.", 1)[1]
            path = next(
                item for item in guard["outcome_paths"] if item["id"] == path_id
            )
            reconvergence = relations_by_edge[path["reconvergence_edge_id"]]
            group["ordered_unit_ids"].append(criterion)
            group["ordered_relation_ids"].append(reconvergence["id"])
            group["exit_unit_id"] = criterion
            group["member_count"] = len(group["ordered_unit_ids"])
        self.assert_rejected(
            model,
            "outcome_path exit_unit_id must not equal criterion_unit_id",
        )

    def test_incomparable_pair_exits_must_be_distinct(self) -> None:
        model = deepcopy(self.base_model)
        first, second = model["groups"][:2]
        second["ordered_unit_ids"] = list(first["ordered_unit_ids"])
        second["ordered_relation_ids"] = list(first["ordered_relation_ids"])
        second["entry_unit_id"] = first["entry_unit_id"]
        second["exit_unit_id"] = first["exit_unit_id"]
        second["member_count"] = len(second["ordered_unit_ids"])
        self.assert_rejected(
            model,
            "incomparable groups must have distinct exit_unit_id values",
        )

    def test_schema_rejects_guard_nest_exit_unit_id(self) -> None:
        model = self.nested()
        outer = next(
            group for group in model["groups"] if group["kind"] == "guard_nest"
        )
        outer["exit_unit_id"] = outer["entry_unit_id"]
        with self.assertRaisesRegex(
            SchemaValidationError,
            "expected exactly one oneOf match",
        ):
            validate_instance(model, json.loads(SCHEMA.read_text(encoding="utf-8")))

    def test_generic_validator_rejects_guard_nest_exit_unit_id(self) -> None:
        model = self.nested()
        outer = next(
            group for group in model["groups"] if group["kind"] == "guard_nest"
        )
        outer["exit_unit_id"] = outer["entry_unit_id"]
        with self.assertRaisesRegex(
            ExplanationModelError,
            "guard_nest must not declare exit_unit_id",
        ):
            explanation_model._validate_groups(model)

    def test_group_partial_order_semantics_is_a_generic_contract_constant(
        self,
    ) -> None:
        model = self.augmented()
        model["group_partial_order"]["semantics"] = "forged semantics"
        self.assert_rejected(
            model,
            "group_partial_order semantics does not match frozen constant",
        )

    def test_incomparable_pair_declaration_order_is_not_bound(self) -> None:
        model = self.augmented()
        pairs = model["group_partial_order"]["incomparable_group_ids"]
        pairs.reverse()
        for pair in pairs:
            pair.reverse()
        self.validate(model)

    def test_exact_duplicate_incomparable_pair_is_rejected(self) -> None:
        model = self.augmented()
        pairs = model["group_partial_order"]["incomparable_group_ids"]
        pairs.append(list(pairs[0]))
        self.assert_rejected(model, "incomparable pair is duplicated")

    def test_reversed_duplicate_incomparable_pair_is_rejected(self) -> None:
        model = self.augmented()
        pairs = model["group_partial_order"]["incomparable_group_ids"]
        pairs.append(list(reversed(pairs[0])))
        self.assert_rejected(model, "incomparable pair is duplicated")

    def test_non_path_group_relation_must_stay_inside_its_members(self) -> None:
        model = self.nested()
        inner = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.inner"
        )
        begin_play = self.unit_by_title(model, "BeginPlay")["id"]
        outer_branch, _ = self.branch_units(model)
        inner["ordered_relation_ids"] = [
            self.relation_between(model, begin_play, outer_branch)["id"]
        ]
        self.assert_rejected(model, "group relation leaves its members")

    def test_complete_group_requires_claim_evidence(self) -> None:
        model = self.augmented()
        model["groups"][0]["claim_evidence"] = []
        self.assert_rejected(model, "COMPLETE group requires a title and claim")

    def test_complete_group_evidence_requires_predicate_label_prefix(self) -> None:
        model = self.augmented()
        model["groups"][0]["claim_evidence"] = [
            entry
            for entry in model["groups"][0]["claim_evidence"]
            if not entry["component"].startswith("predicate_label.")
        ]
        self.assert_rejected(
            model,
            "does not cover predicate_label prefix",
        )

    def test_complete_group_evidence_prefixes_must_match_by_guard(self) -> None:
        model = self.augmented()
        evidence = next(
            entry
            for entry in model["groups"][0]["claim_evidence"]
            if entry["component"].startswith("branch_outcome.")
        )
        evidence["component"] = "branch_outcome.unrelated_guard"
        self.assert_rejected(
            model,
            "does not cover stated claim component 'branch_outcome.outer_guard'",
        )

    def test_structural_only_group_requires_empty_title_and_claim_evidence(self) -> None:
        model = self.nested()
        outer = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.outer"
        )
        outer["claim_evidence"] = [
            {"component": "c", "fact_owner": "f", "source": "s"}
        ]
        self.assert_rejected(
            model,
            "STRUCTURAL_ONLY group requires an empty title and empty claim_evidence",
        )

    def test_structural_only_group_rejects_a_nonempty_title(self) -> None:
        model = self.nested()
        outer = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.outer"
        )
        outer["title"] = "Structural label"
        self.assert_rejected(
            model,
            "STRUCTURAL_ONLY group requires an empty title and empty claim_evidence",
        )

    def test_abstained_group_requires_a_diagnostic_and_no_claim(self) -> None:
        model = self.augmented()
        group = model["groups"][0]
        group["projection_status"] = "ABSTAINED"
        self.assert_rejected(model, "ABSTAINED group requires a diagnostic")

    def test_abstained_group_accepts_a_diagnostic_without_a_title(self) -> None:
        model = self.augmented()
        group = model["groups"][0]
        group["projection_status"] = "ABSTAINED"
        group["title"] = ""
        group["claim_evidence"] = []
        group["diagnostic_code"] = "OUTCOME_TERMINAL_UNRESOLVED"
        self.validate(model)

    # ---------------------------------------------------------- nesting rules

    def test_group_parent_must_resolve(self) -> None:
        model = self.nested()
        inner = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.inner"
        )
        inner["parent_group_id"] = "group.does.not.exist"
        self.assert_rejected(model, "group parent does not resolve")

    def test_group_without_parent_must_not_declare_entered_by(self) -> None:
        model = self.augmented()
        model["groups"][0]["entered_by"] = "condition_true"
        self.assert_rejected(model, "must not declare entered_by")

    def test_nested_group_must_declare_entered_by(self) -> None:
        model = self.nested()
        inner = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.inner"
        )
        inner["entered_by"] = None
        self.assert_rejected(model, "nested group must declare entered_by")

    def test_entered_by_requires_a_real_entering_relation(self) -> None:
        model = self.nested()
        inner = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.inner"
        )
        inner["entered_by"] = "condition_false"
        self.assert_rejected(model, "has no entering relation from its parent")

    def test_group_parent_chain_must_be_acyclic(self) -> None:
        model = self.nested()
        outer = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.outer"
        )
        inner = next(
            group for group in model["groups"] if group["id"] == "group.guard_nest.inner"
        )
        outer["parent_group_id"] = inner["id"]
        outer["entered_by"] = "condition_true"
        self.assert_rejected(model, "contains a cycle")

    # ---------------------------------------------------- partial-order rules

    def test_partial_order_requires_groups(self) -> None:
        model = self.augmented()
        model.pop("groups")
        self.assert_rejected(model, "group_partial_order requires groups")

    def test_incomparable_group_must_resolve(self) -> None:
        model = self.augmented()
        model["group_partial_order"]["incomparable_group_ids"][0][1] = "group.missing"
        self.assert_rejected(model, "incomparable group does not resolve")

    def test_incomparable_pair_must_name_two_distinct_groups(self) -> None:
        model = self.augmented()
        pair = model["group_partial_order"]["incomparable_group_ids"][0]
        pair[1] = pair[0]
        self.assert_rejected(model, "must name two distinct groups")

    def test_ordered_groups_cannot_be_declared_incomparable(self) -> None:
        model = self.augmented()
        begin_play = self.unit_by_title(model, "BeginPlay")["id"]
        outer_branch, inner_branch = self.branch_units(model)
        accepted = self.unit_by_title(model, "Set Accepted")["id"]
        model["groups"] = [
            {
                "id": "group.outcome_path.prefix",
                "kind": "outcome_path",
                "title": "",
                "ordered_unit_ids": [begin_play, outer_branch],
                "ordered_relation_ids": [
                    self.relation_between(model, begin_play, outer_branch)["id"]
                ],
                "entry_unit_id": begin_play,
                "exit_unit_id": outer_branch,
                "parent_group_id": None,
                "entered_by": None,
                "member_count": 2,
                "projection_status": "STRUCTURAL_ONLY",
                "diagnostic_code": "",
                "claim_evidence": [],
            },
            {
                "id": "group.outcome_path.suffix",
                "kind": "outcome_path",
                "title": "",
                "ordered_unit_ids": [inner_branch, accepted],
                "ordered_relation_ids": [
                    self.relation_between(model, inner_branch, accepted)["id"]
                ],
                "entry_unit_id": inner_branch,
                "exit_unit_id": accepted,
                "parent_group_id": None,
                "entered_by": None,
                "member_count": 2,
                "projection_status": "STRUCTURAL_ONLY",
                "diagnostic_code": "",
                "claim_evidence": [],
            },
        ]
        model["group_partial_order"] = {
            "incomparable_group_ids": [
                ["group.outcome_path.prefix", "group.outcome_path.suffix"]
            ],
            "semantics": "no execution order is proven between these groups",
        }
        self.assert_rejected(model, "declared incomparable groups are ordered")


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
