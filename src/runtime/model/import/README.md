# Model import architecture

The installed package format is Mver. Other source formats must be converted
to an Mver package before they enter the installed-model catalog.

## Pipeline

1. `model_import_source.c` resolves the selected file or directory.
2. `model_import_discover.c` selects the source-format discovery module.
3. Identity and digest modules decide whether the package is already known.
4. Mver sources are copied while distribution-only files are filtered out.
5. Tauri sources are converted by the isolated `tauri/` module.
6. The prepared package is discovered again through the Mver module.
7. Mver metadata and assets are converted into the runtime adapter.

## Ownership

- Root files: shared workflow, storage, identity, scanning, and validation.
- `mver/`: canonical package discovery, copying, and runtime adaptation.
- `tauri/`: Tauri discovery and conversion to canonical Mver structure.
- `nearby/`: non-installing discovery and adapter caching.

## Invariants

- Native Mver imports never call Tauri conversion code.
- Tauri conversion ends by producing a valid Mver directory tree.
- Installed packages are validated through Mver discovery after preparation.
- Format-specific APIs stay in their format directories.
- Shared path and manifest helpers stay format-neutral.

## File source recognition

`model_import_source.c` validates the selected path and dispatches supported
file types. Directories pass through unchanged; configuration and model3
files resolve to their parent; image patches resolve through their `img` root.
Moc files delegate ownership lookup to `model_import_probe.c`.

The Live2D owner probe checks up to 12 ancestor directories, starting at the
Moc's parent. At each step the Mver adapter's package lookup runs before the
exact Tauri probe. A recognized invalid Mver package stops lookup. No sibling
or recursive container scan runs during file ownership lookup. Once resolved,
the package directory enters the same discovery and installation pipeline as
a dropped folder. File extension dispatch is case-insensitive.

`test_model_import_source.c` covers source validation and ownership lookup.
Container and Tauri portable tests cover installation identity equivalence
between Moc files and folders, including duplicate installation prevention.
Run `model-import-unit` after building to verify these behavioral checks.

## Mver manifest compatibility

`mver/model_import_mver_manifest.c` reads strict JSON first, then permits
comments and trailing commas. A single legacy extra `] }` immediately before
another `}` at the parser error position can be removed, provided the entire
result parses successfully. It does not guess missing values or truncate
unparseable content. Referenced assets still require validation.

Discovery and Mver metadata readers share this compatibility reader. During
package copying, repaired model3 manifests are serialized as standard JSON
inside the staging directory; original source files remain untouched. Tauri
and runtime validation stay strict. `test_mver_manifest.c` covers repair,
strict output, unchanged sources, duplicate imports, and rejection cases.
