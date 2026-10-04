#pragma once
#include "DatabaseManager.hpp"
#include <string>
struct ServerConfig
{
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string password;
};
class ViewerServer
{
public:
    ViewerServer(ServerConfig config, DatabaseManager &database) : config_(std::move(config)), database_(database) {}
    int run();

private:
    ServerConfig config_;
    DatabaseManager &database_;
};