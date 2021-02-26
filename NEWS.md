Release 0.4.0
-------------

- More precise ticks management.
- Combinatorial tester `gencombi.c` uses the incremental feature for
  scalability (needs it with all those new features).
- Limited incremental mode (conflict limited but not allowing to add new
  clauses nor assumptions).
- Original, inverted (INVERTED) and best (BEST) rephasing.
- Made phase saving a feature (SAVE).
- Initial phase explicitly set to "true" (feature TRUE).
- Bumping reason side literals (REASON).
- By default using target phases in stable mode (TARGET).
- Renamed COMPACT feature to VIRTUAL (binary clause) feature.
- Three tier clause reduction scheme (including new features
  GLUE,TIER1,TIER2, and USED to control clause reduction).
- Allowing conflict limits including a `--conflicts=<limit>` solver option.
- Much more precise interaction of VMTF, VSIDS, STABLE and FOCUSED features
  (see comments in [features.h](features.h) for more details).
- Removed old combinatorial testing script `combi.sh` (now completely relying on
  `gencombi` compiled from `gencombi.c`).
- New [`features/generate.c`](features/generate.c) program to automatically
  generated sub-scripts for [`configure`](configure) and header files for
  [`features.h`](features.h) and [`gencombi.c`](gencombi.c) from feature
  lists in `features` directory (see
  [features/README.md](features/README.md) for details).
