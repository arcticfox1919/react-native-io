#include "IOHttpClient.hpp"
#include "AtomicFileWriter.hpp"
#include "IOLimits.hpp"
#include <RemoteCommunicationKit/rcp.h>
#include <cctype>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>

namespace rct_io::network {
namespace {

void requireSuccess(uint32_t error, const char* operation) {
  if (error != 0) throw std::runtime_error(std::string(operation) + ": RCP error " + std::to_string(error));
}

struct ResponseDeleter {
  void operator()(Rcp_Response* response) const noexcept {
    if (response) response->destroyResponse(response);
  }
};
using ResponsePtr = std::unique_ptr<Rcp_Response, ResponseDeleter>;

class Transfer {
 public:
  Rcp_Request* request = nullptr;
  Rcp_Configuration configuration{};
  Rcp_RequestContent content{};
  Rcp_MultipartForm* multipart = nullptr;

  Transfer(const std::string& url, int32_t timeout, bool redirects) {
    checkPath(url);
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
      throw std::invalid_argument("Only HTTP and HTTPS URLs are supported");
    if (timeout <= 0) throw std::invalid_argument("HTTP timeout must be positive");
    request = HMS_Rcp_CreateRequest(url.c_str());
    if (!request) throw std::runtime_error("Cannot create HarmonyOS HTTP request");
    try {
      // Request members are borrowed by RCP and remain alive until FetchSync returns.
      headers_ = HMS_Rcp_CreateHeaders();
      if (!headers_) throw std::bad_alloc();
      request->headers = headers_;
      configuration.transferConfiguration.autoRedirect = redirects;
      configuration.transferConfiguration.timeout = {
        static_cast<uint32_t>(timeout), static_cast<uint32_t>(timeout)};
      configuration.proxyConfiguration.proxyType = RCP_PROXY_SYSTEM;
      configuration.securityConfiguration.remoteValidationType = RCP_REMOTE_VALIDATION_SYSTEM;
      request->configuration = &configuration;
      uint32_t error = 0;
      session_ = HMS_Rcp_CreateSession(nullptr, &error);
      requireSuccess(error, "Create HTTP session");
      if (!session_) throw std::runtime_error("Cannot create HarmonyOS HTTP session");
    } catch (...) {
      release();
      throw;
    }
  }

  ~Transfer() { release(); }
  Transfer(const Transfer&) = delete;
  Transfer& operator=(const Transfer&) = delete;

  void setHeaders(const HttpHeaders& values) {
    for (const auto& [key, value] : values) {
      requireSuccess(HMS_Rcp_SetHeaderValue(headers_, key.c_str(), value.c_str()), "Set HTTP header");
    }
  }

  ResponsePtr fetch() {
    uint32_t error = 0;
    ResponsePtr response(HMS_Rcp_FetchSync(session_, request, &error));
    requireSuccess(error, "HTTP transfer");
    if (!response) throw std::runtime_error("HarmonyOS HTTP returned no response");
    return response;
  }

 private:
  Rcp_Session* session_ = nullptr;
  Rcp_Headers* headers_ = nullptr;

  void release() noexcept {
    if (session_) HMS_Rcp_CloseSession(&session_);
    if (request) {
      // These values are owned here, not by DestroyRequest.
      request->headers = nullptr;
      request->configuration = nullptr;
      request->content = nullptr;
      HMS_Rcp_DestroyRequest(request);
      request = nullptr;
    }
    if (headers_) { HMS_Rcp_DestroyHeaders(headers_); headers_ = nullptr; }
    if (multipart) { HMS_Rcp_DestroyMultipartForm(multipart); multipart = nullptr; }
  }
};

class DownloadFile {
 public:
  explicit DownloadFile(const std::string& path)
      : parent_(openParent(path)) {
    name_ = std::filesystem::path(path).filename().string();
    if (name_.empty() || name_ == "." || name_ == "..")
      throw std::invalid_argument("Invalid download destination");
    struct stat existing{};
    const bool exists = ::fstatat(parent_.get(), name_.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0;
    if (!exists && errno != ENOENT)
      throw std::system_error(errno, std::generic_category(), "Inspect download destination");
    if (exists && !S_ISREG(existing.st_mode))
      throw std::invalid_argument("Download destination must be a regular file");
    static std::atomic<uint64_t> next{0};
    int descriptor = -1;
    for (int attempt = 0; attempt < 128; ++attempt) {
      stage_ = ".rct-io-download-" + std::to_string(::getpid()) + "-" + std::to_string(++next);
      descriptor = ::openat(parent_.get(), stage_.c_str(),
          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (descriptor >= 0 || errno != EEXIST) break;
    }
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "Create download staging file");
    // UniqueFD is intentionally nonassignable; keep the created descriptor separately.
    descriptor_ = descriptor;
    if (exists && ::fchmod(descriptor_, existing.st_mode & 0777) != 0) {
      const int error = errno;
      ::close(descriptor_);
      descriptor_ = -1;
      ::unlinkat(parent_.get(), stage_.c_str(), 0);
      throw std::system_error(error, std::generic_category(), "Preserve download permissions");
    }
  }

  ~DownloadFile() {
    if (descriptor_ >= 0) ::close(descriptor_);
    if (!committed_) ::unlinkat(parent_.get(), stage_.c_str(), 0);
  }

  DownloadFile(const DownloadFile&) = delete;
  DownloadFile& operator=(const DownloadFile&) = delete;

  void write(const char* data, size_t length) {
    if (length > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - size_))
      throw std::length_error("Downloaded file exceeds supported size");
    size_t position = 0;
    while (position < length) {
      const auto written = ::write(descriptor_, data + position, std::min<size_t>(length - position, 1024 * 1024));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) throw std::system_error(written == 0 ? EIO : errno, std::generic_category(), "Write download");
      position += static_cast<size_t>(written);
    }
    size_ += static_cast<int64_t>(length);
  }

  int64_t commit() {
    const int descriptor = descriptor_;
    descriptor_ = -1; // A close error must not accidentally close a reused fd.
    if (::close(descriptor) != 0)
      throw std::system_error(errno, std::generic_category(), "Close download");
    if (::renameat(parent_.get(), stage_.c_str(), parent_.get(), name_.c_str()) != 0)
      throw std::system_error(errno, std::generic_category(), "Commit download");
    committed_ = true;
    return size_;
  }

 private:
  detail::UniqueFD parent_;
  int descriptor_ = -1;
  std::string name_;
  std::string stage_;
  int64_t size_ = 0;
  bool committed_ = false;

  static int openParent(const std::string& path) {
    checkPath(path);
    auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) parent = ".";
    std::filesystem::create_directories(parent);
    const int descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "Open download directory");
    return descriptor;
  }
};

// RCP calls these callbacks outside JSI. Never access the JS runtime or throw
// across the C ABI; returning a short length aborts the transfer.
struct Sink {
  std::mutex mutex;
  std::vector<uint8_t> bytes;
  DownloadFile* file = nullptr;
  std::exception_ptr failure;

  static size_t receive(void* object, Rcp_Buffer* buffer) noexcept {
    auto& sink = *static_cast<Sink*>(object);
    try {
      std::lock_guard lock(sink.mutex);
      if (sink.failure) return 0;
      if (!buffer || (!buffer->buffer && buffer->length != 0))
        throw std::runtime_error("Invalid RCP receive buffer");
      const size_t length = buffer->length;
      if (length == 0) return 0;
      if (sink.file) sink.file->write(buffer->buffer, length);
      else {
        if (length > maxBufferBytes - sink.bytes.size())
          throw std::length_error("HTTP response exceeds I/O buffer limit; use download()");
        const auto required = sink.bytes.size() + length;
        if (required > sink.bytes.capacity()) {
          const auto capacity = sink.bytes.capacity();
          // Bound capacity as well as logical size; vector's automatic growth
          // may otherwise allocate above the configured native buffer limit.
          const auto growth = capacity + std::min(capacity, maxBufferBytes - capacity);
          sink.bytes.reserve(std::max(required, growth));
        }
        const auto* first = reinterpret_cast<const uint8_t*>(buffer->buffer);
        sink.bytes.insert(sink.bytes.end(), first, first + length);
      }
      return length;
    } catch (...) {
      // Only one receive callback per request runs at a time in RCP. The mutex
      // also protects against future implementations dispatching concurrently.
      std::lock_guard lock(sink.mutex);
      if (!sink.failure) sink.failure = std::current_exception();
      return 0;
    }
  }

  void attach(Transfer& transfer) {
    requireSuccess(HMS_Rcp_SetRequestOnBinaryDataRecvCallback(transfer.request, {receive, this}), "Set binary receiver");
  }

  void check() {
    std::lock_guard lock(mutex);
    if (failure) std::rethrow_exception(failure);
  }
};

HttpHeaders responseHeaders(Rcp_Headers* headers) {
  HttpHeaders result;
  auto* entries = headers ? HMS_Rcp_GetHeaderEntries(headers) : nullptr;
  struct Cleanup {
    Rcp_HeaderEntry* entries;
    ~Cleanup() { if (entries) HMS_Rcp_DestroyHeaderEntries(entries); }
  } cleanup{entries};
  for (auto* entry = entries; entry; entry = entry->next) {
    if (entry->key && entry->value && entry->value->value)
      result[entry->key] = entry->value->value;
  }
  return result;
}

template <size_t Size>
void fixedString(char (&destination)[Size], const std::string& value, const char* field) {
  if (value.size() >= Size || value.find('\0') != std::string::npos)
    throw std::invalid_argument(std::string(field) + " exceeds the HarmonyOS RCP field limit");
  std::memcpy(destination, value.data(), value.size());
  destination[value.size()] = '\0';
}

class IOHttpClientHarmony final : public IOHttpClient {
 public:
  HttpResponse request(const HttpRequestConfig& config) override {
    HttpResponse result;
    result.url = config.url;
    try {
      Sink sink;
      Transfer transfer(config.url, config.timeoutMs, config.followRedirects);
      const auto method = httpMethodToString(config.method);
      transfer.request->method = method.c_str();
      transfer.setHeaders(config.headers);
      if (config.hasBody()) {
        const auto length = config.body.empty() ? config.bodyString.size() : config.body.size();
        checkBufferSize(length);
        if (length > UINT32_MAX) throw std::length_error("Request body exceeds RCP limit");
        transfer.content.type = RCP_CONTENT_TYPE_STRING;
        transfer.content.data.contentStr = {
          config.body.empty() ? config.bodyString.data() : reinterpret_cast<const char*>(config.body.data()),
          static_cast<uint32_t>(length)};
        transfer.request->content = &transfer.content;
      }
      sink.attach(transfer);
      auto response = transfer.fetch();
      sink.check();
      result.statusCode = static_cast<int32_t>(response->statusCode);
      result.headers = responseHeaders(response->headers);
      if (response->effectiveUrl) result.url = response->effectiveUrl;
      result.body = std::move(sink.bytes);
      result.success = true; // The JS Response wrapper applies the 2xx check.
    } catch (const std::exception& error) { result.errorMessage = error.what(); }
    return result;
  }

  DownloadResult download(const DownloadConfig& config, DownloadProgressCallback /*progress*/) override {
    DownloadResult result;
    result.filePath = config.destinationPath;
    try {
      // Restart safely until validator/range handling has device coverage.
      // Never append unvalidated bytes to an existing file.
      DownloadFile file(config.destinationPath);
      Sink sink;
      sink.file = &file;
      Transfer transfer(config.url, config.timeoutMs, true);
      HttpHeaders headers;
      for (const auto& [key, value] : config.headers) {
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower != "range" && lower != "if-range") headers.emplace(key, value);
      }
      transfer.setHeaders(headers);
      sink.attach(transfer);
      auto response = transfer.fetch();
      sink.check();
      result.statusCode = static_cast<int32_t>(response->statusCode);
      if (result.statusCode < 200 || result.statusCode >= 300 || result.statusCode == 206)
        throw std::runtime_error("Download requires a complete successful response; HTTP " + std::to_string(result.statusCode));
      result.fileSize = file.commit();
      result.success = true;
    } catch (const std::exception& error) { result.errorMessage = error.what(); }
    return result;
  }

  UploadResult upload(const UploadConfig& config, UploadProgressCallback /*progress*/) override {
    UploadResult result;
    try {
      checkPath(config.filePath);
      if (!std::filesystem::is_regular_file(config.filePath))
        throw std::invalid_argument("Upload source must be a regular file");
      Sink sink;
      // Keep all multipart records alive until the native synchronous transfer
      // finishes, whether RCP copies their values or borrows their addresses.
      std::vector<Rcp_MultipartFormFieldValue> fields(config.formFields.size() + 1);
      Transfer transfer(config.url, config.timeoutMs, false);
      transfer.request->method = RCP_METHOD_POST;
      transfer.setHeaders(config.headers);
      transfer.multipart = HMS_Rcp_CreateMultipartForm();
      if (!transfer.multipart) throw std::bad_alloc();
      auto& file = fields[0];
      file.type = RCP_TYPE_FORM_FIELD_FILE_VALUE;
      auto& value = file.data.formFileValue;
      fixedString(value.remoteFileName, config.fileName.empty() ? std::filesystem::path(config.filePath).filename().string() : config.fileName, "Upload filename");
      fixedString(value.contentType, config.mimeType.empty() ? "application/octet-stream" : config.mimeType, "Upload MIME type");
      value.contentOrPathOrCb.type = RCP_FILE_VALUE_TYPE_PATH;
      fixedString(value.contentOrPathOrCb.data.path, config.filePath, "Upload path");
      requireSuccess(HMS_Rcp_SetMultipartFormValue(transfer.multipart, config.fieldName.c_str(), &file), "Set upload file");
      size_t index = 1;
      for (const auto& [key, text] : config.formFields) {
        checkBufferSize(text.size());
        if (text.size() > UINT32_MAX) throw std::length_error("Form value exceeds RCP limit");
        auto& field = fields[index++];
        field.type = RCP_TYPE_FORM_FIELD_VALUE;
        field.data.formValue.type = RCP_FORM_VALUE_TYPE_STRING;
        field.data.formValue.data.varStr = {text.data(), static_cast<uint32_t>(text.size())};
        requireSuccess(HMS_Rcp_SetMultipartFormValue(transfer.multipart, key.c_str(), &field), "Set upload field");
      }
      transfer.content.type = RCP_CONTENT_TYPE_MULTIPARTFORM;
      transfer.content.data.multipartForm = transfer.multipart;
      transfer.request->content = &transfer.content;
      sink.attach(transfer);
      auto response = transfer.fetch();
      sink.check();
      result.statusCode = static_cast<int32_t>(response->statusCode);
      result.responseBody = std::move(sink.bytes);
      result.success = result.statusCode >= 200 && result.statusCode < 300;
      if (!result.success) result.errorMessage = "HTTP " + std::to_string(result.statusCode);
    } catch (const std::exception& error) { result.errorMessage = error.what(); }
    return result;
  }
};

} // namespace

std::shared_ptr<IOHttpClient> IOHttpClient::create() {
  return std::make_shared<IOHttpClientHarmony>();
}

} // namespace rct_io::network
