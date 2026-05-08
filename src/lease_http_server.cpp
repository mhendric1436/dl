#include "dl/lease_http_server.hpp"

#include "httplib/httplib.h"
#include "mt/json_parser.hpp"

#include <exception>
#include <functional>
#include <stdexcept>
#include <string>

namespace dl
{
namespace
{

constexpr const char* JSON_CONTENT_TYPE = "application/json";

void write_result(
    httplib::Response& response,
    const LeaseServiceResult& result
)
{
    response.status = http_status(result.status);
    if (has_response_body(result.status))
    {
        response.set_content(result.body.canonical_string(), JSON_CONTENT_TYPE);
    }
}

mt::Json parse_body(const httplib::Request& request)
{
    if (request.body.empty())
    {
        return mt::Json::object({});
    }
    return mt::parse_json(request.body);
}

void write_invalid_json(
    httplib::Response& response,
    const std::exception& error
)
{
    response.status = 400;
    response.set_content(
        lease_error_json("invalid_json", error.what()).canonical_string(), JSON_CONTENT_TYPE
    );
}

using JsonHandler = std::function<LeaseServiceResult(const mt::Json&)>;

void handle_json_request(
    const httplib::Request& request,
    httplib::Response& response,
    const JsonHandler& handler
)
{
    try
    {
        write_result(response, handler(parse_body(request)));
    }
    catch (const std::exception& error)
    {
        write_invalid_json(response, error);
    }
}

} // namespace

struct LeaseHttpServer::Impl
{
    LeaseService& service;
    int port;
    bool bound = false;
    httplib::Server server;

    Impl(
        LeaseService& service,
        int port
    )
        : service(service),
          port(port)
    {
        register_routes();
    }

    int bind()
    {
        int actual = -1;
        if (port == 0)
        {
            actual = server.bind_to_any_port("0.0.0.0");
        }
        else
        {
            actual = server.bind_to_port("0.0.0.0", port) ? port : -1;
        }

        if (actual < 0)
        {
            throw std::runtime_error("failed to bind server on port " + std::to_string(port));
        }

        bound = true;
        return actual;
    }

    void start()
    {
        if (!bound)
        {
            (void)bind();
        }
        if (!server.listen_after_bind())
        {
            throw std::runtime_error("failed to start server on port " + std::to_string(port));
        }
    }

    void stop()
    {
        server.stop();
    }

    void register_routes()
    {
        server.Get(
            R"(/v1/leases/([^/]+))",
            [this](const httplib::Request& request, httplib::Response& response)
            { write_result(response, service.get_lease(request.matches[1])); }
        );

        server.Post(
            R"(/v1/leases/([^/]+):acquire)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto resource_key = request.matches[1].str();
                handle_json_request(
                    request, response,
                    [&](const mt::Json& body) { return service.acquire_lease(resource_key, body); }
                );
            }
        );

        server.Post(
            R"(/v1/leases/([^/]+):renew)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto resource_key = request.matches[1].str();
                handle_json_request(
                    request, response,
                    [&](const mt::Json& body) { return service.renew_lease(resource_key, body); }
                );
            }
        );

        server.Post(
            R"(/v1/leases/([^/]+):release)",
            [this](const httplib::Request& request, httplib::Response& response)
            {
                auto resource_key = request.matches[1].str();
                handle_json_request(
                    request, response,
                    [&](const mt::Json& body) { return service.release_lease(resource_key, body); }
                );
            }
        );
    }
};

LeaseHttpServer::LeaseHttpServer(
    LeaseService& service,
    int port
)
    : impl_(
          std::make_unique<Impl>(
              service,
              port
          )
      )
{
}

LeaseHttpServer::~LeaseHttpServer() = default;

int LeaseHttpServer::bind()
{
    return impl_->bind();
}

void LeaseHttpServer::start()
{
    impl_->start();
}

void LeaseHttpServer::stop()
{
    impl_->stop();
}

} // namespace dl
