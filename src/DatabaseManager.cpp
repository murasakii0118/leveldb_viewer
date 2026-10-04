#include "DatabaseManager.hpp"
#include <leveldb/write_batch.h>
#include <filesystem>
#include <algorithm>
#include <shared_mutex>
namespace fs = std::filesystem;
static bool hasSuffix(const std::string &value, const std::string &suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; }
static bool startsWith(const std::string &value, const std::string &prefix) { return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0; }
void DatabaseManager::invalidateCacheLocked()
{
    std::lock_guard<std::mutex> cacheLock(cacheMutex_);
    sstCacheValid_ = false;
    treeCache_.clear();
}
static std::string prefixLimit(std::string prefix)
{
    for (size_t i = prefix.size(); i > 0; --i)
    {
        auto &byte = prefix[i - 1];
        if (static_cast<unsigned char>(byte) != 0xff)
        {
            byte = static_cast<char>(static_cast<unsigned char>(byte) + 1);
            prefix.resize(i);
            return prefix;
        }
    }
    return {};
}

bool DatabaseManager::open(const std::string &path, std::string &error, size_t cacheMb)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    leveldb::Options options;
    options.create_if_missing = false;
    options.paranoid_checks = true;
    options.compression = leveldb::kZstdCompression;
    auto blockCache = std::unique_ptr<leveldb::Cache>(leveldb::NewLRUCache(std::max<size_t>(1, cacheMb) * 1024ULL * 1024ULL));
    options.block_cache = blockCache.get();
    options.write_buffer_size = 16ULL * 1024ULL * 1024ULL;
    options.block_size = 32ULL * 1024ULL;
    leveldb::DB *raw = nullptr;
    auto status = leveldb::DB::Open(options, path, &raw);
    if (!status.ok())
    {
        error = status.ToString();
        return false;
    }
    db_.reset(raw);
    blockCache_ = std::move(blockCache);
    path_ = path;
    invalidateCacheLocked();
    return true;
}
bool DatabaseManager::get(const std::string &key, std::string &value, std::string &error)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!db_)
    {
        error = "database is not open";
        return false;
    }
    auto status = db_->Get(leveldb::ReadOptions(), key, &value);
    if (status.IsNotFound())
        return false;
    if (!status.ok())
    {
        error = status.ToString();
        return false;
    }
    return true;
}
bool DatabaseManager::put(const std::string &key, const std::string &value, std::string &error)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!db_)
    {
        error = "database is not open";
        return false;
    }
    auto status = db_->Put(leveldb::WriteOptions(), key, value);
    if (!status.ok())
    {
        error = status.ToString();
        return false;
    }
    invalidateCacheLocked();
    return true;
}
bool DatabaseManager::remove(const std::string &key, std::string &error)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!db_)
    {
        error = "database is not open";
        return false;
    }
    auto status = db_->Delete(leveldb::WriteOptions(), key);
    if (!status.ok())
    {
        error = status.ToString();
        return false;
    }
    invalidateCacheLocked();
    return true;
}
bool DatabaseManager::writeBatch(const std::vector<Operation> &operations, std::string &error)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!db_)
    {
        error = "database is not open";
        return false;
    }
    leveldb::WriteBatch batch;
    for (const auto &operation : operations)
    {
        if (operation.type == Operation::Put)
            batch.Put(operation.key, operation.value);
        else
            batch.Delete(operation.key);
    }
    auto status = db_->Write(leveldb::WriteOptions(), &batch);
    if (!status.ok())
    {
        error = status.ToString();
        return false;
    }
    invalidateCacheLocked();
    return true;
}
std::vector<Record> DatabaseManager::scan(const ScanOptions &options, bool &hasMore, std::string &nextKey, std::string &error)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<Record> result;
    hasMore = false;
    nextKey.clear();
    if (!db_)
    {
        error = "database is not open";
        return result;
    }
    auto iterator = std::unique_ptr<leveldb::Iterator>(db_->NewIterator(leveldb::ReadOptions()));
    std::string start = options.start.empty() ? options.prefix : options.start;
    if (!options.reverse)
    {
        iterator->Seek(start);
        while (iterator->Valid())
        {
            std::string key = iterator->key().ToString();
            if (!options.prefix.empty() && key.rfind(options.prefix, 0) != 0)
                break;
            if (!options.end.empty() && key >= options.end)
                break;
            if (result.size() >= options.limit)
            {
                hasMore = true;
                nextKey = key;
                break;
            }
            result.push_back({key, iterator->value().ToString()});
            iterator->Next();
        }
    }
    else
    {
        if (start.empty())
            iterator->SeekToLast();
        else
        {
            iterator->Seek(start);
            if (!iterator->Valid())
                iterator->SeekToLast();
            else if (iterator->key().ToString() > start)
                iterator->Prev();
        }
        while (iterator->Valid())
        {
            std::string key = iterator->key().ToString();
            if (!options.prefix.empty() && key.rfind(options.prefix, 0) != 0)
                break;
            if (!options.end.empty() && key < options.end)
                break;
            if (result.size() >= options.limit)
            {
                hasMore = true;
                nextKey = key;
                break;
            }
            result.push_back({key, iterator->value().ToString()});
            iterator->Prev();
        }
    }
    if (!iterator->status().ok())
        error = iterator->status().ToString();
    return result;
}
std::vector<SstInfo> DatabaseManager::sstList(std::string &error)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex_);
        if (sstCacheValid_)
            return sstCache_;
    }
    std::vector<SstInfo> result;
    if (!db_)
    {
        error = "database is not open";
        return result;
    }
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(path_, ec))
    {
        if (!entry.is_regular_file())
            continue;
        auto name = entry.path().filename().string();
        if (name.size() > 4 && (hasSuffix(name, ".ldb") || hasSuffix(name, ".sst")))
        {
            SstInfo file;
            file.name = name;
            file.size = entry.file_size();
            auto dot = name.find('.');
            if (dot != std::string::npos)
            {
                try
                {
                    file.level = std::stoi(name.substr(0, dot));
                }
                catch (...)
                {
                }
            }
            file.compression = "Zstd";
            result.push_back(file);
        }
    }
    std::sort(result.begin(), result.end(), [](const SstInfo &a, const SstInfo &b)
              { return a.name < b.name; });
    if (ec)
        error = ec.message();
    if (error.empty())
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex_);
        sstCache_ = result;
        sstCacheValid_ = true;
    }
    return result;
}
bool DatabaseManager::sstInfo(const std::string &name, SstInfo &result, std::string &error)
{
    for (const auto &item : sstList(error))
    {
        if (item.name == name)
        {
            result = item;
            return true;
        }
    }
    error = "SST file not found";
    return false;
}
