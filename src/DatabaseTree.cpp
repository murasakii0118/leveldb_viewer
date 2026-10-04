#include <DatabaseManager.hpp>

#include <algorithm>

namespace {
bool startsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}


std::string prefixLimit(std::string prefix) {
  for (size_t index = prefix.size(); index > 0; --index) {
    auto& byte = prefix[index - 1];
    if (static_cast<unsigned char>(byte) != 0xff) {
      byte = static_cast<char>(static_cast<unsigned char>(byte) + 1);
      prefix.resize(index);
      return prefix;
    }
  }
  return {};
}
}

std::vector<TreeEntry> DatabaseManager::tree(const TreeOptions& options,
                                             bool& hasMore,
                                             std::string& nextKey,
                                             std::string& error) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<TreeEntry> result;
  hasMore = false;
  nextKey.clear();
  if (!db_) {
    error = "database is not open";
    return result;
  }

  const std::string delimiter = options.delimiter.empty() ? ":" : options.delimiter;
  const size_t limit = std::max<size_t>(1, options.limit);
  const std::string cacheKey = options.prefix + "\x1f" + delimiter + "\x1f" +
                               options.start + "\x1f" + std::to_string(limit);
  {
    std::lock_guard<std::mutex> cacheLock(cacheMutex_);
    const auto cached = treeCache_.find(cacheKey);
    if (cached != treeCache_.end()) {
      hasMore = cached->second.hasMore;
      nextKey = cached->second.nextKey;
      return cached->second.items;
    }
  }
  auto iterator = std::unique_ptr<leveldb::Iterator>(db_->NewIterator(leveldb::ReadOptions()));
  iterator->Seek(options.start.empty() ? options.prefix : options.start);

  auto setNext = [&]() {
    if (iterator->Valid() &&
        (options.prefix.empty() || startsWith(iterator->key().ToString(), options.prefix))) {
      hasMore = true;
      nextKey = iterator->key().ToString();
    }
  };

  while (iterator->Valid()) {
    const std::string key = iterator->key().ToString();
    if (!options.prefix.empty() && !startsWith(key, options.prefix)) break;
    const std::string remainder = key.substr(options.prefix.size());

    if (remainder.empty()) {
      result.push_back({key, key, {}, static_cast<uint64_t>(iterator->value().size()), true});
      iterator->Next();
      if (result.size() >= limit) setNext();
      if (result.size() >= limit) break;
      continue;
    }

    const size_t separator = remainder.find(delimiter);
    if (separator == std::string::npos) {
      result.push_back({remainder, key, {}, static_cast<uint64_t>(iterator->value().size()), true});
      iterator->Next();
      if (result.size() >= limit) setNext();
      if (result.size() >= limit) break;
      continue;
    }

    const std::string childName = remainder.substr(0, separator + delimiter.size());
    const std::string childPrefix = options.prefix + childName;
    result.push_back({childName, {}, childPrefix, 0, false});
    const std::string upperBound = prefixLimit(childPrefix);
    if (result.size() >= limit) {
      if (upperBound.empty()) iterator->SeekToLast();
      else iterator->Seek(upperBound);
      setNext();
      break;
    }
    if (upperBound.empty()) break;
    iterator->Seek(upperBound);
  }

  if (!iterator->status().ok()) error = iterator->status().ToString();
  if (error.empty()) {
    std::lock_guard<std::mutex> cacheLock(cacheMutex_);
    treeCache_[cacheKey] = TreeCacheEntry{result, hasMore, nextKey};
  }
  return result;
}
