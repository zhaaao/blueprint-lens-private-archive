"""External production adapters for Blueprint Lens typed-IR workflows."""

from .corpus import (
    CorpusAudit,
    RISK_DIMENSIONS,
    SCALE_BANDS,
    audit_corpus,
    load_corpus_manifest,
)
from .m7_corpus import (
    M7CorpusAudit,
    TypedIRDirectoryProvider,
    audit_m7_corpus,
    load_m7_corpus_manifest,
)
from .m7_coverage import (
    CoverageCell,
    CoverageMatrix,
    LCBinding,
    LC_EXPECTED_DIMENSIONS,
    LC_IDS,
    build_coverage_matrix,
    coverage_matrix_document,
    verify_coverage_matrix,
)
from .m7_truth import audit_query_list, audit_truth_registry, query_set_digest
from .m7_adjudication import (
    adjudication_errors,
    adjudication_gaps,
    build_adjudication,
    verify_adjudication,
)
from .m7_correctness import (
    aggregate_rows,
    build_correctness_report,
    correctness_errors,
    measure_one,
    verify_correctness_report,
)
from .m7_measurement import (
    build_layout_metrics,
    build_measurement_report,
    measure_python_timings,
    measurement_environment,
    measurement_errors,
    verify_measurement_report,
)
from .m7_query_selection import (
    backward_supported_depth,
    select_supplementary_queries,
    verify_supplement,
)
from .pipeline import (
    PipelineItem,
    PipelineResult,
    ProductionPipelineError,
    build_batch,
    build_item,
)
from .project_documents import (
    AssetProvenance,
    FrozenFixture,
    FrozenFixtureProvider,
    ProductionManifestProvider,
    ProjectDocument,
    ProjectDocumentError,
    ProjectDocumentProvider,
)
from .readiness import (
    ReadinessCheck,
    ReadinessError,
    build_readiness,
    freeze_readiness,
)
from .typed_documents import TypedProjectDocument, compose_typed_document
from .execution_criteria import (
    ControlledExecutionCase,
    CorpusExecutionCase,
    ExecutionCriteriaRegistry,
    load_execution_criteria,
)
from .execution_products import (
    build_execution_slice_value,
    canonical_execution_json_bytes,
    publish_execution_slice,
    validate_execution_slice_value,
)
from .execution_evidence import (
    build_execution_slice_packet,
    verify_execution_slice_packet,
)
from .data_criteria import (
    ControlledDataCase,
    CorpusDataCase,
    DataCriteriaRegistry,
    load_data_criteria,
)
from .data_products import (
    build_member_data_slice_value,
    canonical_data_slice_json_bytes,
    publish_member_data_slice,
    validate_member_data_slice_value,
)
from .data_evidence import build_data_slice_packet, verify_data_slice_packet
from .session_contracts import (
    ControlledScenario,
    DataSessionCriterion,
    ExecutionSessionCriterion,
    PresentationBudget,
    SemanticBudget,
    SessionRequest,
    canonical_json_bytes,
    load_controlled_scenarios,
    load_session_request,
)
from .session_explanation import (
    SessionExplanationProducts,
    build_session_explanation,
    validate_baseline_facts,
)
from .session_products import (
    PACKET_FILES,
    SessionPacketResult,
    build_session_packet,
    validate_session_packet,
)
from .session_telemetry import (
    REQUIRED_STAGES,
    ReplayedSessionState,
    append_telemetry_event,
    replay_telemetry_record,
    seal_telemetry_record,
    validate_telemetry_record,
)
from .session_evidence import (
    G6_RETAINED_PATHS,
    build_g6_evidence,
    verify_g6_evidence,
)
from .m7_evidence import (
    build_g7_evidence,
    frozen_dataset,
    g7_evidence_errors,
    verify_g7_evidence,
)
from .m10_structural_effect import (
    build_structural_effect,
    structural_effect_errors,
    verify_structural_effect,
)
from .m10_lc_capacity import build_lc_capacity, lc_capacity_errors
from .m10_lc1_explanation_adapter import (
    adapt_explanation_to_lc1,
    build_lc1_explanation_adapter,
    lc1_explanation_adapter_errors,
    verify_lc1_explanation_adapter,
)

__all__ = [
    "AssetProvenance",
    "CorpusAudit",
    "CoverageCell",
    "CoverageMatrix",
    "M7CorpusAudit",
    "ControlledExecutionCase",
    "ControlledDataCase",
    "ControlledScenario",
    "CorpusDataCase",
    "CorpusExecutionCase",
    "DataCriteriaRegistry",
    "DataSessionCriterion",
    "ExecutionCriteriaRegistry",
    "ExecutionSessionCriterion",
    "FrozenFixture",
    "FrozenFixtureProvider",
    "PipelineItem",
    "PipelineResult",
    "ProductionManifestProvider",
    "ProductionPipelineError",
    "PresentationBudget",
    "ProjectDocument",
    "ProjectDocumentError",
    "ProjectDocumentProvider",
    "LCBinding",
    "LC_EXPECTED_DIMENSIONS",
    "LC_IDS",
    "TypedIRDirectoryProvider",
    "ReadinessCheck",
    "ReadinessError",
    "RISK_DIMENSIONS",
    "SCALE_BANDS",
    "SemanticBudget",
    "SessionRequest",
    "SessionExplanationProducts",
    "SessionPacketResult",
    "ReplayedSessionState",
    "TypedProjectDocument",
    "audit_corpus",
    "audit_m7_corpus",
    "audit_query_list",
    "audit_truth_registry",
    "adjudication_errors",
    "adjudication_gaps",
    "build_adjudication",
    "aggregate_rows",
    "build_correctness_report",
    "correctness_errors",
    "measure_one",
    "verify_correctness_report",
    "build_layout_metrics",
    "build_measurement_report",
    "measure_python_timings",
    "measurement_environment",
    "measurement_errors",
    "verify_measurement_report",
    "backward_supported_depth",
    "build_coverage_matrix",
    "build_batch",
    "build_data_slice_packet",
    "build_execution_slice_value",
    "build_execution_slice_packet",
    "build_item",
    "build_member_data_slice_value",
    "build_readiness",
    "build_session_explanation",
    "build_session_packet",
    "build_g6_evidence",
    "append_telemetry_event",
    "canonical_data_slice_json_bytes",
    "canonical_json_bytes",
    "compose_typed_document",
    "coverage_matrix_document",
    "canonical_execution_json_bytes",
    "freeze_readiness",
    "load_corpus_manifest",
    "load_m7_corpus_manifest",
    "load_data_criteria",
    "load_controlled_scenarios",
    "load_execution_criteria",
    "load_session_request",
    "publish_execution_slice",
    "publish_member_data_slice",
    "query_set_digest",
    "select_supplementary_queries",
    "validate_execution_slice_value",
    "validate_member_data_slice_value",
    "verify_execution_slice_packet",
    "verify_data_slice_packet",
    "verify_coverage_matrix",
    "verify_supplement",
    "validate_baseline_facts",
    "validate_session_packet",
    "validate_telemetry_record",
    "verify_adjudication",
    "PACKET_FILES",
    "REQUIRED_STAGES",
    "replay_telemetry_record",
    "seal_telemetry_record",
    "verify_g6_evidence",
    "G6_RETAINED_PATHS",
    "build_g7_evidence",
    "frozen_dataset",
    "g7_evidence_errors",
    "verify_g7_evidence",
    "build_structural_effect",
    "structural_effect_errors",
    "verify_structural_effect",
    "build_lc_capacity",
    "lc_capacity_errors",
    "adapt_explanation_to_lc1",
    "build_lc1_explanation_adapter",
    "lc1_explanation_adapter_errors",
    "verify_lc1_explanation_adapter",
]
