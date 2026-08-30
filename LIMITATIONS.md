# Technical limitations

Blueprint Lens provides deterministic, graph-local static slices over the
explicit nodes, pins and relations exported by its typed representation. The
retained evidence supports the bounded queries and fixtures in this repository;
it does not establish language-wide soundness for arbitrary Blueprint projects.

## Supported boundary

- Execution queries walk explicit incoming execution relations from a valid
  criterion with an execution pin.
- Data queries collect same-member writes and their explicit value, predicate
  and controller dependencies within the exported graph.
- Every typed node is labelled `supported`, `opaque`, `unsupported` or
  `uncertain`. A non-supported node is retained as a visible boundary instead
  of being silently treated as understood.

## Constructs requiring further semantics

- Ordinary function calls remain opaque because callee bodies, call-site
  bindings and effect summaries are not exported.
- Macro instances remain opaque because the macro body and tunnel mapping are
  not expanded.
- Latent calls such as `Delay` remain unsupported because scheduler and resume
  semantics are outside the static model.
- Unlisted promoted operators and `Select` nodes remain uncertain until their
  pin and selection semantics have dedicated fixtures and oracles.
- Dispatcher calls and component-bound events are retained in source material,
  but the admitted ten-graph corpus does not establish listener or binding
  resolution.
- Cross-Blueprint dispatch, interfaces, overrides, timers and runtime causal
  traces are outside the current evidence boundary.

## Evidence limits

The 38-query result establishes exact agreement with the retained truth for the
registered execution and member-level Data questions. The demonstration images
establish the painted panel state at capture time. Neither result measures
runtime causality, performance, capacity, usability, human comprehension,
comparative superiority or a product-default design.
