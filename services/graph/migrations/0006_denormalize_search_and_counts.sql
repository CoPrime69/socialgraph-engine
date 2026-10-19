-- Member search and connection counts without per-request recomputation.
--
-- 1. Lower-cased copies of handle and name with trigram indexes. The search used expression indexes
--    on lower(...); for short patterns the planner preferred a sequential scan that called lower()
--    twice per profile (24 ms on the full dataset). Stored columns make both the index scan and
--    any fallback scan cheap.
-- 2. connection_count on profiles, maintained by statement-level triggers on connections (one
--    aggregated UPDATE per INSERT/DELETE statement, so bulk imports and group-committed batches
--    stay cheap). Every page of profiles used to count each member's connections with two
--    subqueries.

ALTER TABLE profiles
    ADD COLUMN handle_lower text GENERATED ALWAYS AS (lower(handle)) STORED,
    ADD COLUMN name_lower   text GENERATED ALWAYS AS (lower(name)) STORED,
    ADD COLUMN connection_count integer NOT NULL DEFAULT 0 CHECK (connection_count >= 0);

DROP INDEX profiles_handle_trgm_idx;
DROP INDEX profiles_name_trgm_idx;
CREATE INDEX profiles_handle_lower_trgm_idx ON profiles USING gin (handle_lower gin_trgm_ops);
CREATE INDEX profiles_name_lower_trgm_idx ON profiles USING gin (name_lower gin_trgm_ops);

UPDATE profiles p SET connection_count = c.n
FROM (SELECT u, count(*) AS n FROM (SELECT user_a AS u FROM connections UNION ALL SELECT user_b FROM connections) e
      GROUP BY u) c
WHERE p.user_id = c.u;

CREATE FUNCTION connections_count_added() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    UPDATE profiles p SET connection_count = p.connection_count + d.n
    FROM (SELECT u, count(*) AS n FROM (SELECT user_a AS u FROM added UNION ALL SELECT user_b FROM added) e
          GROUP BY u) d
    WHERE p.user_id = d.u;
    RETURN NULL;
END $$;

CREATE FUNCTION connections_count_removed() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
    UPDATE profiles p SET connection_count = p.connection_count - d.n
    FROM (SELECT u, count(*) AS n FROM (SELECT user_a AS u FROM removed UNION ALL SELECT user_b FROM removed) e
          GROUP BY u) d
    WHERE p.user_id = d.u;
    RETURN NULL;
END $$;

CREATE TRIGGER connections_count_added AFTER INSERT ON connections
    REFERENCING NEW TABLE AS added FOR EACH STATEMENT EXECUTE FUNCTION connections_count_added();
CREATE TRIGGER connections_count_removed AFTER DELETE ON connections
    REFERENCING OLD TABLE AS removed FOR EACH STATEMENT EXECUTE FUNCTION connections_count_removed();
