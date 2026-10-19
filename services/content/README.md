# content-service

Read-only API over the Stack Overflow activity behind every member of the network: questions,
answers, comments, badges and tags. It owns the `content` database and nothing else; members are
referenced by id only (contracts.md §10.6).

| Route | What it returns |
|-------|-----------------|
| `GET /api/v1/users/{id}/activity` | Totals, top tags, badge counts, recent badges and top posts of one member |
| `GET /api/v1/tags?q=&limit=&offset=` | Tags by popularity, optional name prefix (`%`/`_` are matched literally) |
| `GET /api/v1/questions?tag=&q=&sort=score\|recent&limit=&offset=` | Questions filtered by tag and full-text search over titles |
| `GET /api/v1/questions/{id}` | One question with its answers, accepted answer first |

All routes need a bearer token issued by identity-service. Free-text parameters are trimmed,
limited to 100 characters and may not contain control characters; `limit` is 1–100.

## Schema

`migrations/0001_create_content_schema.sql`: `categories`, `authors`, `tags`, `questions`
(generated `tsvector` column with a GIN index), `question_tags`, `answers`, `comments`, `badges`,
`user_tags`. Every reference is a foreign key; `questions.accepted_answer_id → answers` is
`DEFERRABLE INITIALLY DEFERRED` because the two tables reference each other.

## Seeding

With `SEED_ON_EMPTY=true`, `serve` imports `SEED_DIR` (default `data/stackoverflow`) into an empty
database in one transaction using `COPY` (`pqxx::stream_to`), under an advisory lock so concurrent
replicas import once. `content-service seed` does the same on demand. Rows pointing at members or
posts outside the sample are skipped and dangling accepted-answer ids are cleared, so the import
never violates a constraint. Loading the full sample (1.23 M rows) took 27 s with a Debug build on
the development container.

## Running and testing

```sh
CONTENT_DATABASE_URL=postgresql://user:pass@localhost:5432/content \
AUTH_PUBLIC_KEY=... SEED_ON_EMPTY=true content-service serve
```

`content_tests` runs the HTTP layer against an in-memory repository, and the PostgreSQL repository
against the fixtures in `tests/fixtures` when `SG_TEST_DATABASE_URL` is set (skipped otherwise).
