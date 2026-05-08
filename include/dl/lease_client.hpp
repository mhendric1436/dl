#pragma once

#include "mt/database.hpp"
#include "mt/transaction.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace dl
{

struct Lease
{
    std::string resource_key;
    std::string holder_id;
    std::int64_t fencing_token = 0;
    std::int64_t expires_at_ms = 0;
    std::int64_t updated_at_ms = 0;
};

struct AcquireLeaseRequest
{
    std::string resource_key;
    std::string holder_id;
    std::int64_t ttl_ms = 0;
    std::int64_t now_ms = 0;
};

struct RenewLeaseRequest
{
    std::string resource_key;
    std::string holder_id;
    std::int64_t fencing_token = 0;
    std::int64_t ttl_ms = 0;
    std::int64_t now_ms = 0;
};

struct ReleaseLeaseRequest
{
    std::string resource_key;
    std::string holder_id;
    std::int64_t fencing_token = 0;
    std::int64_t now_ms = 0;
};

struct GetLeaseRequest
{
    std::string resource_key;
};

class LeaseClient
{
  public:
    explicit LeaseClient(mt::Database& database);

    std::optional<Lease> try_acquire(AcquireLeaseRequest request) const;
    std::optional<Lease> try_acquire(
        mt::Transaction& tx,
        AcquireLeaseRequest request
    ) const;

    std::optional<Lease> renew(RenewLeaseRequest request) const;
    std::optional<Lease> renew(
        mt::Transaction& tx,
        RenewLeaseRequest request
    ) const;

    bool release(ReleaseLeaseRequest request) const;
    bool release(
        mt::Transaction& tx,
        ReleaseLeaseRequest request
    ) const;

    std::optional<Lease> get(const GetLeaseRequest& request) const;
    std::optional<Lease>
    get(mt::Transaction& tx,
        const GetLeaseRequest& request) const;

  private:
    mt::Database* database_ = nullptr;
};

} // namespace dl
