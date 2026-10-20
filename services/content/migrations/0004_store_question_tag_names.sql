-- Every question listing showed the question's tags through a correlated subquery (question_tags
-- joined with tags, ordered by position) per row. The import is read-only, so the space-separated
-- tag names are stored on the question once.

ALTER TABLE questions ADD COLUMN tag_names text NOT NULL DEFAULT '';
UPDATE questions q SET tag_names = t.names
FROM (SELECT qt.question_id, string_agg(tg.name, ' ' ORDER BY qt.position) AS names
      FROM question_tags qt JOIN tags tg ON tg.id = qt.tag_id GROUP BY qt.question_id) t
WHERE t.question_id = q.id;
