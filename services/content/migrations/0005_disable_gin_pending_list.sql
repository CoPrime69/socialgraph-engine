-- The import inserts every question at once; with GIN's fastupdate the entries wait in the pending
-- list until the next VACUUM, and full-text searches scan that list linearly meanwhile. The content
-- is read-only after import, so the pending list buys nothing. (Migrations run before the import.)
ALTER INDEX questions_search_idx SET (fastupdate = off);
