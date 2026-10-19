-- Per-member activity totals, precomputed. The content is a read-only import, so the totals only
-- change when the dataset is (re)loaded; computing them per request meant aggregating every
-- question, answer, comment and badge of the member (thousands of rows for the most active ones).

CREATE TABLE user_activity (
    user_id          bigint  PRIMARY KEY REFERENCES authors (user_id) ON DELETE CASCADE,
    questions        integer NOT NULL,
    question_score   bigint  NOT NULL,
    answers          integer NOT NULL,
    accepted_answers integer NOT NULL,
    answer_score     bigint  NOT NULL,
    comments         integer NOT NULL,
    gold             integer NOT NULL,
    silver           integer NOT NULL,
    bronze           integer NOT NULL
);

-- Rebuilds user_activity from the content tables; the seed loader calls it after an import.
CREATE FUNCTION rebuild_user_activity() RETURNS void LANGUAGE sql AS $$
    TRUNCATE user_activity;
    INSERT INTO user_activity
    SELECT au.user_id,
           coalesce(q.n, 0), coalesce(q.score, 0),
           coalesce(a.n, 0), coalesce(a.accepted, 0), coalesce(a.score, 0),
           coalesce(c.n, 0),
           coalesce(b.gold, 0), coalesce(b.silver, 0), coalesce(b.bronze, 0)
    FROM authors au
    LEFT JOIN (SELECT owner_id, count(*) AS n, sum(score) AS score FROM questions GROUP BY owner_id) q
           ON q.owner_id = au.user_id
    LEFT JOIN (SELECT owner_id, count(*) AS n, count(*) FILTER (WHERE is_accepted) AS accepted, sum(score) AS score
               FROM answers GROUP BY owner_id) a ON a.owner_id = au.user_id
    LEFT JOIN (SELECT user_id, count(*) AS n FROM comments GROUP BY user_id) c ON c.user_id = au.user_id
    LEFT JOIN (SELECT user_id, count(*) FILTER (WHERE class = 1) AS gold, count(*) FILTER (WHERE class = 2) AS silver,
                      count(*) FILTER (WHERE class = 3) AS bronze
               FROM badges GROUP BY user_id) b ON b.user_id = au.user_id;
$$;

SELECT rebuild_user_activity();

-- A member's top tags: read the first rows of this index instead of sorting all their tags.
CREATE INDEX user_tags_user_rank_idx ON user_tags (user_id, posts DESC, score DESC);
