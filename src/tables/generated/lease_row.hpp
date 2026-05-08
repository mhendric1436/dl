#pragma once

#include "mt/core.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dl::tables
{

struct LeaseRow
{
    std::string resourceKey;
    std::string holderId;
    std::int64_t fencingToken;
    std::int64_t expiresAtMs;
    std::int64_t updatedAtMs;

    friend bool operator==(
        const LeaseRow&,
        const LeaseRow&
    ) = default;
};

struct LeaseRowMapping
{
    static constexpr std::string_view table_name = "leases";
    static constexpr int schema_version = 1;
    static constexpr std::string_view field_resourceKey = "resourceKey";
    static constexpr std::string_view field_holderId = "holderId";
    static constexpr std::string_view field_fencingToken = "fencingToken";
    static constexpr std::string_view field_expiresAtMs = "expiresAtMs";
    static constexpr std::string_view field_updatedAtMs = "updatedAtMs";
    static constexpr std::string_view key_field = field_resourceKey;
    static constexpr std::string_view index_0_name = "idx_lease_holder";
    static constexpr std::string_view index_0_path = "$.holderId";
    static constexpr std::string_view index_1_name = "idx_lease_expires_at";
    static constexpr std::string_view index_1_path = "$.expiresAtMs";

    static std::string key(const LeaseRow& row)
    {
        return row.resourceKey;
    }

    static std::vector<mt::FieldSpec> fields()
    {
        return {
            mt::FieldSpec::string(std::string(field_resourceKey)).mark_required(true),
            mt::FieldSpec::string(std::string(field_holderId)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_fencingToken)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_expiresAtMs)).mark_required(true),
            mt::FieldSpec::int64(std::string(field_updatedAtMs)).mark_required(true)
        };
    }

    static mt::Json to_json(const LeaseRow& row)
    {
        return mt::Json::object(
            {{std::string(field_resourceKey), row.resourceKey},
             {std::string(field_holderId), row.holderId},
             {std::string(field_fencingToken), row.fencingToken},
             {std::string(field_expiresAtMs), row.expiresAtMs},
             {std::string(field_updatedAtMs), row.updatedAtMs}}
        );
    }

    static LeaseRow from_json(const mt::Json& json)
    {
        return LeaseRow{
            .resourceKey = json[std::string(field_resourceKey)].as_string(),
            .holderId = json[std::string(field_holderId)].as_string(),
            .fencingToken = json[std::string(field_fencingToken)].as_int64(),
            .expiresAtMs = json[std::string(field_expiresAtMs)].as_int64(),
            .updatedAtMs = json[std::string(field_updatedAtMs)].as_int64()
        };
    }

    static std::vector<mt::IndexSpec> indexes()
    {
        return {
            mt::IndexSpec::json_path_index(std::string(index_0_name), std::string(index_0_path)),
            mt::IndexSpec::json_path_index(std::string(index_1_name), std::string(index_1_path))
        };
    }
};

} // namespace dl::tables
