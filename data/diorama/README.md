# Diorama rule sources

These JSON documents are strict offline sources compiled by
`tools/diorama_rules/compile_rules.py`. The game never parses them at runtime.

R0 intentionally contains no terrain classifications, behavior geometry, metatile pins,
coordinate anchors, profiles, patterns or generated terrain records from the superseded
implementation. Every one of the 518 maps and 441 layouts remains cataloged and eligible
for the renderer, but unresolved content uses the local flat fallback. This is a neutral
starting point, not an assertion that the map has been interpreted.

R1 extracts the complete read-only corpus with `tools/diorama_rules/catalog.py` and builds
the global visual survey plan with `tools/diorama_survey/survey.py`. Generated catalogs,
plans, captures and run manifests belong under `build/` and are never versioned. The
survey observes a location reached through normal gameplay; it has no save loader,
teleport command or game-state setup API.

The phased Red-equivalent pipeline in `TODOLIST_DIORAMA_RED_PARITY.md` will add rules only
after the corresponding global extractor, classifier, claims, pixel analysis and runtime
IR are implemented and approved. Rules must be reusable and their blast radius must be
surveyed across every placement. Gameplay collision, elevation, scripts and events remain
read-only evidence.

Validate and regenerate with:

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/compile_rules.py --check
python3 tools/diorama_survey/survey.py plan
make -f Makefile_pc test-diorama-red-parity
```
