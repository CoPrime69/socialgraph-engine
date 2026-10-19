#!/usr/bin/env bash
# HTTP load test of the running stack with wrk.
#   BASE_URL=http://localhost:8080 HANDLE=user1 PASSWORD=... tools/loadtest/run.sh
# Optional: DURATION (default 20s), THREADS (4), CONNECTIONS (32).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BASE_URL=${BASE_URL:-http://localhost:8080}
HANDLE=${HANDLE:-user1}
DURATION=${DURATION:-20s}
THREADS=${THREADS:-4}
CONNECTIONS=${CONNECTIONS:-32}
: "${PASSWORD:?set PASSWORD to the seeded demo password}"

TOKEN=$(curl -fsS -H 'Content-Type: application/json' \
  -d "{\"handle\":\"$HANDLE\",\"password\":\"$PASSWORD\"}" "$BASE_URL/api/v1/auth/login" |
  python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')
export TOKEN

endpoints=(
  "/api/v1/analytics/summary"
  "/api/v1/analytics/influencers?metric=pagerank&limit=10"
  "/api/v1/me/recommendations?limit=10"
  "/api/v1/paths?to=57"
  "/api/v1/analytics/graph"
  "/api/v1/me/connections"
  "/api/v1/users?q=user&limit=20"
)
echo "# wrk -t$THREADS -c$CONNECTIONS -d$DURATION against $BASE_URL"
for path in "${endpoints[@]}"; do
  result=$(wrk -t"$THREADS" -c"$CONNECTIONS" -d"$DURATION" -s "$here/auth.lua" "$BASE_URL$path" | grep '^RESULT')
  echo "$path $result"
done
