#pragma once
#include <leveldb/cache.h>
#include <leveldb/db.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
struct Operation
{
  enum Type
  {
    Put,
    Delete
  };
  Type type;
  std::string key;
  std::string value;
};
struct Record
{
  std::string key;
  std::string value;
};
struct ScanOptions
{
  std::string prefix, start, end;
  size_t limit = 100;
  bool reverse = false;
};
struct TreeOptions
{
  std::string prefix, delimiter = std::string(1, static_cast<char>(58));
  std::string start;
  size_t limit = 200;
};
struct TreeEntry
{
  std::string name;
  std::string key;
  std::string prefix;
  uint64_t valueSize = 0;
  bool leaf = false;
};
struct DatabaseInfo
{
  uint64_t keys = 0, approximateBytes = 0;
  size_t files = 0;
  std::string path;
};
struct SstInfo
{
  std::string name;
  uint64_t size = 0;
  int level = 0;
  std::string compression = "Unknown";
};
class DatabaseManager
{
public:
  bool open(const std::string &, std::string &, size_t cacheMb = 500);
  bool get(const std::string &, std::string &, std::string &);
  bool put(const std::string &, const std::string &, std::string &);
  bool remove(const std::string &, std::string &);
  bool writeBatch(const std::vector<Operation> &, std::string &);
  std::vector<Record> scan(const ScanOptions &, bool &, std::string &, std::string &);
  std::vector<TreeEntry> tree(const TreeOptions &, bool &, std::string &, std::string &);
  std::vector<SstInfo> sstList(std::string &);
  bool sstInfo(const std::string &, SstInfo &, std::string &);

private:
  struct TreeCacheEntry
  {
    std::vector<TreeEntry> items;
    bool hasMore = false;
    std::string nextKey;
  };
  void invalidateCacheLocked();
  std::unique_ptr<leveldb::Cache> blockCache_;
  std::unique_ptr<leveldb::DB> db_;
  std::string path_;
  mutable std::shared_mutex mutex_;
  mutable std::mutex cacheMutex_;
  bool sstCacheValid_ = false;
  std::vector<SstInfo> sstCache_;
  std::unordered_map<std::string, TreeCacheEntry> treeCache_;
};
