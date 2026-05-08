#include "catch2/catch_amalgamated.hpp"
#include "dl/lease_manager.hpp"
#include "dl/lease_service.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{

struct ServiceContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    dl::LeaseManager leases{database};
    dl::LeaseService service{leases};
};

mt::Json acquire_request(
    std::string holder_id,
    std::int64_t ttl_ms,
    std::int64_t now_ms
)
{
    return mt::Json::object(
        {{"holder_id", std::move(holder_id)}, {"ttl_ms", ttl_ms}, {"now_ms", now_ms}}
    );
}

mt::Json renew_request(
    std::string holder_id,
    std::int64_t fencing_token,
    std::int64_t ttl_ms,
    std::int64_t now_ms
)
{
    return mt::Json::object(
        {{"holder_id", std::move(holder_id)},
         {"fencing_token", fencing_token},
         {"ttl_ms", ttl_ms},
         {"now_ms", now_ms}}
    );
}

mt::Json release_request(
    std::string holder_id,
    std::int64_t fencing_token,
    std::int64_t now_ms
)
{
    return mt::Json::object(
        {{"holder_id", std::move(holder_id)}, {"fencing_token", fencing_token}, {"now_ms", now_ms}}
    );
}

} // namespace

TEST_CASE("lease service acquires and gets leases")
{
    ServiceContext ctx;

    auto acquired = ctx.service.acquire_lease("resource", acquire_request("worker-1", 100, 1000));

    REQUIRE(acquired.status == dl::LeaseServiceStatus::Ok);
    CHECK(acquired.body["resource_key"].as_string() == "resource");
    CHECK(acquired.body["holder_id"].as_string() == "worker-1");
    CHECK(acquired.body["fencing_token"].as_int64() == 1);
    CHECK(acquired.body["expires_at_ms"].as_int64() == 1100);

    auto found = ctx.service.get_lease("resource");
    REQUIRE(found.status == dl::LeaseServiceStatus::Ok);
    CHECK(found.body["updated_at_ms"].as_int64() == 1000);
}

TEST_CASE("lease service maps acquire conflict to conflict")
{
    ServiceContext ctx;

    REQUIRE(
        ctx.service.acquire_lease("resource", acquire_request("worker-1", 100, 1000)).status ==
        dl::LeaseServiceStatus::Ok
    );

    auto conflict = ctx.service.acquire_lease("resource", acquire_request("worker-2", 100, 1001));

    CHECK(conflict.status == dl::LeaseServiceStatus::Conflict);
    CHECK(conflict.body["code"].as_string() == "lease_conflict");
}

TEST_CASE("lease service renews leases")
{
    ServiceContext ctx;
    auto acquired = ctx.service.acquire_lease("resource", acquire_request("worker-1", 100, 1000));
    REQUIRE(acquired.status == dl::LeaseServiceStatus::Ok);

    auto renewed = ctx.service.renew_lease("resource", renew_request("worker-1", 1, 200, 1050));

    REQUIRE(renewed.status == dl::LeaseServiceStatus::Ok);
    CHECK(renewed.body["holder_id"].as_string() == "worker-1");
    CHECK(renewed.body["fencing_token"].as_int64() == 1);
    CHECK(renewed.body["expires_at_ms"].as_int64() == 1250);
}

TEST_CASE("lease service rejects invalid renew ownership")
{
    ServiceContext ctx;
    REQUIRE(
        ctx.service.acquire_lease("resource", acquire_request("worker-1", 100, 1000)).status ==
        dl::LeaseServiceStatus::Ok
    );

    auto conflict = ctx.service.renew_lease("resource", renew_request("worker-2", 1, 100, 1050));

    CHECK(conflict.status == dl::LeaseServiceStatus::Conflict);
    CHECK(conflict.body["code"].as_string() == "lease_conflict");
}

TEST_CASE("lease service releases leases")
{
    ServiceContext ctx;
    REQUIRE(
        ctx.service.acquire_lease("resource", acquire_request("worker-1", 100, 1000)).status ==
        dl::LeaseServiceStatus::Ok
    );

    auto released = ctx.service.release_lease("resource", release_request("worker-1", 1, 1050));

    CHECK(released.status == dl::LeaseServiceStatus::NoContent);

    auto replacement =
        ctx.service.acquire_lease("resource", acquire_request("worker-2", 100, 1050));
    REQUIRE(replacement.status == dl::LeaseServiceStatus::Ok);
    CHECK(replacement.body["fencing_token"].as_int64() == 2);
}

TEST_CASE("lease service maps missing lease to not found")
{
    ServiceContext ctx;

    auto missing = ctx.service.get_lease("missing");

    CHECK(missing.status == dl::LeaseServiceStatus::NotFound);
    CHECK(missing.body["code"].as_string() == "not_found");
}

TEST_CASE("lease service rejects malformed requests")
{
    ServiceContext ctx;

    auto result = ctx.service.acquire_lease("resource", mt::Json::object({}));

    CHECK(result.status == dl::LeaseServiceStatus::BadRequest);
    CHECK(result.body["code"].as_string() == "invalid_request");
}

TEST_CASE("lease service maps statuses to HTTP statuses")
{
    CHECK(dl::http_status(dl::LeaseServiceStatus::Ok) == 200);
    CHECK(dl::http_status(dl::LeaseServiceStatus::NoContent) == 204);
    CHECK(dl::http_status(dl::LeaseServiceStatus::BadRequest) == 400);
    CHECK(dl::http_status(dl::LeaseServiceStatus::NotFound) == 404);
    CHECK(dl::http_status(dl::LeaseServiceStatus::Conflict) == 409);
    CHECK(dl::http_status(dl::LeaseServiceStatus::InternalError) == 500);
    CHECK(dl::has_response_body(dl::LeaseServiceStatus::NoContent) == false);
}
