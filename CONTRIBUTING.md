# Contributing

`dl` is pre-alpha. The API, schemas, and storage layout may change quickly while the core lease
model settles.

## Prerequisites

- C++20 compiler, currently `clang++` by default
- `make`
- `python3`
- `clang-format`
- sibling `mt` checkout at `$(HOME)/repos/mt`

The repository vendors Catch2 under `third_party/catch2/` and cpp-httplib under
`third_party/httplib/`.

## Build And Test

Run the full test target before submitting code changes:

```sh
make test
```

Useful targets:

```sh
make build
make codegen
make codegen-check
make format
make format-check
make clean
```

`make test` formats C++ sources as part of the build. Use `make format-check` when you want to
check formatting without changing files.

## Generated Tables

Schemas live in `src/tables/schemas/`. Generated headers live in `src/tables/generated/`.

Do not edit generated headers directly. To change table shape:

1. Edit the matching `*.mt.json` schema.
2. Run `make codegen`.
3. Run `make codegen-check`.
4. Commit both the schema and generated header changes.

Because `dl` is not deployed, do not bump schema versions automatically. Bump versions only when a
change intentionally needs versioned schema behavior.

## Lease Semantics

Please preserve these behaviors unless the contribution explicitly changes them:

- Lease ownership is scoped by `resource_key`.
- Acquire succeeds when no active lease exists.
- Acquire after expiry is allowed and increments the fencing token.
- Renew and release require both the current holder id and fencing token.
- Release expires the row instead of deleting it, preserving fencing-token monotonicity.
- The caller supplies `now_ms`; `dl` does not own clock synchronization.
- Public convenience APIs should have matching `mt::Transaction&` overloads where callers may need
  atomic composition with other `mt` users.
- HTTP support should stay layered over `LeaseService` and `LeaseHttpServer`; keep transport and
  JSON concerns out of `LeaseManager`.

## Tests

Add focused Catch2 tests under `tests/` for behavior changes.

Good tests should cover observable lease behavior, especially:

- acquire on missing rows
- acquire conflict while a lease is unexpired
- acquire after expiry
- renew success and rejection cases
- release success and rejection cases
- fencing-token monotonicity
- caller-owned transaction overloads
- HTTP service request validation and status mapping

## Documentation

Keep `README.md` examples aligned with the public API.

Keep `api/openapi.yaml` aligned with the implemented HTTP routes, field names, and status codes.

Update `AGENTS.md` when build targets, generated files, repository layout, or project-specific
agent rules change.

## Pull Request Expectations

Before opening a PR or handing off a change, run:

```sh
make codegen-check
make test
```

Run `make format-check` when you want a non-mutating formatting check.

Keep changes narrowly scoped. Avoid unrelated refactors, dependency additions, or metadata churn.
