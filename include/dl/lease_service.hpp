#pragma once

#include "dl/lease_manager.hpp"
#include "mt/json.hpp"

#include <string>

namespace dl
{

enum class LeaseServiceStatus
{
    Ok,
    NoContent,
    BadRequest,
    NotFound,
    Conflict,
    InternalError
};

struct LeaseServiceResult
{
    LeaseServiceStatus status = LeaseServiceStatus::Ok;
    mt::Json body = mt::Json::null();
};

class LeaseService
{
  public:
    explicit LeaseService(LeaseManager& leases);

    LeaseServiceResult get_lease(const std::string& resource_key) const;

    LeaseServiceResult acquire_lease(
        const std::string& resource_key,
        const mt::Json& request
    ) const;

    LeaseServiceResult renew_lease(
        const std::string& resource_key,
        const mt::Json& request
    ) const;

    LeaseServiceResult release_lease(
        const std::string& resource_key,
        const mt::Json& request
    ) const;

  private:
    LeaseManager* leases_ = nullptr;
};

int http_status(LeaseServiceStatus status);
bool has_response_body(LeaseServiceStatus status);
mt::Json lease_error_json(
    std::string code,
    std::string message
);

} // namespace dl
