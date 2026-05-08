#include "catch2/catch_amalgamated.hpp"
#include "dl/lease_manager.hpp"
#include "mt/backends/memory.hpp"
#include "mt/database.hpp"
#include "mt/transaction.hpp"

#include <memory>

namespace
{

struct TestContext
{
    std::shared_ptr<mt::backends::memory::MemoryBackend> backend =
        std::make_shared<mt::backends::memory::MemoryBackend>();
    mt::Database database{backend};
    dl::LeaseManager leases{database};
};

} // namespace

TEST_CASE("acquire stores a new lease")
{
    TestContext ctx;

    auto lease = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "queue:orders:consumer",
            .holder_id = "worker-1",
            .ttl_ms = 100,
            .now_ms = 1000
        }
    );

    REQUIRE(lease.has_value());
    CHECK(lease->resource_key == "queue:orders:consumer");
    CHECK(lease->holder_id == "worker-1");
    CHECK(lease->fencing_token == 1);
    CHECK(lease->expires_at_ms == 1100);
    CHECK(lease->updated_at_ms == 1000);
}

TEST_CASE("acquire rejects an unexpired lease")
{
    TestContext ctx;

    REQUIRE(ctx.leases
                .try_acquire(
                    dl::AcquireLeaseRequest{
                        .resource_key = "resource",
                        .holder_id = "worker-1",
                        .ttl_ms = 100,
                        .now_ms = 1000
                    }
                )
                .has_value());

    auto competing = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-2", .ttl_ms = 100, .now_ms = 1099
        }
    );

    CHECK_FALSE(competing.has_value());
}

TEST_CASE("acquire after expiry increments the fencing token")
{
    TestContext ctx;

    REQUIRE(ctx.leases
                .try_acquire(
                    dl::AcquireLeaseRequest{
                        .resource_key = "resource",
                        .holder_id = "worker-1",
                        .ttl_ms = 100,
                        .now_ms = 1000
                    }
                )
                .has_value());

    auto replacement = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-2", .ttl_ms = 50, .now_ms = 1100
        }
    );

    REQUIRE(replacement.has_value());
    CHECK(replacement->holder_id == "worker-2");
    CHECK(replacement->fencing_token == 2);
    CHECK(replacement->expires_at_ms == 1150);
}

TEST_CASE("renew extends only the current holder lease")
{
    TestContext ctx;

    auto acquired = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-1", .ttl_ms = 100, .now_ms = 1000
        }
    );
    REQUIRE(acquired.has_value());

    auto renewed = ctx.leases.renew(
        dl::RenewLeaseRequest{
            .resource_key = "resource",
            .holder_id = "worker-1",
            .fencing_token = acquired->fencing_token,
            .ttl_ms = 200,
            .now_ms = 1050
        }
    );

    REQUIRE(renewed.has_value());
    CHECK(renewed->holder_id == "worker-1");
    CHECK(renewed->fencing_token == acquired->fencing_token);
    CHECK(renewed->expires_at_ms == 1250);
}

TEST_CASE("renew rejects wrong holders stale tokens and expired leases")
{
    TestContext ctx;

    auto acquired = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-1", .ttl_ms = 100, .now_ms = 1000
        }
    );
    REQUIRE(acquired.has_value());

    CHECK_FALSE(ctx.leases
                    .renew(
                        dl::RenewLeaseRequest{
                            .resource_key = "resource",
                            .holder_id = "worker-2",
                            .fencing_token = acquired->fencing_token,
                            .ttl_ms = 100,
                            .now_ms = 1050
                        }
                    )
                    .has_value());
    CHECK_FALSE(ctx.leases
                    .renew(
                        dl::RenewLeaseRequest{
                            .resource_key = "resource",
                            .holder_id = "worker-1",
                            .fencing_token = acquired->fencing_token + 1,
                            .ttl_ms = 100,
                            .now_ms = 1050
                        }
                    )
                    .has_value());
    CHECK_FALSE(ctx.leases
                    .renew(
                        dl::RenewLeaseRequest{
                            .resource_key = "resource",
                            .holder_id = "worker-1",
                            .fencing_token = acquired->fencing_token,
                            .ttl_ms = 100,
                            .now_ms = 1100
                        }
                    )
                    .has_value());
}

TEST_CASE("release expires only the current holder lease")
{
    TestContext ctx;

    auto acquired = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-1", .ttl_ms = 100, .now_ms = 1000
        }
    );
    REQUIRE(acquired.has_value());

    CHECK_FALSE(ctx.leases.release(
        dl::ReleaseLeaseRequest{
            .resource_key = "resource",
            .holder_id = "worker-2",
            .fencing_token = acquired->fencing_token,
            .now_ms = 1050
        }
    ));

    CHECK(ctx.leases.release(
        dl::ReleaseLeaseRequest{
            .resource_key = "resource",
            .holder_id = "worker-1",
            .fencing_token = acquired->fencing_token,
            .now_ms = 1050
        }
    ));

    auto replacement = ctx.leases.try_acquire(
        dl::AcquireLeaseRequest{
            .resource_key = "resource", .holder_id = "worker-2", .ttl_ms = 100, .now_ms = 1050
        }
    );
    REQUIRE(replacement.has_value());
    CHECK(replacement->fencing_token == 2);
}

TEST_CASE("get returns the stored lease")
{
    TestContext ctx;

    REQUIRE(ctx.leases
                .try_acquire(
                    dl::AcquireLeaseRequest{
                        .resource_key = "resource",
                        .holder_id = "worker-1",
                        .ttl_ms = 100,
                        .now_ms = 1000
                    }
                )
                .has_value());

    auto stored = ctx.leases.get(dl::GetLeaseRequest{.resource_key = "resource"});

    REQUIRE(stored.has_value());
    CHECK(stored->resource_key == "resource");
    CHECK(stored->holder_id == "worker-1");
    CHECK(stored->fencing_token == 1);
}

TEST_CASE("lease operations can use caller-owned transactions")
{
    TestContext ctx;
    mt::TransactionProvider txs{ctx.database};

    txs.run(
        [&](mt::Transaction& tx)
        {
            auto acquired = ctx.leases.try_acquire(
                tx, dl::AcquireLeaseRequest{
                        .resource_key = "resource",
                        .holder_id = "worker-1",
                        .ttl_ms = 100,
                        .now_ms = 1000
                    }
            );
            REQUIRE(acquired.has_value());

            auto stored = ctx.leases.get(tx, dl::GetLeaseRequest{.resource_key = "resource"});
            REQUIRE(stored.has_value());

            auto renewed = ctx.leases.renew(
                tx, dl::RenewLeaseRequest{
                        .resource_key = "resource",
                        .holder_id = "worker-1",
                        .fencing_token = acquired->fencing_token,
                        .ttl_ms = 200,
                        .now_ms = 1050
                    }
            );
            REQUIRE(renewed.has_value());
            CHECK(renewed->expires_at_ms == 1250);
        }
    );
}
