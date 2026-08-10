#include "atlas/streaming/tile_source.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vulkax::atlas {
namespace {

void ensureActive(const std::shared_ptr<CancellationToken>& cancellation) {
  if (cancellation && cancellation->isCancelled()) {
    throw std::runtime_error("tile request cancelled");
  }
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error("could not read tile file: " + path.string());
  const auto size = input.tellg();
  if (size < 0) throw std::runtime_error("could not determine tile file size: " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input && !bytes.empty()) {
    throw std::runtime_error("could not read full tile file: " + path.string());
  }
  return bytes;
}

size_t appendCurlBytes(char* data, size_t size, size_t count, void* userData) {
  const size_t total = size * count;
  auto& bytes = *static_cast<std::vector<uint8_t>*>(userData);
  const auto* begin = reinterpret_cast<const uint8_t*>(data);
  bytes.insert(bytes.end(), begin, begin + total);
  return total;
}

std::string trimHeaderValue(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
                      return std::isspace(c) != 0;
                    }).base();
  return first >= last ? std::string{} : std::string{first, last};
}

size_t captureCurlHeader(char* data, size_t size, size_t count, void* userData) {
  const size_t total = size * count;
  std::string_view line{data, total};
  constexpr std::string_view etagPrefix{"etag:"};
  if (line.size() >= etagPrefix.size()) {
    bool etag = true;
    for (size_t i = 0; i < etagPrefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(line[i])) != etagPrefix[i]) {
        etag = false;
        break;
      }
    }
    if (etag) {
      auto& destination = *static_cast<std::string*>(userData);
      destination = trimHeaderValue(std::string{line.substr(etagPrefix.size())});
    }
  }
  return total;
}

int curlProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* token = static_cast<const CancellationToken*>(userData);
  return token != nullptr && token->isCancelled() ? 1 : 0;
}

void ensureCurlInitialized() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) throw std::runtime_error("curl_global_init failed");
  });
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
            (firstComponent != relativeToRoot.end() &&
             *firstComponent == std::filesystem::path{".."})) {
          throw std::runtime_error("file tile URI escapes its source root");
        }
        auto bytes = readFile(path);
        ensureActive(cancellation);
        TilePayload payload{};
        payload.key = request.key;
        payload.bytes = std::move(bytes);
        payload.fromCache = false;
        return payload;
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
        TilePayload payload{};
        payload.key = request.key;
        payload.etag = found->second.etag;
        payload.notModified = !request.knownEtag.empty() && request.knownEtag == found->second.etag;
        if (!payload.notModified) payload.bytes = found->second.bytes;
        payload.fromCache = true;
        return payload;
      });
}

HttpTileSource::HttpTileSource(std::shared_ptr<HttpTransport> transport)
    : transport{std::move(transport)} {
  if (!this->transport) throw std::invalid_argument("HTTP tile source requires a transport");
}

std::future<TilePayload> HttpTileSource::request(
    TileRequest request,
    std::shared_ptr<CancellationToken> cancellation) {
  return std::async(
      std::launch::async,
      [transport = transport,
       request = std::move(request),
       cancellation = std::move(cancellation)] {
        ensureActive(cancellation);
        auto token = cancellation ? cancellation : std::make_shared<CancellationToken>();
        const HttpResponse response = transport->get(request.uri, request.knownEtag, *token);
        ensureActive(token);
        if (response.status == 304) {
          TilePayload payload{};
          payload.key = request.key;
          payload.etag = response.etag.empty() ? request.knownEtag : response.etag;
          payload.notModified = true;
          return payload;
        }
        if (response.status < 200 || response.status >= 300) {
          throw std::runtime_error(
              "HTTP tile request failed with status " + std::to_string(response.status) +
              ": " + request.uri);
        }
        TilePayload payload{};
        payload.key = request.key;
        payload.bytes = response.body;
        payload.etag = response.etag;
        return payload;
      });
}

CurlHttpTransport::CurlHttpTransport(std::chrono::milliseconds timeout)
    : timeout{timeout} {
  if (timeout.count() <= 0) throw std::invalid_argument("HTTP timeout must be positive");
  ensureCurlInitialized();
}

HttpResponse CurlHttpTransport::get(
    const std::string& uri,
    const std::string& knownEtag,
    const CancellationToken& cancellation) {
  return perform(uri, "GET", {}, knownEtag, cancellation);
}

HttpResponse CurlHttpTransport::postJson(
    const std::string& uri,
    const std::string& json,
    const CancellationToken& cancellation) {
  return perform(uri, "POST", json, {}, cancellation);
}

HttpResponse CurlHttpTransport::perform(
    const std::string& uri,
    const char* method,
    const std::string& requestBody,
    const std::string& knownEtag,
    const CancellationToken& cancellation) {
  if (cancellation.isCancelled()) throw std::runtime_error("HTTP request cancelled");
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl_easy_init failed");

  HttpResponse response{};
  curl_slist* headers = nullptr;
  if (!knownEtag.empty()) {
    headers = curl_slist_append(headers, ("If-None-Match: " + knownEtag).c_str());
  }
  if (std::string_view{method} == "POST") {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }

  curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendCurlBytes);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, captureCurlHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.etag);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancellation);
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (std::string_view{method} == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody.size()));
  }

  const CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  response.status = static_cast<uint32_t>(std::max(0L, status));

  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (result == CURLE_ABORTED_BY_CALLBACK && cancellation.isCancelled()) {
    throw std::runtime_error("HTTP request cancelled");
  }
  if (result != CURLE_OK) {
    throw std::runtime_error(
        "HTTP transport failed for " + uri + ": " + curl_easy_strerror(result));
  }
  return response;
}

}  // namespace vulkax::atlas
