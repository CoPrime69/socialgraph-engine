-- Data-driven taxonomy (contracts §10.3): technology domains and their primary-technology branches,
-- loaded from the dataset. `id` is the display order (file order); sg::graph::Profile codes are the
-- 0-based position in ORDER BY id.
CREATE TABLE categories (
  id          integer PRIMARY KEY CONSTRAINT categories_id_positive CHECK (id > 0),
  slug        text    NOT NULL CONSTRAINT categories_slug_key UNIQUE
                      CONSTRAINT categories_slug_format CHECK (slug ~ '^[a-z0-9][a-z0-9_-]{0,63}$'),
  name        text    NOT NULL CONSTRAINT categories_name_key UNIQUE
                      CONSTRAINT categories_name_length CHECK (char_length(name) BETWEEN 1 AND 64),
  description text    NOT NULL DEFAULT '' CONSTRAINT categories_description_length CHECK (char_length(description) <= 500)
);

CREATE TABLE branches (
  id            integer PRIMARY KEY CONSTRAINT branches_id_positive CHECK (id > 0),
  name          text    NOT NULL CONSTRAINT branches_name_key UNIQUE
                        CONSTRAINT branches_name_length CHECK (char_length(name) BETWEEN 1 AND 64),
  category_slug text    NOT NULL REFERENCES categories (slug)
);

CREATE INDEX branches_category_slug_idx ON branches (category_slug);
