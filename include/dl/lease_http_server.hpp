#pragma once

#include "dl/lease_service.hpp"

#include <memory>

namespace dl
{

class LeaseHttpServer
{
  public:
    LeaseHttpServer(
        LeaseService& service,
        int port
    );
    ~LeaseHttpServer();

    int bind();
    void start();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dl
