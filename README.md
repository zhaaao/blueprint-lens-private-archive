# Blueprint Lens

Blueprint Lens is a BSc Computer Science dissertation project for query-specific
static slicing and question-oriented dependency explanations of Unreal Engine 5
Blueprint graphs.

This examiner archive contains the software and retained inputs needed to rerun
the bounded static checks described in Appendix D, together with the Unreal
Editor project and the current English-language Execution and Data demonstration
frames.

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

## Environment used

- Windows
- Python 3.14.2
- Unreal Engine 5.8.1
- Visual Studio 2022 with MSVC and the Windows SDK

## Bounded static reproduction

Open PowerShell at the repository root:

```powershell
python --version
$env:PYTHONPATH='analysis'
python tools/reproduce_m7_slices.py --root .
python -c "import json; from pathlib import Path; from blueprint_lens.production import verify_structural_effect; root=Path('.').resolve(); report=json.loads((root/'artifacts/m10/structural-effect/structural-effect.v1.json').read_text(encoding='utf-8')); print(verify_structural_effect(report, root))"
python -m pytest analysis/tests/test_execution_slice.py analysis/tests/test_data_slice.py analysis/tests/test_explanation_model.py analysis/tests/test_explanation_semantic_extension.py --basetemp .pytest-tmp-examiner -q
```

The slice command should report `status: PASS`, 38 queries, 38 exact matches,
266 produced/truth node identities, 230 produced/truth relation identities, 98
authority entries, 12 typed-IR entries and no provenance errors. The structural
verifier should print `()`.

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
