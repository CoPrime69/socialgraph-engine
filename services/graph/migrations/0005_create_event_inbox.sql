-- Consumed events (identity.events): processed_events makes the handler idempotent under
-- at-least-once delivery; events that can never be applied are kept in dead_letter_events.
CREATE TABLE processed_events (
  stream       text        NOT NULL,
  event_id     text        NOT NULL,
  processed_at timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (stream, event_id)
);

CREATE TABLE dead_letter_events (
  id          bigserial   PRIMARY KEY,
  stream      text        NOT NULL,
  entry_id    text        NOT NULL,
  fields      jsonb       NOT NULL,
  error       text        NOT NULL,
  received_at timestamptz NOT NULL DEFAULT now()
);
