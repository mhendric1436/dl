# Agent Guidelines

## Project Shape

This repository is a small C++20 distributed locks and leases library built on the sibling `mt`
library.

- `include/dl/lease_manager.hpp` contains the public lease API.
- `include/dl/lease_service.hpp` contains JSON/status mapping for the HTTP API.
- `include/dl/lease_http_server.hpp` contains the cpp-httplib server wrapper.
- `src/lease_manager.cpp` contains the lease implementation.
- `src/lease_service.cpp` contains service request validation and response serialization.
- `src/lease_http_server.cpp` contains HTTP route registration and JSON parsing.
- `src/tables/schemas/` contains private `mt_codegen.py` schema metadata.
- `src/tables/generated/` contains generated row and mapping headers. Do not edit these by hand;
  update the matching schema and run codegen.
- `tests/` contains Catch2 unit tests for lease behavior.
- `third_party/catch2/` contains the vendored Catch2 amalgamated files.
- `third_party/httplib/` contains the vendored cpp-httplib header for HTTP transport.
- `api/openapi.yaml` contains the OpenAPI v3 HTTP API contract.
- `images/` contains README imagery.

The non-vendored dependency is `mt`, expected by the Makefile at `$(HOME)/repos/mt`.

## Build And Test

Use the existing Makefile targets:

```sh
make test
```

Useful targeted commands:

```sh
make build
make codegen
make codegen-check
make format
make format-check
make clean
```

Run `make codegen-check` after changing files in `src/tables/schemas/` or
`src/tables/generated/`.

## Working Rules

- Do not make code changes when the request is analysis-only.
- Prefer small, focused changes that preserve the dependency-minimal design.
- Preserve C++20 compatibility.
- Do not introduce external runtime dependencies unless explicitly requested.
- Use `rg` or `rg --files` for search.
- Run `make test` after code changes when feasible.
- Run `make codegen-check` after schema or generated-header changes.
- Run `make format-check` or `make format` when touching C++ files.
- Keep vendored third-party code limited to Catch2 and cpp-httplib unless the user explicitly asks
  otherwise.
- Include a suggested git commit message after every code or documentation change.
- After every code generation change, report which generated files changed and whether
  `make codegen-check` passed.

## Style

- Follow `.clang-format`; the Makefile runs `clang-format` over C++ files during builds.
- Public API belongs under `include/dl/`; implementation belongs under `src/`.
- Use the `dl` namespace for public lease types and `dl::tables` for generated rows.
- Prefer value-oriented structs and straightforward helper functions over new abstractions unless
  the change clearly reduces complexity.
- Keep backend-specific details behind `mt::Database`; `dl::LeaseManager` should remain
  backend-neutral.
- Keep HTTP-specific details behind `LeaseService` and `LeaseHttpServer`; do not make
  `LeaseManager` depend on httplib or JSON transport concerns.

## Lease Semantics

- A lease is scoped by `resource_key`.
- A successful acquire returns a `holder_id`, `fencing_token`, and `expires_at_ms`.
- Acquire succeeds when no lease exists or the stored lease is expired at caller-supplied `now_ms`.
- Acquire rejects conflicts while an unexpired lease exists.
- Renew succeeds only for the current `holder_id` and `fencing_token`.
- Release succeeds only for the current `holder_id` and `fencing_token`.
- Release expires the stored row instead of deleting it so future acquisitions can continue
  monotonically increasing fencing tokens.
- `expires_at_ms > now_ms` means the lease is still active; equality is treated as expired.
- Time is caller-supplied. Do not add clock synchronization or hidden wall-clock reads in `dl`.
- Every operation that opens its own transaction should have a matching `mt::Transaction&`
  overload so callers can compose `dl` with other `mt` users atomically.

## Generated Tables

Schema files currently include:

- `src/tables/schemas/lease.mt.json`

Generated headers currently include:

- `src/tables/generated/lease_row.hpp`

When adding or changing schemas:

1. Update the schema JSON.
2. Run `make codegen`.
3. Run `make codegen-check`.
4. Commit both the schema and generated header changes.

This repo is pre-alpha and not deployed; do not bump schema versions automatically unless the user
asks for versioning or migration behavior.

## Test Guidance

- Add focused Catch2 tests near the behavior being changed.
- For public API changes, cover both convenience methods and `mt::Transaction&` overloads when
  behavior differs or composition is at risk.
- For acquire behavior, test missing rows, unexpired conflicts, and expired replacement.
- For renew and release behavior, test wrong holders, stale fencing tokens, and expired leases.
- For fencing behavior, verify that tokens increase monotonically across expiry and release.
- For HTTP API behavior, test `LeaseService` request validation, status mapping, and JSON response
  bodies. Keep `LeaseHttpServer` route handlers thin.

## Documentation

- Keep `README.md` examples aligned with the public API.
- Keep contributor and agent guidance aligned with the actual Makefile targets.
- Keep `api/openapi.yaml` aligned with `LeaseService` routes, field names, and status codes.
