# Dataset: Stack Overflow, August–December 2008

The original project ran on a hand-written CSV of 100 fictional users whose connections were
regenerated at random on every start. The rebuilt system runs on a real social network instead: the
first five months of Stack Overflow, from the Stack Exchange data dump (**CC BY-SA 4.0**,
attribution: Stack Exchange, Inc. and the Stack Overflow contributors).

## Why this dataset

* **It is a real social graph with the original problem's attributes.** Every attribute the legacy
  program invented has a real counterpart. Its category becomes the member's technology domain, its
  branch becomes their primary tag, and its influence becomes their reputation percentile.
* **Connections are observed, not generated.** Two members are connected when one answered the
  other's question or commented on the other's post. Repeated interactions make a connection stronger.
* **It comes with related entities that make the relational schema meaningful.** These are the
  questions, answers, comments, badges, tags and per-member tag expertise.
* **It is reproducible.** The builder reads pinned Parquet files from ClickHouse's public S3 mirror
  of the dump. It writes deterministic gzip output (identical bytes on every run) and records a
  SHA-256 per file in `data/stackoverflow/manifest.json`.
* **It fits the size brief.** It is 23 MB compressed (77 MB raw) in the repository and 193 MB once
  loaded and indexed in PostgreSQL.

SNAP, Kaggle and Hugging Face were not reachable from the build environment, so the Stack Exchange
dump (via S3) was the real-world source that could be fetched and pinned.

## Files (`data/stackoverflow/*.csv.gz`)

| File | Rows | Loaded by | Contents |
|------|-----:|-----------|----------|
| `categories` | 10 | graph, content | Technology domains (Web Development, .NET & Windows, …) |
| `branches` | 73 | graph | Curated primary-technology tags and their domain |
| `users` | 18,648 | graph | Profile: reputation, influence percentile, location, website, about, views, votes, domain, primary tag |
| `connections` | 204,650 | graph | Pair, interaction count, first/last interaction, strength |
| `tags` | 9,890 | content | Tag, question count, domain |
| `questions` | 57,376 | content | Title, tags, score, views, answers, comments, favourites, accepted answer, closed date |
| `answers` | 208,230 | content | Question, owner, score, comments, accepted flag |
| `comments` | 117,417 | content | Post, author, score, text (≤ 600 chars) |
| `badges` | 211,627 | content | Gold/silver/bronze awards up to 2009-12-31 |
| `user_tags` | 437,229 | content | Per-member expertise: posts and score per tag |

Column definitions are in `docs/contracts.md` §10.1.

## How the network is derived

* **Members.** A member is anyone who asked, answered or commented between 2008-08-01 and
  2008-12-31 and still has a public account.
* **Domain and primary tag.** A member's primary tag is the curated branch tag they posted under
  most (`tools/dataset/taxonomy.py`). Its domain becomes their category. 473 members whose posts
  carry no curated tag fall back to Computer Science / `language-agnostic`.
* **Influence.** Influence is the member's reputation percentile, from 0 to 100.
* **Connections.** A connection is an unordered pair `(user_a < user_b)` with at least one answer or
  comment between them. Self-interactions are dropped.
* **Strength.** Strength is `round3(0.5·affinity + 0.5·(1 − e^(−interactions/3)))`. Affinity is the
  profile similarity from contracts §8. The result blends "how alike they are" with "how much they
  actually talked".

## Shape of the graph (measured from the files)

| Property | Value |
|----------|------:|
| Nodes / edges | 18,648 / 204,650 |
| Mean / median degree | 21.95 / 7 |
| 99th percentile / max degree | 215 / 1,360 |
| Connected components | 5 (largest: 18,640 members) |
| Isolated members | 0 |
| Pairs with ≥ 3 interactions | 10,051 |
| Mean strength | 0.396 |

The degree distribution is heavy-tailed: a few prolific answerers touch over a thousand members.
That is the realistic load the analytics algorithms (Brandes betweenness in particular) are
benchmarked on.

Members per domain: .NET & Windows 5,077 · Web Development 3,929 · Databases & Data 2,120 ·
Systems Programming 1,830 · Computer Science 1,595 · Java & JVM 1,557 · Scripting Languages 1,421 ·
DevOps & Tooling 543 · Mobile & Apple 391 · Security & Networking 185.

## Rebuilding and verifying

```sh
pip install pyarrow pandas
python3 tools/dataset/build_stackoverflow_dataset.py           # rebuilds data/stackoverflow (~2 min)
python3 tools/dataset/build_stackoverflow_dataset.py --verify  # checks files against manifest.json
```

## Limitations

* The data is a snapshot of 2008 activity. Profile counters (reputation, views, votes) are as of the
  2024 dump the mirror was built from, so they reflect each member's whole career.
* Imported members are profiles, not accounts, so they cannot sign in. People who register through
  the app join the same graph, with ids from 100,000,000 upward.
* Comment text is truncated to 600 characters, and question and answer bodies are not included.
  That keeps the repository near the 20 MB budget.
