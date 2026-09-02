# Blueprint Lens

Blueprint Lens is an MSc Games Engineering dissertation research artefact for
query-specific static slicing and question-oriented dependency explanations of
Unreal Engine 5 Blueprint graphs.

This repository contains the software and retained inputs needed to rerun the
bounded static checks described in the dissertation's Appendix D. It also
contains the Unreal Editor project and the retained English-language Execution
and Data demonstration frames. It was prepared from a sanitised examiner
distribution for public release.

## Included material

- Python implementation of the typed representation, execution slicing, data
  slicing and explanation model.
- The 98-file examiner reproduction set for the 38 registered M7 queries.
- Schemas, fixtures and four focused core test files.
- The Unreal Engine 5.8.1 project, plugin source and demonstration Blueprint.
- A frame-only visual manifest and its retained English screenshots/tree records.
- A concise [technical limitations record](LIMITATIONS.md).

The dissertation manuscript and submission records are not part of this software
archive. Development-only workflow material, raw diagnostics, private research notes, local
histories and machine-specific files are excluded. Five legacy reviewer labels in retained
truth metadata are normalised to role-only identifiers in this distribution;
the query criteria and expected node/relation sets are unchanged.

## Prerequisites and installation

The static reproduction was tested on Windows with Python 3.14.2. It uses the
Python standard library; `pytest` is required only for the focused test command.
No external dataset, model weights or network service is required. All static
inputs used below are versioned in this repository. The pipeline has no training
stage or random sampling, so no random seed is required.

Clone the repository and create an isolated Python environment in PowerShell:

```powershell
git clone https://github.com/zhaaao/blueprint-lens-private-archive.git
Set-Location blueprint-lens-private-archive
py -3.14 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

If the repository is still private, GitHub authentication is required for the
clone command. The Unreal demonstration additionally requires Unreal Engine
5.8.1 and Visual Studio 2022 with MSVC and the Windows SDK.

## Bounded static reproduction

Open PowerShell at the repository root with the virtual environment active:

```powershell
python --version
$env:PYTHONPATH='analysis'
python tools/reproduce_m7_slices.py --root .
python -c "import json; from pathlib import Path; from blueprint_lens.production import verify_structural_effect; root=Path('.').resolve(); report=json.loads((root/'artifacts/m10/structural-effect/structural-effect.v1.json').read_text(encoding='utf-8')); print(verify_structural_effect(report, root))"
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
python -m pytest analysis/tests/test_execution_slice.py analysis/tests/test_data_slice.py analysis/tests/test_explanation_model.py analysis/tests/test_explanation_semantic_extension.py --basetemp ".pytest-tmp-$runId" -q
```

Expected results are:

| Check | Expected result |
| --- | --- |
| Registered M7 slice reproduction | `status: PASS`; 38 queries and 38 exact matches; 266 produced/truth node identities; 230 produced/truth relation identities; 98 authority entries; 12 typed-IR entries; no provenance errors |
| Retained M10 structural-effect verification | `()` |
| Focused core tests | `90 passed, 9 subtests passed` on the tested environment |

These commands validate the retained static result set. They do not rerun a
user study, performance benchmark or comparative tool experiment.

## Unreal Editor demonstration

Close any running Unreal Editor instance before building:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_editor_target.ps1
$project = (Resolve-Path '.\unreal\BlueprintLensProbe\BlueprintLensProbe.uproject').Path
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' $project -NoLiveCoding -culture=en
```

In the Editor:

1. Open `/Game/LensCorpus/BP_M10_CompositionDemo`.
2. Choose `Window > Blueprint Lens` if the panel is not visible.
3. Select `Expand session controls`. A selection proposes a query; `Run` commits it.
4. For Execution, select `Set ComposedExecutionComplete`, choose `Run`, and open
   `C - Causal Lens`. The retained header is 18 entities and 20 relations.
5. For Data, choose `Data`, select `DataWithMultipleSetsAnswer`, choose `Run`, and
   open `C - Causal Lens`. Expanding both source disclosures shows nine entities,
   eight relations and two retained Set writes.

The retained frames prove only the painted state at capture time. They do not by
themselves establish runtime causality, performance, capacity, usability, human
comprehension, comparative superiority or a product-default design.

## Troubleshooting

- Run the commands from the repository root and set `PYTHONPATH` to `analysis`
  before invoking the reproduction script or tests.
- Use a fresh repository-local `--basetemp` for each test run. This avoids stale
  Windows ACLs and prevents parallel runs from sharing temporary files.
- Close Unreal Editor before building. Do not use Live Coding for this project.
- If PowerShell blocks virtual-environment activation, use a process-scoped
  execution policy or invoke `.venv\Scripts\python.exe` directly.

## Evidence and reuse boundaries

The retained JSON reports, manifests, tests and screenshots support only their
stated static or captured-state checks. They do not establish real-time
performance, general correctness for arbitrary Blueprints, user comprehension,
or superiority over another tool. See [LIMITATIONS.md](LIMITATIONS.md) for the
detailed technical boundary.

No licence has been selected for this repository. Until a `LICENSE` file is
added, repository visibility alone does not grant permission to copy,
redistribute or modify the material.
