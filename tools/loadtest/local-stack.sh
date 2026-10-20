#!/usr/bin/env bash
# Boots the full stack on this machine for load testing: the four services as native processes
# (from a Release build), the nginx gateway in Docker on the host network, PostgreSQL and Redis
# already running locally. Databases are recreated and seeded from data/stackoverflow on every
# start, so each run begins from the same state.
#
#   BUILD_DIR=build/release tools/loadtest/local-stack.sh start   # prints the env for workload.mjs
#   tools/loadtest/local-stack.sh stop
#
# Needs: psql, redis-cli, docker; PG_ADMIN_URL pointing at a role that may create databases.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-$root/build/release}
RUN_DIR=${RUN_DIR:-$root/build/stack}
PG_ADMIN_URL=${PG_ADMIN_URL:-postgresql://socialgraph:devpassword@localhost:5432/postgres}
REDIS_HOST=${REDIS_HOST:-127.0.0.1}
HTTP_THREADS=${HTTP_THREADS:-0}          # event-loop threads; 0 = one per core
HTTP_WORKER_THREADS=${HTTP_WORKER_THREADS:-32}  # threads running handlers that block on PostgreSQL
DB_POOL_SIZE=${DB_POOL_SIZE:-16}
GATEWAY_IMAGE=${GATEWAY_IMAGE:-nginx:1.27-alpine}
mkdir -p "$RUN_DIR"

db_url() { echo "${PG_ADMIN_URL%/*}/$1"; }

stop() {
  docker rm -f sg-bench-gateway >/dev/null 2>&1 || true
  for svc in identity graph analytics content; do
    if [[ -f $RUN_DIR/$svc.pid ]]; then
      kill "$(cat "$RUN_DIR/$svc.pid")" 2>/dev/null || true
      rm -f "$RUN_DIR/$svc.pid"
    fi
  done
  sleep 1
}

wait_ready() {
  local name=$1 port=$2
  for _ in $(seq 1 240); do
    curl -fsS "http://127.0.0.1:$port/readyz" >/dev/null 2>&1 && return 0
    sleep 0.5
  done
  echo "$name did not become ready; see $RUN_DIR/$name.log" >&2
  exit 1
}

start() {
  stop
  for db in identity graph content; do
    psql "$PG_ADMIN_URL" -qc "DROP DATABASE IF EXISTS $db WITH (FORCE)" -c "CREATE DATABASE $db" >/dev/null
  done
  redis-cli -h "$REDIS_HOST" DEL identity.events graph.events >/dev/null

  # Fresh Ed25519 key pair and demo password for this run (never written to the repository).
  local key seed public demo
  key=$(mktemp)
  openssl genpkey -algorithm ed25519 -out "$key" 2>/dev/null
  seed=$(openssl pkey -in "$key" -outform DER | tail -c 32 | base64)
  public=$(openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | base64)
  rm -f "$key"
  demo=$(openssl rand -hex 12)

  export LOG_LEVEL=${LOG_LEVEL:-warn} REDIS_HOST REDIS_PORT=6379 HTTP_THREADS HTTP_WORKER_THREADS DB_POOL_SIZE
  export SEED_ON_EMPTY=true SEED_DIR="$root/data/stackoverflow" AUTH_PUBLIC_KEY="$public"

  GRAPH_PORT=8082 GRAPH_DATABASE_URL=$(db_url graph) \
    nohup "$BUILD_DIR/services/graph/graph-service" serve >"$RUN_DIR/graph.log" 2>&1 &
  echo $! >"$RUN_DIR/graph.pid"
  CONTENT_PORT=8084 CONTENT_DATABASE_URL=$(db_url content) \
    nohup "$BUILD_DIR/services/content/content-service" serve >"$RUN_DIR/content.log" 2>&1 &
  echo $! >"$RUN_DIR/content.pid"
  wait_ready graph 8082

  # The load test registers its members from one address; lift the per-IP sign-up limit for it.
  IDENTITY_PORT=8081 IDENTITY_DATABASE_URL=$(db_url identity) AUTH_SIGNING_SEED="$seed" \
    SEED_DEMO_PASSWORD="$demo" GRAPH_SERVICE_URL=http://127.0.0.1:8082 \
    REGISTER_RATE_LIMIT_PER_IP=${REGISTER_RATE_LIMIT_PER_IP:-100000} \
    LOGIN_RATE_LIMIT_PER_IP=${LOGIN_RATE_LIMIT_PER_IP:-100000} PASSWORD_HASH_PROFILE=${PASSWORD_HASH_PROFILE:-owasp} \
    nohup "$BUILD_DIR/services/identity/identity-service" serve >"$RUN_DIR/identity.log" 2>&1 &
  echo $! >"$RUN_DIR/identity.pid"
  ANALYTICS_PORT=8083 GRAPH_SERVICE_URL=http://127.0.0.1:8082 \
    nohup "$BUILD_DIR/services/analytics/analytics-service" serve >"$RUN_DIR/analytics.log" 2>&1 &
  echo $! >"$RUN_DIR/analytics.pid"
  wait_ready identity 8081
  wait_ready analytics 8083
  wait_ready content 8084

  docker run -d --name sg-bench-gateway --network host \
    --add-host identity:127.0.0.1 --add-host graph:127.0.0.1 --add-host analytics:127.0.0.1 --add-host content:127.0.0.1 \
    -v "$root/gateway/nginx.conf:/etc/nginx/nginx.conf:ro" \
    -v "$root/gateway/security-headers.conf:/etc/nginx/security-headers.conf:ro" \
    "$GATEWAY_IMAGE" >/dev/null
  for _ in $(seq 1 40); do curl -fsS http://127.0.0.1:8080/gateway/health >/dev/null 2>&1 && break; sleep 0.25; done
  echo "BASE_URL=http://127.0.0.1:8080 SETUP_URL=http://127.0.0.1:8081 DEMO_PASSWORD=$demo"
}

case ${1:-start} in
  start) start ;;
  stop) stop ;;
  *) echo "usage: $0 start|stop" >&2; exit 2 ;;
esac
