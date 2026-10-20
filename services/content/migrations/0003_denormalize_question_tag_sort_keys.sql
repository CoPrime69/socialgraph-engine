-- "Questions with tag X, by score / most recent" is the hottest content query. With the sort keys
-- only on questions, it either walked the global score index until it found enough questions of
-- the tag (7 ms for a rare tag) or sorted all of a popular tag's questions. Copying the two sort
-- keys onto question_tags lets one index range read answer it for any tag. The content is a
-- read-only import, so the copies cannot drift.

ALTER TABLE question_tags ADD COLUMN score integer, ADD COLUMN created_at timestamptz;
UPDATE question_tags qt SET score = q.score, created_at = q.created_at FROM questions q WHERE q.id = qt.question_id;
ALTER TABLE question_tags ALTER COLUMN score SET NOT NULL, ALTER COLUMN created_at SET NOT NULL;

CREATE INDEX question_tags_tag_score_idx ON question_tags (tag_id, score DESC, question_id);
CREATE INDEX question_tags_tag_recent_idx ON question_tags (tag_id, created_at DESC, question_id DESC);
