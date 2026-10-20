# ADR-0008: nginx as API gateway and static host

## Context
The browser needs one origin for the SPA and three APIs; `/internal/*` endpoints must not be public;
login/registration need brute-force protection.

## Decision
nginx serves the built SPA and routes `/api/v1/*` by prefix (contracts §2), denies `/internal/`,
rate-limits `/api/v1/auth/{login,register}` per client IP, propagates/creates `X-Request-Id`, sets
security headers (CSP, nosniff, frame denial) and logs JSON. Authentication stays in the services
(ADR-0005), so the gateway holds no secrets.

## Alternatives
* **Custom C++ gateway**: more code to secure and maintain for features nginx already has.
* **Envoy/Traefik**: richer (retries, circuit breaking, mTLS) but heavier to configure; worth it with
  a service mesh, not for three services.

## Consequences
* Same-origin deployment: no CORS configuration in services, and the SPA uses relative URLs.
* The Vite dev server reproduces the same routing table for local development.
