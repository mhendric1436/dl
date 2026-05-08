#include "dl/lease_client.hpp"

#include "mt/table.hpp"
#include "mt/transaction.hpp"
#include "tables/generated/lease_row.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace dl
{
namespace
{

using LeaseTable = mt::Table<tables::LeaseRow, tables::LeaseRowMapping>;

LeaseTable lease_table(mt::Database& database)
{
    mt::TableProvider tables{database};
    return tables.table<tables::LeaseRow, tables::LeaseRowMapping>();
}

std::string lease_key(const std::string& resource_key)
{
    return tables::LeaseRowMapping::key(
        tables::LeaseRow{
            .resourceKey = resource_key,
            .holderId = std::string{},
            .fencingToken = 0,
            .expiresAtMs = 0,
            .updatedAtMs = 0
        }
    );
}

Lease to_public_lease(const tables::LeaseRow& row)
{
    return Lease{
        .resource_key = row.resourceKey,
        .holder_id = row.holderId,
        .fencing_token = row.fencingToken,
        .expires_at_ms = row.expiresAtMs,
        .updated_at_ms = row.updatedAtMs
    };
}

bool is_unexpired(
    const tables::LeaseRow& row,
    std::int64_t now_ms
)
{
    return row.expiresAtMs > now_ms;
}

bool is_current_holder(
    const tables::LeaseRow& row,
    const std::string& holder_id,
    std::int64_t fencing_token,
    std::int64_t now_ms
)
{
    return row.holderId == holder_id && row.fencingToken == fencing_token &&
           is_unexpired(row, now_ms);
}

} // namespace

LeaseClient::LeaseClient(mt::Database& database)
    : database_(&database)
{
    (void)lease_table(*database_);
}

std::optional<Lease> LeaseClient::try_acquire(AcquireLeaseRequest request) const
{
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::optional<Lease>
        { return try_acquire(tx, std::move(request)); }
    );
}

std::optional<Lease> LeaseClient::try_acquire(
    mt::Transaction& tx,
    AcquireLeaseRequest request
) const
{
    auto leases = lease_table(*database_);
    auto key = lease_key(request.resource_key);
    auto existing = leases.get(tx, key);

    if (existing && is_unexpired(*existing, request.now_ms))
    {
        return std::nullopt;
    }

    auto fencing_token = existing ? existing->fencingToken + 1 : std::int64_t{1};
    tables::LeaseRow row{
        .resourceKey = std::move(request.resource_key),
        .holderId = std::move(request.holder_id),
        .fencingToken = fencing_token,
        .expiresAtMs = request.now_ms + std::max<std::int64_t>(request.ttl_ms, 0),
        .updatedAtMs = request.now_ms
    };
    leases.put(tx, row);

    return to_public_lease(row);
}

std::optional<Lease> LeaseClient::renew(RenewLeaseRequest request) const
{
    mt::TransactionProvider txs{*database_};

    return txs.run(
        [&](mt::Transaction& tx) -> std::optional<Lease> { return renew(tx, std::move(request)); }
    );
}

std::optional<Lease> LeaseClient::renew(
    mt::Transaction& tx,
    RenewLeaseRequest request
) const
{
    auto leases = lease_table(*database_);
    auto key = lease_key(request.resource_key);
    auto row = leases.get(tx, key);
    if (!row || !is_current_holder(*row, request.holder_id, request.fencing_token, request.now_ms))
    {
        return std::nullopt;
    }

    row->expiresAtMs = request.now_ms + std::max<std::int64_t>(request.ttl_ms, 0);
    row->updatedAtMs = request.now_ms;
    leases.put(tx, *row);

    return to_public_lease(*row);
}

bool LeaseClient::release(ReleaseLeaseRequest request) const
{
    mt::TransactionProvider txs{*database_};

    return txs.run([&](mt::Transaction& tx) -> bool { return release(tx, std::move(request)); });
}

bool LeaseClient::release(
    mt::Transaction& tx,
    ReleaseLeaseRequest request
) const
{
    auto leases = lease_table(*database_);
    auto key = lease_key(request.resource_key);
    auto row = leases.get(tx, key);
    if (!row || !is_current_holder(*row, request.holder_id, request.fencing_token, request.now_ms))
    {
        return false;
    }

    row->expiresAtMs = request.now_ms;
    row->updatedAtMs = request.now_ms;
    leases.put(tx, *row);
    return true;
}

std::optional<Lease> LeaseClient::get(const GetLeaseRequest& request) const
{
    auto leases = lease_table(*database_);
    auto row = leases.get(lease_key(request.resource_key));
    if (!row)
    {
        return std::nullopt;
    }
    return to_public_lease(*row);
}

std::optional<Lease> LeaseClient::get(
    mt::Transaction& tx,
    const GetLeaseRequest& request
) const
{
    auto leases = lease_table(*database_);
    auto row = leases.get(tx, lease_key(request.resource_key));
    if (!row)
    {
        return std::nullopt;
    }
    return to_public_lease(*row);
}

} // namespace dl
