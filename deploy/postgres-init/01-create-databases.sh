#!/bin/sh
# Database-per-service: each service gets its own database and a role that owns only that database.
set -eu
psql -v ON_ERROR_STOP=1 --username postgres \
  -v identity_pw="$IDENTITY_DB_PASSWORD" -v graph_pw="$GRAPH_DB_PASSWORD" -v content_pw="$CONTENT_DB_PASSWORD" <<'SQL'
CREATE ROLE identity_svc LOGIN PASSWORD :'identity_pw';
CREATE ROLE graph_svc LOGIN PASSWORD :'graph_pw';
CREATE ROLE content_svc LOGIN PASSWORD :'content_pw';
CREATE DATABASE identity OWNER identity_svc;
CREATE DATABASE graph OWNER graph_svc;
CREATE DATABASE content OWNER content_svc;
REVOKE ALL ON DATABASE identity FROM PUBLIC;
REVOKE ALL ON DATABASE graph FROM PUBLIC;
REVOKE ALL ON DATABASE content FROM PUBLIC;
SQL
