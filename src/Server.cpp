#include "Server.hpp"
#include <WebPage.hpp>
#include "ViewerJson.hpp"
#include "httplib.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
namespace
{
    using viewer::Json;
    const char *PAGE = viewer::INDEX_HTML;
    std::string json(const Json &value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }
    void send(httplib::Response &res, const Json &value, int status = 200)
    {
        res.status = status;
        res.set_content(value.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json; charset=UTF-8");
    }
    Json errorJson(const std::string &e) { return Json(Json::object_t{{"ok", false}, {"error", e}}); }
    bool field(const Json &body, const char *name, std::string &out)
    {
        if (!body.is_object())
            return false;
        auto it = body.find(name);
        if (it == body.end() || !it->is_string())
            return false;
        out = it->get<std::string>();
        return true;
    }
    bool auth(const httplib::Request &req, const std::string &password)
    {
        if (!req.has_param("pwd"))
            return false;
        auto supplied = req.get_param_value("pwd");
        if (supplied.size() != password.size())
            return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < password.size(); ++i)
            diff |= static_cast<unsigned char>(supplied[i] ^ password[i]);
        return diff == 0;
    }
    size_t number(const httplib::Request &r, const char *name, size_t fallback)
    {
        if (!r.has_param(name))
            return fallback;
        try
        {
            return std::min<size_t>(1000, std::max<size_t>(1, std::stoull(r.get_param_value(name))));
        }
        catch (...)
        {
            return fallback;
        }
    }
}
int ViewerServer::run()
{
    httplib::Server server;
    auto writeCheck = [this](const httplib::Request &req, httplib::Response &res)
    {if(!auth(req,config_.password)){send(res,errorJson("invalid password"),403);return false;}return true; };
    server.set_error_handler([](const httplib::Request &, httplib::Response &res)
                             {if(res.status==404)send(res,errorJson("not found"),404); });
    server.Get("/", [](const httplib::Request &, httplib::Response &res)
               { res.set_content(PAGE, "text/html; charset=UTF-8"); });
    server.Get("/api/status", [](const httplib::Request &, httplib::Response &res)
               { send(res, Json(Json::object_t{{"ok", true}, {"connected", true}})); });
    server.Get("/api/kv/get", [this](const httplib::Request &req, httplib::Response &res)
               {if(!req.has_param("key")){send(res,errorJson("missing key"),400);return;}std::string value,e,key=req.get_param_value("key");bool found=database_.get(key,value,e);if(!e.empty()){send(res,errorJson(e),500);return;}Json::object_t o{{"ok",true},{"found",found},{"key",key}};if(found){o["value"]=value;o["valueSize"]=value.size();}send(res,o); });
    server.Get("/api/kv/tree", [this](const httplib::Request &req, httplib::Response &res)
               {TreeOptions options;if(req.has_param("prefix"))options.prefix=req.get_param_value("prefix");if(req.has_param("delimiter"))options.delimiter=req.get_param_value("delimiter");if(req.has_param("start"))options.start=req.get_param_value("start");options.limit=number(req,"limit",200);bool more=false;std::string next,error;auto entries=database_.tree(options,more,next,error);if(!error.empty()){send(res,errorJson(error),500);return;}Json::array_t items;for(const auto&entry:entries)items.emplace_back(Json(Json::object_t{{"name",entry.name},{"key",entry.key},{"prefix",entry.prefix},{"valueSize",entry.valueSize},{"leaf",entry.leaf}}));Json::object_t output{{"ok",true},{"items",items},{"count",entries.size()},{"hasMore",more},{"prefix",options.prefix},{"delimiter",options.delimiter}};if(more)output["nextKey"]=next;send(res,output); });
    server.Get("/api/kv/scan", [this](const httplib::Request &req, httplib::Response &res)
               {ScanOptions o;if(req.has_param("prefix"))o.prefix=req.get_param_value("prefix");if(req.has_param("start"))o.start=req.get_param_value("start");if(req.has_param("end"))o.end=req.get_param_value("end");o.limit=number(req,"limit",100);o.reverse=req.has_param("reverse")&&(req.get_param_value("reverse")=="1"||req.get_param_value("reverse")=="true");bool more=false;std::string next,e;auto records=database_.scan(o,more,next,e);if(!e.empty()){send(res,errorJson(e),500);return;}Json::array_t items;for(auto&r:records)items.emplace_back(Json(Json::object_t{{"key",r.key},{"value",r.value},{"valueSize",r.value.size()}}));Json::object_t out{{"ok",true},{"items",items},{"count",records.size()},{"hasMore",more}};if(more)out["nextKey"]=next;send(res,out); });
    server.Post("/api/kv/put", [this](const httplib::Request &req, httplib::Response &res)
                {if(!auth(req,config_.password)){send(res,errorJson("invalid password"),403);return;}try{auto body=Json::parse(req.body);std::string key,value;if(!field(body,"key",key)||!field(body,"value",value)){send(res,errorJson("key and value are required"),400);return;}std::string e;if(!database_.put(key,value,e)){send(res,errorJson(e),500);return;}send(res,Json(Json::object_t{{"ok",true}}));}catch(const std::exception&e){send(res,errorJson(e.what()),400);} });
    server.Delete("/api/kv/delete", [this](const httplib::Request &req, httplib::Response &res)
                  {if(!auth(req,config_.password)){send(res,errorJson("invalid password"),403);return;}try{auto body=Json::parse(req.body);std::string key;if(!field(body,"key",key)){send(res,errorJson("key is required"),400);return;}std::string e;if(!database_.remove(key,e)){send(res,errorJson(e),500);return;}send(res,Json(Json::object_t{{"ok",true}}));}catch(const std::exception&e){send(res,errorJson(e.what()),400);} });
    server.Post("/api/kv/batch", [this](const httplib::Request &req, httplib::Response &res)
                {if(!auth(req,config_.password)){send(res,errorJson("invalid password"),403);return;}try{auto body=Json::parse(req.body);auto it=body.find("operations");if(it==body.end()||!it->is_array()){send(res,errorJson("operations are required"),400);return;}std::vector<Operation> ops;for(const auto&item:*it){std::string type,key,value;if(!field(item,"type",type)||!field(item,"key",key)){send(res,errorJson("invalid operation"),400);return;}if(type=="put"){if(!field(item,"value",value)){send(res,errorJson("put value is required"),400);return;}ops.push_back({Operation::Put,key,value});}else if(type=="delete")ops.push_back({Operation::Delete,key,{}});else{send(res,errorJson("unknown operation type"),400);return;}}std::string e;if(!database_.writeBatch(ops,e)){send(res,errorJson(e),500);return;}send(res,Json(Json::object_t{{"ok",true},{"count",ops.size()}}));}catch(const std::exception&e){send(res,errorJson(e.what()),400);} });
    server.Get("/api/sst/list", [this](const httplib::Request &, httplib::Response &res)
               {std::string e;auto files=database_.sstList(e);if(!e.empty()){send(res,errorJson(e),500);return;}Json::array_t items;for(auto&f:files)items.emplace_back(Json(Json::object_t{{"name",f.name},{"size",f.size},{"level",f.level},{"compression",f.compression}}));send(res,Json(Json::object_t{{"ok",true},{"items",items}})); });
    server.Get("/api/sst/info", [this](const httplib::Request &req, httplib::Response &res)
               {if(!req.has_param("file")){send(res,errorJson("missing file"),400);return;}std::string e;SstInfo f;if(!database_.sstInfo(req.get_param_value("file"),f,e)){send(res,errorJson(e),404);return;}send(res,Json(Json::object_t{{"ok",true},{"name",f.name},{"size",f.size},{"level",f.level},{"compression",f.compression}})); });
    std::cout << "LevelDB Viewer listening on http://" << config_.host << ":" << config_.port << "/\n";
    if (!server.listen(config_.host, config_.port))
    {
        std::cerr << "failed to start server\n";
        return 1;
    }
    return 0;
}
