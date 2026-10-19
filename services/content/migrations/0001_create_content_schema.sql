-- content-service owns the imported Stack Overflow content (contracts §10.6).
-- Users are owned by graph-service; `authors` is a local read projection used for display only.

CREATE TABLE categories (
    slug        text PRIMARY KEY CHECK (slug ~ '^[a-z0-9-]{1,32}$'),
    name        text NOT NULL UNIQUE,
    position    smallint NOT NULL UNIQUE
);

CREATE TABLE authors (
    user_id      bigint PRIMARY KEY CHECK (user_id > 0),
    display_name text NOT NULL CHECK (length(display_name) BETWEEN 1 AND 200),
    reputation   integer NOT NULL CHECK (reputation >= 0)
);

CREATE TABLE tags (
    id             integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name           text NOT NULL UNIQUE CHECK (length(name) BETWEEN 1 AND 64),
    question_count integer NOT NULL DEFAULT 0 CHECK (question_count >= 0),
    category_slug  text REFERENCES categories (slug)
);
CREATE INDEX tags_question_count_idx ON tags (question_count DESC, name);
CREATE INDEX tags_name_pattern_idx ON tags (name text_pattern_ops);

CREATE TABLE questions (
    id                 bigint PRIMARY KEY,
    owner_id           bigint REFERENCES authors (user_id) ON DELETE SET NULL,
    created_at         timestamptz NOT NULL,
    title              text NOT NULL CHECK (length(title) BETWEEN 1 AND 300),
    score              integer NOT NULL,
    view_count         integer NOT NULL CHECK (view_count >= 0),
    answer_count       integer NOT NULL CHECK (answer_count >= 0),
    comment_count      integer NOT NULL CHECK (comment_count >= 0),
    favorite_count     integer NOT NULL CHECK (favorite_count >= 0),
    accepted_answer_id bigint,
    closed_at          timestamptz,
    search             tsvector GENERATED ALWAYS AS (to_tsvector('english', title)) STORED
);
CREATE INDEX questions_score_idx ON questions (score DESC, id);
CREATE INDEX questions_recent_idx ON questions (created_at DESC, id DESC);
CREATE INDEX questions_owner_idx ON questions (owner_id, score DESC);
CREATE INDEX questions_search_idx ON questions USING gin (search);

CREATE TABLE question_tags (
    question_id bigint  NOT NULL REFERENCES questions (id) ON DELETE CASCADE,
    tag_id      integer NOT NULL REFERENCES tags (id),
    position    smallint NOT NULL CHECK (position >= 0),
    PRIMARY KEY (question_id, tag_id)
);
CREATE INDEX question_tags_tag_idx ON question_tags (tag_id, question_id);

CREATE TABLE answers (
    id            bigint PRIMARY KEY,
    question_id   bigint NOT NULL REFERENCES questions (id) ON DELETE CASCADE,
    owner_id      bigint REFERENCES authors (user_id) ON DELETE SET NULL,
    created_at    timestamptz NOT NULL,
    score         integer NOT NULL,
    comment_count integer NOT NULL CHECK (comment_count >= 0),
    is_accepted   boolean NOT NULL DEFAULT false
);
CREATE INDEX answers_question_idx ON answers (question_id, is_accepted DESC, score DESC);
CREATE INDEX answers_owner_idx ON answers (owner_id, score DESC);

-- An accepted answer must exist; checked at commit so questions and answers can be loaded in any order.
ALTER TABLE questions ADD CONSTRAINT questions_accepted_answer_fk
    FOREIGN KEY (accepted_answer_id) REFERENCES answers (id) ON DELETE SET NULL DEFERRABLE INITIALLY DEFERRED;

CREATE TABLE comments (
    id          bigint PRIMARY KEY,
    post_id     bigint NOT NULL,                       -- question or answer id
    question_id bigint NOT NULL REFERENCES questions (id) ON DELETE CASCADE,
    user_id     bigint REFERENCES authors (user_id) ON DELETE SET NULL,
    created_at  timestamptz NOT NULL,
    score       integer NOT NULL CHECK (score >= 0),
    body        text NOT NULL CHECK (length(body) <= 600)
);
CREATE INDEX comments_question_idx ON comments (question_id);
CREATE INDEX comments_user_idx ON comments (user_id);

CREATE TABLE badges (
    id         bigint PRIMARY KEY,
    user_id    bigint NOT NULL REFERENCES authors (user_id) ON DELETE CASCADE,
    name       text NOT NULL CHECK (length(name) BETWEEN 1 AND 100),
    class      smallint NOT NULL CHECK (class IN (1, 2, 3)),   -- 1 gold, 2 silver, 3 bronze
    tag_based  boolean NOT NULL,
    awarded_at timestamptz NOT NULL
);
CREATE INDEX badges_user_idx ON badges (user_id, awarded_at DESC);

CREATE TABLE user_tags (
    user_id bigint  NOT NULL REFERENCES authors (user_id) ON DELETE CASCADE,
    tag_id  integer NOT NULL REFERENCES tags (id),
    posts   integer NOT NULL CHECK (posts > 0),
    score   integer NOT NULL,
    PRIMARY KEY (user_id, tag_id)
);
CREATE INDEX user_tags_tag_idx ON user_tags (tag_id, score DESC);
