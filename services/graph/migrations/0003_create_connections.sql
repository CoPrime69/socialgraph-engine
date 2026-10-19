-- Undirected connections, stored once with user_a < user_b (contracts §5, §10.5).
-- Imported ones are aggregated Stack Overflow interactions; members create theirs via the API.
CREATE TABLE connections (
  user_a               bigint       NOT NULL REFERENCES profiles (user_id) ON DELETE CASCADE,
  user_b               bigint       NOT NULL REFERENCES profiles (user_id) ON DELETE CASCADE,
  strength             numeric(4,3) NOT NULL CONSTRAINT connections_strength_range CHECK (strength BETWEEN 0 AND 1),
  interactions         integer      NOT NULL DEFAULT 0 CONSTRAINT connections_interactions_range CHECK (interactions >= 0),
  origin               text         NOT NULL CONSTRAINT connections_origin_values CHECK (origin IN ('imported', 'member')),
  created_at           timestamptz  NOT NULL DEFAULT now(),
  first_interaction_at timestamptz,
  last_interaction_at  timestamptz,
  PRIMARY KEY (user_a, user_b),
  CONSTRAINT connections_ordered CHECK (user_a < user_b)
);

-- Neighbours of u are (user_a = u) via the primary key plus (user_b = u) via this index.
CREATE INDEX connections_user_b_idx ON connections (user_b);
