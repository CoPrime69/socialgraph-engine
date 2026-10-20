# Stack Overflow 2008 dataset

Real data from the Stack Exchange data dump, licensed **CC BY-SA 4.0**
(attribution: Stack Exchange, Inc. and the Stack Overflow contributors). It covers everyone who
interacted on Stack Overflow from its public launch (Aug 2008) to 31 Dec 2008. Profile attributes
(reputation, views, votes, last access) are as of the dump the mirror was built from (2024).

Built by [`tools/dataset/build_stackoverflow_dataset.py`](../../tools/dataset/build_stackoverflow_dataset.py)
from pinned Parquet files; `manifest.json` lists sources, row counts and SHA-256 checksums
(`python3 tools/dataset/build_stackoverflow_dataset.py --verify`). Column definitions: `docs/contracts.md` §10
and `docs/dataset.md`.
