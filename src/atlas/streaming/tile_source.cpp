#include "atlas/streaming/tile_source.hpp"

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <utility>

namespace vulkax::atlas {
namespace {

void ensureActive(const std::shared_ptr<CancellationToken>& cancellation) {
  if (cancellation && cancellation->cancelled()) throw std::runtime_error("tile request cancelled");
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("could not read tile file: " + path.string());
  const auto size = input.tellg();
  if (size < 0) throw std::runtime_error("could not determine tile file size: " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input && !bytes.empty()) throw std::runtime_error("could not read full tile file: " + path.string());
  return bytes;
}

size_t appendCurlBytes(char* data, size_t size, size_t count, void* userData) {
  const size_t total = size * count;
  auto& bytes = *static_cast<std::vector<uint8_t>*>(userData);
  const auto* begin = reinterpret_cast<const uint8_t*>(data);
  bytes.insert(bytes.end(), begin, begin + total);
  return total;
}

}  // namespace

FileTileSource::FileTileSource(std::filesystem::path root)
    : root{std::move(root)} {}

std::future<TilePayload> FileTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::async,
      [root = root,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        std::filesystem::path relative{request.uri};
        if (relative.is_absolute()) {
          throw std::runtime_error("file tile URI must be relative");
        }
        const auto path = std::filesystem::weakly_canonical(root / relative);
        const auto canonicalRoot = std::filesystem::weakly_canonical(root);
        const auto relativeToRoot = path.lexically_relative(canonicalRoot);
        const auto firstComponent = relativeToRoot.begin();
        if (relativeToRoot.empty() ||
            (firstComponent != relativeToRoot.end() && *firstComponent == std::filesystem::path{".."})) {
          throw std::runtime_error("file tile URI escapes its source root");
        }
        auto bytes = readFile(path);
        ensureActive(cancellation);
        return TilePayload{request.key, std::move(bytes)};
      });
}

void MemoryTileSource::put(
    std::string uri,
    std::vector<uint8_t> bytes,
    std::string etag) {
  entries[std::move(uri)] = {std::move(bytes), std::move(etag)};
}

std::future<TilePayload> MemoryTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::deferred,
      [entries = entries,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        const auto found = entries.find(request.uri);
        if (found == entries.end()) throw std::runtime_error("memory tile URI was not found");
        return TilePayload{request.key, found->second.bytes, found->second.etag};
      });
}

HttpTileSource::HttpTileSource(std::string baseUrl)
    : baseUrl{std::move(baseUrl)} {
  const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (initialized != CURLE_OK) {
    throw std::runtime_error("curl_global_init failed");
  }
}

std::future<TilePayload> HttpTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::async,
      [baseUrl = baseUrl,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        std::vector<uint8_t> bytes;
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("curl_easy_init failed");
        const std::string url = baseUrl.empty() ? request.uri : baseUrl + request.uri;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlBytes);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
        const CURLcode result = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        ensureActive(cancellation);
        if (result != CURLE_OK || status >= 400) {
          throw std::runtime_error("HTTP tile request failed: " + url);
        }
        return TilePayload{request.key, std::move(bytes)};
      });
}

}  // namespace vulkax::atlas
