#include "dl/lease_service.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace dl
{
namespace
{

class InvalidLeaseRequest : public std::runtime_error
{
  public:
    explicit InvalidLeaseRequest(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

const mt::Json& required_field(
    const mt::Json& object,
    const std::string& field
)
{
    if (!object.is_object())
    {
        throw InvalidLeaseRequest("request body must be a JSON object");
    }

    const auto& values = object.as_object();
    auto it = values.find(field);
    if (it == values.end())
    {
        throw InvalidLeaseRequest("missing required field: " + field);
    }
    return it->second;
}

std::string required_string(
    const mt::Json& object,
    const std::string& field
)
{
    const auto& value = required_field(object, field);
    if (!value.is_string() || value.as_string().empty())
    {
        throw InvalidLeaseRequest("field must be a non-empty string: " + field);
    }
    return value.as_string();
}

std::int64_t required_int64(
    const mt::Json& object,
    const std::string& field
)
{
    const auto& value = required_field(object, field);
    if (!value.is_int64())
    {
        throw InvalidLeaseRequest("field must be an int64: " + field);
    }
    return value.as_int64();
}

std::int64_t required_non_negative_int64(
    const mt::Json& object,
    const std::string& field
)
{
    auto value = required_int64(object, field);
    if (value < 0)
    {
        throw InvalidLeaseRequest("field must be non-negative: " + field);
    }
    return value;
}

std::int64_t required_positive_int64(
    const mt::Json& object,
    const std::string& field
)
{
    auto value = required_int64(object, field);
    if (value < 1)
    {
        throw InvalidLeaseRequest("field must be positive: " + field);
    }
    return value;
}

mt::Json lease_json(const Lease& lease)
{
    return mt::Json::object(
        {{"resource_key", lease.resource_key},
         {"holder_id", lease.holder_id},
         {"fencing_token", lease.fencing_token},
         {"expires_at_ms", lease.expires_at_ms},
         {"updated_at_ms", lease.updated_at_ms}}
    );
}

LeaseServiceResult bad_request(const std::exception& error)
{
    return LeaseServiceResult{
        .status = LeaseServiceStatus::BadRequest,
        .body = lease_error_json("invalid_request", error.what())
    };
}

LeaseServiceResult conflict(
    std::string code,
    std::string message
)
{
    return LeaseServiceResult{
        .status = LeaseServiceStatus::Conflict,
        .body = lease_error_json(std::move(code), std::move(message))
    };
}

} // namespace

LeaseService::LeaseService(LeaseManager& leases)
    : leases_(&leases)
{
}

LeaseServiceResult LeaseService::get_lease(const std::string& resource_key) const
{
    auto lease = leases_->get(GetLeaseRequest{.resource_key = resource_key});
    if (!lease)
    {
        return LeaseServiceResult{
            .status = LeaseServiceStatus::NotFound,
            .body = lease_error_json("not_found", "lease was not found")
        };
    }

    return LeaseServiceResult{.status = LeaseServiceStatus::Ok, .body = lease_json(*lease)};
}

LeaseServiceResult LeaseService::acquire_lease(
    const std::string& resource_key,
    const mt::Json& request
) const
{
    try
    {
        auto holder_id = required_string(request, "holder_id");
        auto ttl_ms = required_non_negative_int64(request, "ttl_ms");
        auto now_ms = required_int64(request, "now_ms");

        auto lease = leases_->try_acquire(
            AcquireLeaseRequest{
                .resource_key = resource_key,
                .holder_id = std::move(holder_id),
                .ttl_ms = ttl_ms,
                .now_ms = now_ms
            }
        );
        if (!lease)
        {
            return conflict("lease_conflict", "resource already has an unexpired lease");
        }

        return LeaseServiceResult{.status = LeaseServiceStatus::Ok, .body = lease_json(*lease)};
    }
    catch (const InvalidLeaseRequest& error)
    {
        return bad_request(error);
    }
}

LeaseServiceResult LeaseService::renew_lease(
    const std::string& resource_key,
    const mt::Json& request
) const
{
    try
    {
        auto holder_id = required_string(request, "holder_id");
        auto fencing_token = required_positive_int64(request, "fencing_token");
        auto ttl_ms = required_non_negative_int64(request, "ttl_ms");
        auto now_ms = required_int64(request, "now_ms");

        auto lease = leases_->renew(
            RenewLeaseRequest{
                .resource_key = resource_key,
                .holder_id = std::move(holder_id),
                .fencing_token = fencing_token,
                .ttl_ms = ttl_ms,
                .now_ms = now_ms
            }
        );
        if (!lease)
        {
            return conflict(
                "lease_conflict", "lease is missing, expired, or owned by another holder"
            );
        }

        return LeaseServiceResult{.status = LeaseServiceStatus::Ok, .body = lease_json(*lease)};
    }
    catch (const InvalidLeaseRequest& error)
    {
        return bad_request(error);
    }
}

LeaseServiceResult LeaseService::release_lease(
    const std::string& resource_key,
    const mt::Json& request
) const
{
    try
    {
        auto holder_id = required_string(request, "holder_id");
        auto fencing_token = required_positive_int64(request, "fencing_token");
        auto now_ms = required_int64(request, "now_ms");

        auto released = leases_->release(
            ReleaseLeaseRequest{
                .resource_key = resource_key,
                .holder_id = std::move(holder_id),
                .fencing_token = fencing_token,
                .now_ms = now_ms
            }
        );
        if (!released)
        {
            return conflict(
                "lease_conflict", "lease is missing, expired, or owned by another holder"
            );
        }

        return LeaseServiceResult{.status = LeaseServiceStatus::NoContent};
    }
    catch (const InvalidLeaseRequest& error)
    {
        return bad_request(error);
    }
}

int http_status(LeaseServiceStatus status)
{
    switch (status)
    {
    case LeaseServiceStatus::Ok:
        return 200;
    case LeaseServiceStatus::NoContent:
        return 204;
    case LeaseServiceStatus::BadRequest:
        return 400;
    case LeaseServiceStatus::NotFound:
        return 404;
    case LeaseServiceStatus::Conflict:
        return 409;
    case LeaseServiceStatus::InternalError:
        return 500;
    }

    return 500;
}

bool has_response_body(LeaseServiceStatus status)
{
    return status != LeaseServiceStatus::NoContent;
}

mt::Json lease_error_json(
    std::string code,
    std::string message
)
{
    return mt::Json::object({{"code", std::move(code)}, {"message", std::move(message)}});
}

} // namespace dl
