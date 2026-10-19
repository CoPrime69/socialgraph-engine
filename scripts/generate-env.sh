#!/usr/bin/env bash
# Writes deploy/.env with random database passwords, a fresh Ed25519 signing key pair and a demo
# password for the seeded accounts. Requires openssl. Refuses to overwrite an existing file.
set -euo pipefail
cd "$(dirname "$0")/.."
target=deploy/.env
if [[ -e $target && ${1:-} != --force ]]; then
  echo "$target already exists (use --force to overwrite)" >&2
  exit 1
fi
rand() { openssl rand -base64 24 | tr -d '/+=' | cut -c1-24; }
key=$(mktemp)
trap 'rm -f "$key"' EXIT
openssl genpkey -algorithm ed25519 -out "$key" 2>/dev/null
# The last 32 bytes of the DER encodings are the raw seed and raw public key.
seed=$(openssl pkey -in "$key" -outform DER | tail -c 32 | base64)
public=$(openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | base64)
demo=${SEED_DEMO_PASSWORD:-$(rand)}
umask 077
cat > "$target" <<ENV
POSTGRES_PASSWORD=$(rand)
IDENTITY_DB_PASSWORD=$(rand)
GRAPH_DB_PASSWORD=$(rand)
CONTENT_DB_PASSWORD=$(rand)
AUTH_SIGNING_SEED=$seed
AUTH_PUBLIC_KEY=$public
AUTH_TOKEN_TTL_SECONDS=28800
SEED_ON_EMPTY=true
SEED_DEMO_PASSWORD=$demo
GATEWAY_PORT=8080
LOG_LEVEL=info
ENV
echo "Wrote $target. Sign in as handle \"demo\" with password: $demo"
