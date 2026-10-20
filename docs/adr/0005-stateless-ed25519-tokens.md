# ADR-0005: Stateless Ed25519-signed access tokens

## Context
Graph and analytics endpoints all need the caller's identity. A per-request call to identity-service
would put it on every request path and make it a single point of failure.

## Decision
identity-service issues JWTs signed with **Ed25519** (`alg: EdDSA`, libsodium `crypto_sign`). Only
identity holds the 32-byte seed; every other service holds the public key and verifies tokens locally
(signature, `alg`, issuer, expiry with 30 s skew). Passwords are hashed with **Argon2id**
(OWASP parameters: t=2, m=19 MiB) and verified with constant-time comparison; failed logins for unknown
handles do equivalent work to avoid user enumeration. Login/registration are rate limited both at the
gateway (per IP) and in identity (per IP and per handle).

## Alternatives
* **Opaque session tokens in a shared store**: revocable instantly, but every service would need the
  store, coupling them to identity's data.
* **HS256 (shared secret)**: any service holding the secret could mint tokens; asymmetric keys keep
  minting power in one place.

## Consequences
* (+) Verification is a local ~50 µs signature check; identity can be down without breaking reads.
* (−) Tokens cannot be revoked before expiry; logout is client-side. TTL defaults to 8 hours.
  A denylist propagated by a `session.revoked` event is the natural extension (future work).
* Keys are supplied by environment (`scripts/generate-env.sh` creates them); nothing secret is committed.
