-- Transactional outbox (contracts §7): events are written in the same transaction as the state
-- change and published to the Redis stream identity.events by the relay.
CREATE TABLE outbox (
  id           bigserial   PRIMARY KEY,
  type         text        NOT NULL,
  payload      jsonb       NOT NULL,
  occurred_at  timestamptz NOT NULL DEFAULT now(),
  published_at timestamptz
);

CREATE INDEX outbox_unpublished_idx ON outbox (id) WHERE published_at IS NULL;
