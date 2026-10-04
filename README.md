# LevelDB Viewer

[中文](#中文) | [English](#english)

LevelDB Viewer is a lightweight local web interface for browsing and editing a LevelDB database. It uses LevelDB, Zstd, cpp-httplib, and nlohmann::json.

## 中文

### 功能

- Key Tree 按分隔符懒加载层级键。
- 展开叶子节点时按需读取 Value。
- 支持 Raw、JSON、HEX、Base64 预览。
- 支持 Put、Delete、Batch Write。
- 读请求支持并发执行。
- 默认启用 500 MB LevelDB block cache。
- 树查询和 SST 列表在进程内缓存，成功写入后自动失效。

### 依赖

准备以下依赖目录，并在构建时通过 CMake 参数传入：

- `LEVELDB_DIR`：LevelDB 源码目录，必须包含 `CMakeLists.txt`。
- `ZSTD_DIR`：Zstd 根目录，必须包含 `include/zstd.h` 和兼容的静态库。
- `CPPHTTPLIB_DIR`：cpp-httplib 目录，必须包含 `httplib.h` 和 `httplib.cpp`。
- `JSON_INCLUDE_DIR`：包含 `json.hpp` 的目录。

需要 CMake 3.16 或更高版本，以及支持 C++17 的编译器。LevelDB 源码必须包含本项目使用的 Zstd 支持。

### 构建

将下面的占位符替换为实际路径。每条命令都是完整的一行，不依赖 shell 的续行符。

Windows PowerShell：

```powershell
cmake -S <project-dir> -B <project-dir>/build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=<mingw>/bin/g++.exe -DLEVELDB_DIR=<leveldb-dir> -DZSTD_DIR=<zstd-dir> -DCPPHTTPLIB_DIR=<cpp-httplib-dir> -DJSON_INCLUDE_DIR=<nlohmann-dir>
cmake --build <project-dir>/build -j 4
```

Linux 或 macOS：

```bash
cmake -S <project-dir> -B <project-dir>/build -DLEVELDB_DIR=<leveldb-dir> -DZSTD_DIR=<zstd-dir> -DCPPHTTPLIB_DIR=<cpp-httplib-dir> -DJSON_INCLUDE_DIR=<nlohmann-dir>
cmake --build <project-dir>/build --parallel
```

如果 CMake 自动选择了错误的编译器，请显式设置 `CMAKE_CXX_COMPILER`，或删除旧的 `build` 目录后重新配置。

### 启动

```text
leveldb_viewer --db <database-dir> --pwd <write-password> [--host <address>] [--port <port>] [--cache-mb <size>]
```

参数：

- `--db`：要打开的 LevelDB 数据库目录。
- `--pwd`：写操作密码，必填。
- `--host`：监听地址，默认 `127.0.0.1`。
- `--port`：监听端口，默认 `8080`。
- `--cache-mb`：block cache 大小，默认 `500` MB。

启动后访问 `http://127.0.0.1:<port>/`。读取接口不需要密码，写入接口需要使用启动时设置的密码。

### API

所有响应均为 UTF-8 JSON。请对 key、prefix、file 和 password 等查询参数进行 URL 编码。

#### 检查服务

```http
GET /api/status
```

响应示例：

```json
{"connected":true,"ok":true}
```

#### 读取单个键

```http
GET /api/kv/get?key=<url-encoded-key>
```

响应字段包括 `ok`、`found`、`key`，找到时还会返回 `value` 和 `valueSize`。

#### 查询 Key Tree

```http
GET /api/kv/tree?prefix=<prefix>&delimiter=:&limit=200&start=<next-key>
```

参数：

- `prefix`：键前缀。
- `delimiter`：层级分隔符，默认 `:`。
- `limit`：每页节点数，范围 1 到 1000。
- `start`：分页起始键。

响应中的 `leaf=true` 表示实际键；`leaf=false` 表示可以继续展开的目录。返回 `hasMore=true` 时，下一次请求使用响应中的 `nextKey`。

#### 扫描记录

```http
GET /api/kv/scan?prefix=<prefix>&start=<start>&end=<end>&limit=100&reverse=false
```

用于批量读取记录。大量数据查询时请使用较小的 `limit`。

#### 查看 SST 文件

```http
GET /api/sst/list
GET /api/sst/info?file=<file-name>
```

#### 写入或更新

```http
POST /api/kv/put?pwd=<password>
Content-Type: application/json

{"key":"example:key","value":"example value"}
```

#### 删除

```http
DELETE /api/kv/delete?pwd=<password>
Content-Type: application/json

{"key":"example:key"}
```

#### 批量操作

```http
POST /api/kv/batch?pwd=<password>
Content-Type: application/json

{"operations":[{"type":"put","key":"a","value":"1"},{"type":"delete","key":"b"}]}
```

`type` 只能是 `put` 或 `delete`。成功的写操作会清理服务端缓存。

### curl 示例

```bash
curl "http://127.0.0.1:8080/api/status"
curl "http://127.0.0.1:8080/api/kv/get?key=example%3Akey"
curl -X POST "http://127.0.0.1:8080/api/kv/put?pwd=<password>" -H "Content-Type: application/json" --data '{"key":"example:key","value":"hello"}'
curl -X DELETE "http://127.0.0.1:8080/api/kv/delete?pwd=<password>" -H "Content-Type: application/json" --data '{"key":"example:key"}'
```

### 安全注意事项

- 默认只监听本机地址，不建议直接暴露到公网。
- 不要把真实密码提交到 README、脚本或命令历史中。
- 修改数据库前请先备份。
- 不要让多个不兼容的程序同时写入同一个 LevelDB 数据库。

## English

### Features

- Lazy hierarchical browsing with the Key Tree.
- Values are loaded only when a leaf is opened.
- Raw, JSON, HEX, and Base64 value preview.
- Put, Delete, and Batch Write operations.
- Concurrent read requests.
- 500 MB block cache by default.
- In-process tree and SST caches invalidated after successful writes.

### Dependencies

Pass these directories to CMake:

- `LEVELDB_DIR`: LevelDB source directory containing `CMakeLists.txt`.
- `ZSTD_DIR`: Zstd package directory containing `include/zstd.h` and a compatible static library.
- `CPPHTTPLIB_DIR`: cpp-httplib directory containing `httplib.h` and `httplib.cpp`.
- `JSON_INCLUDE_DIR`: directory containing `json.hpp`.

CMake 3.16+ and a C++17 compiler are required. The LevelDB source must include the Zstd support used by this project.

### Build

Replace the placeholders with paths on your machine. Each command is complete on one line and does not rely on shell-specific continuation characters.

```bash
cmake -S <project-dir> -B <project-dir>/build -DLEVELDB_DIR=<leveldb-dir> -DZSTD_DIR=<zstd-dir> -DCPPHTTPLIB_DIR=<cpp-httplib-dir> -DJSON_INCLUDE_DIR=<nlohmann-dir>
cmake --build <project-dir>/build --parallel
```

On Windows with MinGW, add `-G "MinGW Makefiles"` and set `-DCMAKE_CXX_COMPILER=<mingw>/bin/g++.exe` if needed.

### Run

```text
leveldb_viewer --db <database-dir> --pwd <write-password> [--host <address>] [--port <port>] [--cache-mb <size>]
```

The default host is `127.0.0.1`, the default port is `8080`, and the default block cache is `500` MB. Open `http://127.0.0.1:<port>/` after startup.

### API reference

| Method | Endpoint | Authentication | Description |
|---|---|---|---|
| `GET` | `/api/status` | None | Check server status |
| `GET` | `/api/kv/get?key=...` | None | Read one key |
| `GET` | `/api/kv/tree?...` | None | Browse hierarchical keys |
| `GET` | `/api/kv/scan?...` | None | Scan records |
| `GET` | `/api/sst/list` | None | List SST files |
| `GET` | `/api/sst/info?file=...` | None | Inspect an SST file |
| `POST` | `/api/kv/put?pwd=...` | Password | Insert or update a key |
| `DELETE` | `/api/kv/delete?pwd=...` | Password | Delete a key |
| `POST` | `/api/kv/batch?pwd=...` | Password | Apply multiple operations |

All request bodies are UTF-8 JSON. URL-encode query parameters. Use the returned `nextKey` for pagination. Successful writes invalidate server-side caches.

## License

This project is distributed under the Apache License 2.0. See `LICENSE` for the complete terms.
