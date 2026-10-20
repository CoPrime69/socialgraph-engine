-- graph_version (contracts §7): gap-free and commit-ordered. Every write transaction first locks
-- this single row (SELECT ... FOR UPDATE), then bumps it once if and only if it changes the graph,
-- and writes the matching event to the outbox; writers are therefore serialised and outbox id order,
-- version order and commit order coincide.
CREATE TABLE graph_meta (
  id      boolean PRIMARY KEY DEFAULT true CONSTRAINT graph_meta_single_row CHECK (id),
  version bigint  NOT NULL DEFAULT 0 CONSTRAINT graph_meta_version_range CHECK (version >= 0)
);
INSERT INTO graph_meta (id, version) VALUES (true, 0);

-- Transactional outbox, published to the Redis stream graph.events by the relay.
CREATE TABLE outbox (
  id           bigserial   PRIMARY KEY,
  type         text        NOT NULL,
  payload      jsonb       NOT NULL,
  occurred_at  timestamptz NOT NULL DEFAULT now(),
  published_at timestamptz
);
CREATE INDEX outbox_unpublished_idx ON outbox (id) WHERE published_at IS NULL;
