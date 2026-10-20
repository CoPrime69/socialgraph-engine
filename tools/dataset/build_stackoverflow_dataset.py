#!/usr/bin/env python3
"""Builds data/stackoverflow/ from the public Stack Overflow data dump.

Source: the Stack Exchange data dump (CC BY-SA 4.0) as Parquet files in ClickHouse's public
documentation bucket (s3://datasets-documentation/stackoverflow/parquet/, region eu-west-3,
anonymous access). Only the year-2008 posts/comments files plus the matching users and badges are
read, using column projection and predicate push-down, so the build downloads ~1 GB instead of the
full dump.

What it produces (see docs/contracts.md §10 and docs/dataset.md):
  * the people who interacted on Stack Overflow between Aug and Dec 2008 (answered someone's
    question or commented on someone's post) and their profile attributes;
  * one undirected connection per interacting pair, with interaction counts and timestamps;
  * their questions, answers, comments, badges (to 2009-12-31), tags and per-user tag expertise;
  * a curated category ("technology domain") and branch ("primary technology") per user.

Output is deterministic: rows are sorted, gzip headers carry no timestamp, and manifest.json records
SHA-256 checksums and row counts.

Usage: python3 -m pip install pyarrow pandas && python3 tools/dataset/build_stackoverflow_dataset.py
"""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import html
import io
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import pandas as pd
import pyarrow.compute as pc
import pyarrow.dataset as ds
import pyarrow.fs as pafs
import pyarrow.parquet as pq

sys.path.insert(0, str(Path(__file__).resolve().parent))
from taxonomy import BRANCHES, CATEGORIES, FALLBACK, tag_category  # noqa: E402

BUCKET = "datasets-documentation/stackoverflow/parquet"
REGION = "eu-west-3"
SOURCES = {
    "posts": f"{BUCKET}/posts/2008.parquet",
    "comments": f"{BUCKET}/comments/2008.parquet",
    "users": f"{BUCKET}/users.parquet",
    "badges": f"{BUCKET}/badges.parquet",
}
BADGES_UNTIL = pd.Timestamp("2010-01-01", tz="UTC")
ABOUT_MAX = 500
COMMENT_MAX = 300
COMMENT_WEIGHT = 0.25  # how much commenting on a post counts towards a user's topical profile


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def text(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def strip_html(value: str, limit: int) -> str:
    plain = html.unescape(re.sub(r"<[^>]+>", " ", value))
    plain = re.sub(r"\s+", " ", plain).strip()
    if len(plain) > limit:
        plain = plain[: limit - 1].rstrip() + "…"
    return plain


def iso(ts) -> str:
    if ts is None or pd.isna(ts) or ts.year <= 1970:
        return ""
    return ts.strftime("%Y-%m-%dT%H:%M:%SZ")


def handle_for(name: str, user_id: int) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")[:20].strip("_") or "user"
    return f"{slug}_{user_id}"


def read_parquet(fs, key: str, columns: list[str], filt=None) -> pd.DataFrame:
    dataset = ds.dataset(key, filesystem=fs, format="parquet")
    table = dataset.to_table(columns=columns, filter=filt)
    return table.to_pandas()


def write_csv(out_dir: Path, name: str, header: list[str], rows) -> dict:
    """Writes a gzip CSV with a fixed gzip mtime so checksums are reproducible."""
    path = out_dir / f"{name}.csv.gz"
    buffer = io.StringIO()
    writer = csv.writer(buffer, lineterminator="\n")
    writer.writerow(header)
    count = 0
    for row in rows:
        writer.writerow(row)
        count += 1
    raw = buffer.getvalue().encode("utf-8")
    with open(path, "wb") as fh, gzip.GzipFile(fileobj=fh, mode="wb", mtime=0, filename="") as gz:
        gz.write(raw)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    log(f"  {path.name}: {count} rows, {len(raw) / 1e6:.1f} MB raw, {path.stat().st_size / 1e6:.1f} MB gz")
    return {"file": path.name, "rows": count, "bytes_uncompressed": len(raw),
            "bytes": path.stat().st_size, "sha256": digest}


def build(out_dir: Path) -> None:
    fs = pafs.S3FileSystem(anonymous=True, region=REGION)
    out_dir.mkdir(parents=True, exist_ok=True)
    tag_to_category = tag_category()
    branch_set = {tag for tags in BRANCHES.values() for tag in tags}

    log("reading 2008 posts and comments")
    posts = read_parquet(fs, SOURCES["posts"], [
        "Id", "PostTypeId", "AcceptedAnswerId", "CreationDate", "Score", "ViewCount", "OwnerUserId",
        "Title", "Tags", "AnswerCount", "CommentCount", "FavoriteCount", "ParentId", "ClosedDate"])
    comments = read_parquet(fs, SOURCES["comments"], ["Id", "PostId", "Score", "CreationDate", "UserId", "Text"])

    posts["ParentId"] = pd.to_numeric(posts["ParentId"].map(text).replace("", np.nan)).astype("Int64")
    questions = posts[posts.PostTypeId == 1].copy()
    answers = posts[(posts.PostTypeId == 2) & posts.ParentId.isin(questions.Id)].copy()
    questions["tag_list"] = questions.Tags.map(lambda t: [x for x in text(t).strip("|").split("|") if x])
    question_tags = dict(zip(questions.Id, questions.tag_list))
    owner_of_post = pd.concat([questions.set_index("Id").OwnerUserId, answers.set_index("Id").OwnerUserId])
    question_of_post = {**{q: q for q in questions.Id}, **dict(zip(answers.Id, answers.ParentId))}

    log("deriving the interaction graph")
    a_edges = pd.DataFrame({"u": answers.OwnerUserId.values,
                            "v": owner_of_post.reindex(answers.ParentId.values).values,
                            "at": answers.CreationDate.values})
    c_edges = pd.DataFrame({"u": comments.UserId.values,
                            "v": owner_of_post.reindex(comments.PostId.values).values,
                            "at": comments.CreationDate.values})
    edges = pd.concat([a_edges, c_edges]).dropna()
    edges = edges[(edges.u > 0) & (edges.v > 0) & (edges.u != edges.v)]
    edges["a"] = edges[["u", "v"]].min(axis=1).astype(np.int64)
    edges["b"] = edges[["u", "v"]].max(axis=1).astype(np.int64)
    pairs = edges.groupby(["a", "b"]).agg(interactions=("at", "size"), first=("at", "min"), last=("at", "max"))
    pairs = pairs.reset_index().sort_values(["a", "b"])
    user_ids = sorted(set(pairs.a) | set(pairs.b))
    user_set = set(user_ids)
    log(f"  {len(user_ids)} users, {len(pairs)} connections, {len(edges)} interactions")

    log("reading user profiles")
    users = read_parquet(fs, SOURCES["users"], [
        "Id", "Reputation", "CreationDate", "DisplayName", "LastAccessDate", "AboutMe", "Views", "UpVotes",
        "DownVotes", "WebsiteUrl", "Location"], pc.field("Id").isin(user_ids))
    users = users.drop_duplicates("Id").set_index("Id").reindex(user_ids)
    missing = users.DisplayName.isna().sum()
    if missing:
        raise SystemExit(f"{missing} interacting users have no profile row")
    users["reputation"] = pd.to_numeric(users.Reputation.map(text)).astype(np.int64)

    log("assigning categories and branches from topical activity")
    weight: dict[int, defaultdict[str, float]] = defaultdict(lambda: defaultdict(float))
    expertise: dict[tuple[int, str], list[int]] = defaultdict(lambda: [0, 0])
    for qid, owner, score in zip(questions.Id, questions.OwnerUserId, questions.Score):
        for tag in question_tags[qid]:
            if owner in user_set:
                weight[owner][tag] += 1.0
                e = expertise[(owner, tag)]
                e[0] += 1
                e[1] += int(score)
    for parent, owner, score in zip(answers.ParentId, answers.OwnerUserId, answers.Score):
        for tag in question_tags.get(parent, []):
            if owner in user_set:
                weight[owner][tag] += 1.0
                e = expertise[(owner, tag)]
                e[0] += 1
                e[1] += int(score)
    for post_id, user in zip(comments.PostId, comments.UserId):
        if user in user_set and post_id in question_of_post:
            for tag in question_tags.get(question_of_post[post_id], []):
                weight[user][tag] += COMMENT_WEIGHT

    category_name = {slug: name for slug, name, _ in CATEGORIES}
    assignments: dict[int, tuple[str, str]] = {}
    fallbacks = 0
    for uid in user_ids:
        w = weight.get(uid, {})
        cat_w: defaultdict[str, float] = defaultdict(float)
        for tag, value in w.items():
            if tag in tag_to_category:
                cat_w[tag_to_category[tag]] += value
        if not cat_w:
            assignments[uid] = FALLBACK
            fallbacks += 1
            continue
        category = max(sorted(cat_w), key=lambda c: cat_w[c])
        candidates = [t for t in BRANCHES[category] if w.get(t, 0) > 0]
        branch = max(candidates, key=lambda t: (w[t], -BRANCHES[category].index(t))) if candidates \
            else BRANCHES[category][0]
        assignments[uid] = (category, branch)
    log(f"  {fallbacks} users had no mapped tags and use the fallback {FALLBACK}")

    # Influence = reputation percentile (mid-rank), 0..100.
    rep = users.reputation
    influence = (rep.rank(method="average") - 0.5) / len(rep) * 100.0

    def affinity(u: int, v: int) -> float:
        cu, bu = assignments[u]
        cv, bv = assignments[v]
        return (0.4 * (cu == cv) + 0.3 * (bu == bv)
                + 0.3 * (1.0 - abs(influence[u] - influence[v]) / 100.0))

    log("writing files")
    manifest = {"dataset": "stackoverflow-2008", "license": "CC BY-SA 4.0",
                "attribution": "Stack Exchange, Inc. and Stack Overflow contributors, Stack Exchange Data Dump",
                "source": {k: f"s3://{v} (region {REGION}, anonymous)" for k, v in SOURCES.items()},
                "builder": "tools/dataset/build_stackoverflow_dataset.py", "files": []}
    files = manifest["files"]

    files.append(write_csv(out_dir, "categories", ["slug", "name", "description"], CATEGORIES))
    files.append(write_csv(out_dir, "branches", ["name", "category_slug"],
                           [(t, slug) for slug, _, _ in CATEGORIES for t in BRANCHES[slug]]))

    def user_rows():
        for uid in user_ids:
            u = users.loc[uid]
            name = text(u.DisplayName).strip() or f"user{uid}"
            category, branch = assignments[uid]
            website = text(u.WebsiteUrl).strip()
            if not re.match(r"^https?://", website):
                website = ""
            yield (uid, handle_for(name, uid), name, int(u.reputation), f"{influence[uid]:.2f}",
                   iso(u.CreationDate), iso(u.LastAccessDate), text(u.Location).strip()[:100], website[:200],
                   strip_html(text(u.AboutMe), ABOUT_MAX), int(u.Views), int(u.UpVotes), int(u.DownVotes),
                   category_name[category], branch)
    files.append(write_csv(out_dir, "users", [
        "id", "handle", "display_name", "reputation", "influence", "created_at", "last_access_at", "location",
        "website_url", "about", "views", "up_votes", "down_votes", "category", "branch"], user_rows()))

    def connection_rows():
        for a, b, k, first, last in pairs[["a", "b", "interactions", "first", "last"]].itertuples(index=False):
            strength = 0.5 * affinity(a, b) + 0.5 * (1.0 - math.exp(-k / 3.0))
            yield a, b, k, iso(first), iso(last), f"{strength:.3f}"
    files.append(write_csv(out_dir, "connections", [
        "user_a", "user_b", "interactions", "first_interaction_at", "last_interaction_at", "strength"],
        connection_rows()))

    tag_counts: defaultdict[str, int] = defaultdict(int)
    for tags in questions.tag_list:
        for tag in tags:
            tag_counts[tag] += 1
    files.append(write_csv(out_dir, "tags", ["name", "question_count", "category_slug"],
                           [(t, n, tag_to_category.get(t, ""))
                            for t, n in sorted(tag_counts.items(), key=lambda x: (-x[1], x[0]))]))

    def owner(value) -> str:
        return str(int(value)) if value in user_set else ""

    questions_sorted = questions.sort_values("Id")
    files.append(write_csv(out_dir, "questions", [
        "id", "owner_id", "created_at", "title", "tags", "score", "view_count", "answer_count", "comment_count",
        "favorite_count", "accepted_answer_id", "closed_at"],
        ((q.Id, owner(q.OwnerUserId), iso(q.CreationDate), text(q.Title), " ".join(q.tag_list), q.Score,
          q.ViewCount, q.AnswerCount, q.CommentCount, q.FavoriteCount,
          q.AcceptedAnswerId if q.AcceptedAnswerId else "", iso(q.ClosedDate))
         for q in questions_sorted.itertuples(index=False))))

    accepted = set(questions.AcceptedAnswerId[questions.AcceptedAnswerId > 0])
    files.append(write_csv(out_dir, "answers", [
        "id", "question_id", "owner_id", "created_at", "score", "comment_count", "is_accepted"],
        ((a.Id, int(a.ParentId), owner(a.OwnerUserId), iso(a.CreationDate), a.Score, a.CommentCount,
          "true" if a.Id in accepted else "false")
         for a in answers.sort_values("Id").itertuples(index=False))))

    post_ids = set(questions.Id) | set(answers.Id)
    comments_kept = comments[comments.PostId.isin(post_ids)].sort_values("Id")
    files.append(write_csv(out_dir, "comments", ["id", "post_id", "user_id", "created_at", "score", "text"],
                           ((c.Id, c.PostId, owner(c.UserId), iso(c.CreationDate), c.Score,
                             strip_html(text(c.Text), COMMENT_MAX))
                            for c in comments_kept.itertuples(index=False))))

    log("reading badges")
    badges = read_parquet(fs, SOURCES["badges"], ["Id", "UserId", "Name", "Date", "Class", "TagBased"],
                          pc.field("UserId").isin(user_ids) & (pc.field("Date") < BADGES_UNTIL))
    files.append(write_csv(out_dir, "badges", ["id", "user_id", "name", "class", "tag_based", "awarded_at"],
                           ((b.Id, b.UserId, text(b.Name), b.Class, "true" if b.TagBased else "false",
                             iso(b.Date)) for b in badges.sort_values("Id").itertuples(index=False))))

    files.append(write_csv(out_dir, "user_tags", ["user_id", "tag", "posts", "score"],
                           ((uid, tag, v[0], v[1]) for (uid, tag), v in sorted(expertise.items()))))

    manifest["summary"] = {f["file"].removesuffix(".csv.gz"): f["rows"] for f in files}
    manifest["total_bytes_uncompressed"] = sum(f["bytes_uncompressed"] for f in files)
    manifest["total_bytes"] = sum(f["bytes"] for f in files)
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    log(f"done: {manifest['total_bytes'] / 1e6:.1f} MB compressed, "
        f"{manifest['total_bytes_uncompressed'] / 1e6:.1f} MB uncompressed")


def verify(out_dir: Path) -> None:
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for entry in manifest["files"]:
        digest = hashlib.sha256((out_dir / entry["file"]).read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise SystemExit(f"checksum mismatch for {entry['file']}")
    print(f"all {len(manifest['files'])} files match manifest.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "data" / "stackoverflow")
    parser.add_argument("--verify", action="store_true", help="only verify checksums against manifest.json")
    args = parser.parse_args()
    verify(args.out) if args.verify else build(args.out)
