-- User profiles (contracts §5, §10.5): imported Stack Overflow users (ids < 100,000,000) and members
-- registered through identity-service (ids >= 100,000,000, contracts §10.2).
CREATE TABLE profiles (
  user_id        bigint       PRIMARY KEY,
  handle         text         NOT NULL CONSTRAINT profiles_handle_format CHECK (handle ~ '^[a-z0-9_]{3,32}$'),
  name           text         NOT NULL CONSTRAINT profiles_name_length CHECK (char_length(name) BETWEEN 1 AND 64),
  category       text         NOT NULL REFERENCES categories (name),
  branch         text         NOT NULL REFERENCES branches (name),
  influence      numeric(5,2) NOT NULL DEFAULT 0 CONSTRAINT profiles_influence_range CHECK (influence BETWEEN 0 AND 100),
  reputation     integer      NOT NULL DEFAULT 1 CONSTRAINT profiles_reputation_range CHECK (reputation >= 0),
  location       text         CONSTRAINT profiles_location_length CHECK (char_length(location) <= 100),
  website_url    text         CONSTRAINT profiles_website_url_length CHECK (char_length(website_url) <= 300),
  about          text         CONSTRAINT profiles_about_length CHECK (char_length(about) <= 500),
  views          integer      NOT NULL DEFAULT 0 CONSTRAINT profiles_views_range CHECK (views >= 0),
  up_votes       integer      NOT NULL DEFAULT 0 CONSTRAINT profiles_up_votes_range CHECK (up_votes >= 0),
  down_votes     integer      NOT NULL DEFAULT 0 CONSTRAINT profiles_down_votes_range CHECK (down_votes >= 0),
  source         text         NOT NULL CONSTRAINT profiles_source_values CHECK (source IN ('stackoverflow', 'member')),
  created_at     timestamptz  NOT NULL DEFAULT now(),
  last_access_at timestamptz,
  CONSTRAINT profiles_user_id_positive CHECK (user_id > 0),
  -- The two id spaces never overlap (contracts §10.2).
  CONSTRAINT profiles_id_space CHECK ((source = 'member') = (user_id >= 100000000))
);

-- Handles are unique within each source. identity-service guarantees unique member handles but
-- cannot see imported ones, so a member may legitimately pick the handle of a Stack Overflow user.
CREATE UNIQUE INDEX profiles_member_handle_key ON profiles (handle) WHERE source = 'member';
CREATE UNIQUE INDEX profiles_imported_handle_key ON profiles (handle) WHERE source = 'stackoverflow';

-- GET /users default order (reputation desc, ties by id) and its category/branch filtered variants;
-- the latter two also serve the foreign keys.
CREATE INDEX profiles_reputation_idx ON profiles (reputation DESC, user_id);
CREATE INDEX profiles_category_reputation_idx ON profiles (category, reputation DESC, user_id);
CREATE INDEX profiles_branch_reputation_idx ON profiles (branch, reputation DESC, user_id);

-- GET /users?q= is a case-insensitive substring match on handle or name. Trigram indexes turn the
-- 18k-row sequential scan (~24 ms) into bitmap index scans (~0.4-3 ms) for patterns of 3+ characters.
-- pg_trgm is a trusted extension: the database owner may create it.
CREATE EXTENSION IF NOT EXISTS pg_trgm;
CREATE INDEX profiles_handle_trgm_idx ON profiles USING gin (lower(handle) gin_trgm_ops);
CREATE INDEX profiles_name_trgm_idx ON profiles USING gin (lower(name) gin_trgm_ops);
