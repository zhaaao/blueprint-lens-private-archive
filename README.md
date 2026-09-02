# Blueprint Lens

Blueprint Lens is an MSc Games Engineering dissertation research artefact for
query-specific static slicing and question-oriented dependency explanations of
Unreal Engine 5 Blueprint graphs.

This sanitised release contains the software and retained inputs needed to
rerun the bounded static checks reported in the dissertation. It also contains
the Unreal Editor project and the retained English-language Execution and Data
demonstration frames.

## Included material

- Python implementation of the typed representation, execution slicing, data
  slicing and explanation model.
- The 38 registered validation queries and a hashed 98-entry inventory of their
  inputs and reference results.
- Schemas, fixtures and four focused core test files.
- The Unreal Engine 5.8.1 project, plugin source and demonstration Blueprint.
- A manifest linking the retained English screenshots to matching widget-tree
  captures.
- A concise [technical limitations record](LIMITATIONS.md).

The dissertation manuscript and submission records are not part of this
software archive. Development-only workflow material, raw diagnostics, private
research notes, local histories and machine-specific files are excluded. Review
metadata is pseudonymised; the query criteria and expected node and relation
sets are unchanged.

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

The Unreal demonstration additionally requires Unreal Engine 5.8.1 and Visual
Studio 2022 with MSVC and the Windows SDK.

### Optional layout backends

The bounded static reproduction in the next section uses only Python. The
Unreal Editor visualisation can additionally use Graphviz or ELK.js for
automatic graph layout. When the panel requests automatic layout, it tries
`ELK.Layered`, then `Graphviz.dot`, and finally the built-in deterministic
fallback. The external backends are therefore optional for ordinary use, but
are needed to exercise their specific layout paths.

The tested Windows environment used Graphviz 15.1.1, Node.js 24.15.0 and ELK.js
0.12.0. Install Graphviz and Node.js so that `dot.exe` and `node.exe` are on
`PATH`, then install the pinned ELK.js version and set the process-scoped paths:

```powershell
npm.cmd install --global elkjs@0.12.0
$env:BLUEPRINT_LENS_GRAPHVIZ_ROOT = (Get-Command dot.exe).Source
$env:BLUEPRINT_LENS_NODE_EXE = (Get-Command node.exe).Source
$env:BLUEPRINT_LENS_ELKJS_ROOT = Join-Path (& npm.cmd root --global) 'elkjs'

dot.exe -V
node.exe --version
npm.cmd list --global elkjs --depth=0
```

The default [ELK bridge](tools/layout/blueprint_lens_elk_layout.mjs) is included
in this repository. A non-standard checkout may point
`BLUEPRINT_LENS_ELK_HELPER` at that file explicitly. These environment
variables apply to the PowerShell process that launches Unreal Editor; set them
persistently only if that matches your local setup.

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
| Registered slice checks | `status: PASS`; 38 queries and 38 exact matches; 266/266 node memberships and 230/230 relation memberships; 98 hashed input/reference entries; 12 typed-IR entries; no provenance errors |
| Structural-effect report verification | `()` (no verification errors) |
| Focused core tests | `90 passed, 9 subtests passed` on the tested environment |

These commands validate the retained static result set. They do not rerun a
user study, performance benchmark or comparative tool experiment.

## Unreal Editor demonstration

Close any running Unreal Editor instance before building. Change
`$unrealRoot` if Unreal Engine is installed elsewhere:

```powershell
$unrealRoot = 'C:\Program Files\Epic Games\UE_5.8'
powershell -ExecutionPolicy Bypass -File tools/build_editor_target.ps1 -UnrealRoot $unrealRoot
$project = (Resolve-Path '.\unreal\BlueprintLensProbe\BlueprintLensProbe.uproject').Path
& (Join-Path $unrealRoot 'Engine\Binaries\Win64\UnrealEditor.exe') $project -NoLiveCoding -culture=en
```

In the Editor:

1. Open `/Game/LensCorpus/BP_M10_CompositionDemo`.
2. Choose `Window > Blueprint Lens` if the panel is not visible.
3. Select `Expand session controls`. A selection proposes a query; `Run` commits it.
4. For Execution, select `Set ComposedExecutionComplete`, choose `Run`, and open
   `C - Causal Lens`. The captured result header is 18 entities and 20 relations.
5. For Data, choose `Data`, select `DataWithMultipleSetsAnswer`, choose `Run`, and
   open `C - Causal Lens`. Expanding both source disclosures shows nine entities,
   eight relations and two retained Set writes.

The retained frames prove only the painted state at capture time. They do not by
themselves establish runtime causality, performance, capacity, usability, human
comprehension, comparative superiority or that this interface should be a
product default.

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
