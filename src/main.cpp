#include "Server.hpp"
#include <iostream>
#include <string>
#include <unordered_map>
#include <stdexcept>
int main(int argc, char **argv)
{
    ServerConfig config;
    std::string dbPath;
    size_t cacheMb = 500;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto next = [&](std::string &out)
        {if(i+1>=argc){std::cerr<<"missing value for "<<arg<<"\n";return false;}out=argv[++i];return true; };
        if (arg == "--db")
        {
            if (!next(dbPath))
                return 2;
        }
        else if (arg == "--host")
        {
            if (!next(config.host))
                return 2;
        }
        else if (arg == "--pwd")
        {
            if (!next(config.password))
                return 2;
        }
        else if (arg == "--port")
        {
            std::string value;
            if (!next(value))
                return 2;
            try
            {
                config.port = std::stoi(value);
            }
            catch (...)
            {
                std::cerr << "invalid port\n";
                return 2;
            }
        }
        else if (arg == "--cache-mb")
        {
            std::string value;
            if (!next(value))
                return 2;
            try
            {
                cacheMb = std::stoull(value);
                if (cacheMb == 0)
                    throw std::invalid_argument("zero");
            }
            catch (...)
            {
                std::cerr << "invalid cache size\n";
                return 2;
            }
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: leveldb_viewer --db <path> --pwd <password> [--host 127.0.0.1] [--port 8080] [--cache-mb 500]\n";
            return 0;
        }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (dbPath.empty() || config.password.empty())
    {
        std::cerr << "--db and --pwd are required\n";
        return 2;
    }
    DatabaseManager database;
    std::string error;
    if (!database.open(dbPath, error, cacheMb))
    {
        std::cerr << "failed to open database: " << error << "\n";
        return 1;
    }
    return ViewerServer(config, database).run();
}