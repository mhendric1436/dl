![dl logo and project overview](images/dl.png)
# dl

`dl` is a C++20 distributed locks and leases library built on the sibling `mt` library.

It is intended to provide the foundational coordination primitive for higher-level
systems such as queues, schedulers, workflow engines, control planes, and service
registries. The core idea is simple: represent exclusive ownership as a durable lease
row, then use `mt` transactions, absent-key reads, point-read validation, and optimistic
concurrency control to make acquire, renew, release, and expiry conflict-detected.

## Short Description

C++20 `mt`-backed distributed locks and leases with compare-and-swap-style acquire,
renew, release, and expiry semantics.

## Why This Exists

Locks and leases are the primitive coordination unit for a distributed systems stack.
They show up inside:

- queue message claims
- job ownership and heartbeats
- workflow step leases
- leader election
- singleton control-plane reconcilers
- service registration and health tracking
- exclusive access to named resources

`dl` should make that pattern reusable instead of reimplementing narrow lease logic in
each higher-level project.

## Intended Model

A lock is a named resource with a current holder and an expiry time.

```text
resource_key -> holder_id, fencing_token, expires_at
```

The first implementation should support:

- acquire when no unexpired lease exists
- acquire after expiry
- renew by the current holder only
- release by the current holder only
- inspect the current lease
- reject conflicting acquire or renew attempts
- monotonically increasing fencing tokens

The safety property comes from `mt`:

- acquire uses an absent-key or point read before writing ownership
- renew uses a point read of the current lease version
- release uses a point read of the current lease version
- concurrent conflicting decisions fail at commit or observe the newer state on retry

## Initial API Sketch

```cpp
dl::LeaseManager leases{database};

auto acquired = leases.try_acquire(
    dl::AcquireLeaseRequest{
        .resource_key = "queue:orders:consumer",
        .holder_id = "worker-1",
        .ttl_ms = 30000,
        .now_ms = now_ms
    }
);

if (acquired)
{
    leases.renew(
        dl::RenewLeaseRequest{
            .resource_key = acquired->resource_key,
            .holder_id = acquired->holder_id,
            .fencing_token = acquired->fencing_token,
            .ttl_ms = 30000,
            .now_ms = later_ms
        }
    );
}
```

The exact API can evolve, but v1 should keep the library backend-neutral by accepting an
`mt::Database&` and using private `mt_codegen.py` table mappings internally.

## Trust Domain

`dl` is currently targeted at a single administrative trust domain. The intended callers are
cooperating services that are already part of the same control plane, deployment, or internal
platform boundary.

That means:

- resource keys are trusted coordination names, not security boundaries
- holder IDs are caller-provided identifiers, not authenticated principals
- `now_ms` is caller supplied so cooperating components can use a shared time source
- the HTTP layer is a transport convenience for internal services, not a complete public
  multi-tenant lease service

For a true multi-tenant deployment, a service built on `dl` should add tenant identity,
authorization, resource-key namespacing, TTL policy limits, request auditing, and server-side time
ownership before exposing lease operations to mutually untrusted callers. In that model,
`LeaseManager` should remain the backend-neutral primitive while the outer service layer enforces
tenant isolation and policy.

## HTTP API

`dl` also includes an optional cpp-httplib transport layer for HTTP use cases:

- `dl::LeaseService` maps JSON requests and lease results to HTTP-oriented status codes.
- `dl::LeaseHttpServer` registers the HTTP routes and delegates behavior to `LeaseService`.
- `api/openapi.yaml` defines the OpenAPI v3 contract.

The in-process `LeaseManager` remains backend-neutral and does not depend on HTTP transport
details. The current HTTP routes assume the same single-trust-domain model described above.

## Relationship To `qu`

`qu` can use `dl` as the reusable lease primitive for message claims:

- a pending message is claimed by acquiring a lease for `message:<id>`
- the visibility timeout is the lease TTL
- a worker can renew the claim by renewing the lease
- expired leases make messages claimable again
- fencing tokens can prevent stale workers from acknowledging old claims

The first `qu` implementation may keep local claim fields, but `dl` should define the
shared semantics that queue, scheduler, and workflow repos converge on.

## Build And Repository Shape

`dl` now has the initial C++20 scaffold in place:

- `include/dl/lease_manager.hpp` and `src/lease_manager.cpp` provide the backend-neutral
  in-process lease API.
- `include/dl/lease_service.hpp` and `src/lease_service.cpp` provide JSON request validation and
  HTTP-oriented response/status mapping.
- `include/dl/lease_http_server.hpp` and `src/lease_http_server.cpp` provide the optional
  cpp-httplib transport.
- `src/tables/schemas/lease.mt.json` defines the private lease table schema.
- `src/tables/generated/lease_row.hpp` is generated by `mt_codegen.py` and should not be edited by
  hand.
- `tests/` contains memory-backed Catch2 tests for lease behavior and service mapping.
- `api/openapi.yaml` documents the HTTP contract.
- `docs/*.puml` contains PlantUML architecture and sequence diagrams; generated PNGs are produced
  by `make docs-png`.

Useful Makefile targets:

```sh
make test
make build
make codegen
make codegen-check
make format
make format-check
make docs-png
make clean-docs
make clean
```

The Makefile expects the sibling `mt` checkout at `$(HOME)/repos/mt`.

## Non-Goals For V1

- no external lock service
- no clock synchronization protocol
- no blocking wait API
- no fairness guarantee
- no durable backend choice beyond what `mt` provides

## Roadmap

- add examples showing `qu` message claim integration
- add HTTP route-level tests around `LeaseHttpServer`
- add race-focused tests that exercise concurrent transaction conflicts
- add configurable TTL policy limits for hosted HTTP use cases
- add multi-tenant service guidance or a wrapper if `dl` is used outside a single trust domain
