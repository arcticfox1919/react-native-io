// Transport-contract tests using the real SDK declarations and a deterministic
// RCP test double. These do not exercise the HarmonyOS network service or TLS.
#include <RemoteCommunicationKit/rcp.h>
#include "IOHttpClient.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <atomic>
#include <string>
#include <unordered_map>
#include <sys/resource.h>
#include <csignal>

struct Rcp_Headers { std::map<std::string, std::string> values; };
struct Rcp_MultipartForm { std::map<std::string, const Rcp_MultipartFormFieldValue*> fields; };
struct Rcp_Session {};
static std::mutex callbackMutex;
static std::unordered_map<Rcp_Request*, Rcp_OnBinaryReceiveCallback> callbacks;
static std::atomic<int> requests{0}, sessions{0}, headers{0}, forms{0}, responses{0};

extern "C" {
Rcp_Request* HMS_Rcp_CreateRequest(const char* url) {
  auto* request = new Rcp_Request{};
  request->url = ::strdup(url);
  ++requests;
  return request;
}
void HMS_Rcp_DestroyRequest(Rcp_Request* request) {
  assert(!request->content && !request->configuration && !request->headers);
  { std::lock_guard lock(callbackMutex); callbacks.erase(request); }
  std::free(request->url);
  delete request;
  --requests;
}
Rcp_Headers* HMS_Rcp_CreateHeaders() { ++headers; return new Rcp_Headers; }
void HMS_Rcp_DestroyHeaders(Rcp_Headers* value) { delete value; --headers; }
uint32_t HMS_Rcp_SetHeaderValue(Rcp_Headers* value, const char* key, const char* text) {
  value->values[key] = text;
  return 0;
}
Rcp_HeaderEntry* HMS_Rcp_GetHeaderEntries(Rcp_Headers* headers) {
  Rcp_HeaderEntry* result = nullptr;
  for (const auto& [key, value] : headers->values) {
    auto* entry = new Rcp_HeaderEntry{};
    entry->key = ::strdup(key.c_str());
    entry->value = new Rcp_HeaderValue{::strdup(value.c_str()), nullptr};
    entry->next = result;
    result = entry;
  }
  return result;
}
void HMS_Rcp_DestroyHeaderEntries(Rcp_HeaderEntry* entry) {
  while (entry) {
    auto* next = entry->next;
    std::free(entry->key);
    std::free(entry->value->value);
    delete entry->value;
    delete entry;
    entry = next;
  }
}
Rcp_Session* HMS_Rcp_CreateSession(const Rcp_SessionConfiguration*, uint32_t* error) {
  *error = 0;
  ++sessions;
  return new Rcp_Session;
}
uint32_t HMS_Rcp_CloseSession(Rcp_Session** session) {
  delete *session;
  *session = nullptr;
  --sessions;
  return 0;
}
uint32_t HMS_Rcp_SetRequestOnBinaryDataRecvCallback(Rcp_Request* request, Rcp_OnBinaryReceiveCallback callback) {
  std::lock_guard lock(callbackMutex);
  callbacks[request] = callback;
  return 0;
}
Rcp_MultipartForm* HMS_Rcp_CreateMultipartForm() { ++forms; return new Rcp_MultipartForm; }
void HMS_Rcp_DestroyMultipartForm(Rcp_MultipartForm* value) { delete value; --forms; }
uint32_t HMS_Rcp_SetMultipartFormValue(Rcp_MultipartForm* value, const char* key,
    const Rcp_MultipartFormFieldValue* field) {
  value->fields[key] = field; // Deliberately borrow to detect lifetime mistakes under ASan.
  return 0;
}
Rcp_Response* HMS_Rcp_FetchSync(Rcp_Session*, Rcp_Request* request, uint32_t* error) {
  const auto& config = *request->configuration;
  assert(config.securityConfiguration.remoteValidationType == RCP_REMOTE_VALIDATION_SYSTEM);
  assert(config.proxyConfiguration.proxyType == RCP_PROXY_SYSTEM);
  assert(config.transferConfiguration.timeout.transferMs > 0);
  std::string path = request->url;
  std::string body("a\0b", 3);
  auto code = RCP_OK;
  if (path.ends_with("/error")) { code = RCP_BAD_REQUEST; body = "server error"; }
  if (path.ends_with("/partial")) code = RCP_PARTIAL;
  if (path.ends_with("/redirect") && !config.transferConfiguration.autoRedirect) code = static_cast<Rcp_StatusCode>(302);
  if (request->headers->values.contains("Range") || request->headers->values.contains("If-Range"))
    assert(path.ends_with("/echo"));
  if (path.ends_with("/echo")) {
    body.clear();
    if (request->content) {
      const auto& buffer = request->content->data.contentStr;
      body.assign(buffer.buffer, buffer.length);
    }
  }
  if (path.ends_with("/upload")) {
    assert(request->content->type == RCP_CONTENT_TYPE_MULTIPARTFORM);
    const auto& fields = request->content->data.multipartForm->fields;
    const auto& file = fields.at("file")->data.formFileValue;
    assert(file.contentOrPathOrCb.type == RCP_FILE_VALUE_TYPE_PATH);
    std::ifstream stream(file.contentOrPathOrCb.data.path, std::ios::binary);
    body.assign(std::istreambuf_iterator<char>(stream), {});
    assert(fields.at("caption")->data.formValue.data.varStr.length == 5);
  }
  if (path.ends_with("/oversize")) body.assign(65537, 'x');
  if (path.ends_with("/timeout")) { *error = 1007900028; return nullptr; }
  Rcp_OnBinaryReceiveCallback callback;
  { std::lock_guard lock(callbackMutex); callback = callbacks.at(request); }
  // Run the callback on a different thread, as the system may do.
  const auto received = std::async(std::launch::async, [&] {
    Rcp_Buffer buffer{body.data(), static_cast<uint32_t>(body.size())};
    return callback.callback(callback.usrObject, &buffer);
  }).get();
  if (received != body.size()) { *error = 1007900023; return nullptr; }
  if (path.ends_with("/truncated")) { *error = 1007900018; return nullptr; }
  *error = 0;
  auto* response = new Rcp_Response{};
  ++responses;
  response->statusCode = code;
  response->headers = HMS_Rcp_CreateHeaders();
  HMS_Rcp_SetHeaderValue(response->headers, "X-Fixture", "verified");
  response->effectiveUrl = request->url;
  response->destroyResponse = [](Rcp_Response* value) {
    HMS_Rcp_DestroyHeaders(value->headers);
    delete value;
    --responses;
  };
  return response;
}
}

using namespace rct_io::network;
static std::string readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  std::filesystem::create_directories(root);
  auto client = IOHttpClient::create();
  HttpRequestConfig request;
  request.url = "https://fixture/echo";
  request.method = HttpMethod::POST;
  request.body = {0, 1, 2, 255};
  const auto echoed = client->request(request);
  assert(echoed.body == request.body && echoed.headers.at("X-Fixture") == "verified");
  for (const size_t length : {0, 1, 65535, 65536, 65537}) {
    request.body.assign(length, 'x');
    const auto response = client->request(request);
    assert(response.success == (length <= 65536));
    if (response.success) assert(response.body == request.body);
  }
  request.body.clear();
  request.url = "https://fixture/error";
  auto error = client->request(request);
  assert(error.success && error.statusCode == 400);
  request.url = "https://fixture/redirect";
  request.followRedirects = false;
  assert(client->request(request).statusCode == 302);
  request.url = "https://fixture/timeout";
  assert(!client->request(request).success);
  request.url = "https://fixture/oversize";
  assert(!client->request(request).success);
  request.url = "file:///etc/passwd";
  assert(!client->request(request).success);

  const std::string destination = root + "/output";
  { std::ofstream file(destination); file << "original"; }
  DownloadConfig download;
  download.url = "https://fixture/error";
  download.destinationPath = destination;
  assert(!client->download(download).success);
  assert(readFile(destination) == "original");
  download.url = "https://fixture/truncated";
  assert(!client->download(download).success);
  assert(readFile(destination) == "original");
  // Force a real short write on the receive thread without filling the disk.
  struct rlimit saved{};
  assert(::getrlimit(RLIMIT_FSIZE, &saved) == 0);
  auto limited = saved;
  limited.rlim_cur = 64;
  auto previousSignal = std::signal(SIGXFSZ, SIG_IGN);
  assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
  download.url = "https://fixture/oversize";
  assert(!client->download(download).success);
  assert(::setrlimit(RLIMIT_FSIZE, &saved) == 0);
  std::signal(SIGXFSZ, previousSignal);
  assert(readFile(destination) == "original");
  // Streaming downloads are not limited to the in-memory response quota.
  assert(client->download(download).fileSize == 65537);
  download.url = "https://fixture/partial";
  assert(!client->download(download).success);
  assert(readFile(destination) == std::string(65537, 'x'));
  download.url = "https://fixture/payload";
  download.resumable = true;
  download.headers = {{"Range", "bytes=3-"}, {"If-Range", "etag"}};
  assert(client->download(download).success);
  assert(readFile(destination) == std::string("a\0b", 3));
  const auto link = root + "/link";
  std::filesystem::create_symlink(destination, link);
  download.destinationPath = link;
  assert(!client->download(download).success);
  assert(readFile(destination) == std::string("a\0b", 3));

  UploadConfig upload;
  upload.url = "https://fixture/upload";
  upload.filePath = destination;
  upload.formFields = {{"caption", "hello"}};
  const auto uploaded = client->upload(upload);
  assert(uploaded.success && uploaded.responseBody == std::vector<uint8_t>({'a', 0, 'b'}));
  upload.fileName.assign(128, 'x');
  assert(!client->upload(upload).success);
  upload.fileName.clear();
  upload.mimeType.assign(64, 'x');
  assert(!client->upload(upload).success);

  std::vector<std::future<void>> parallel;
  for (int index = 0; index < 32; ++index) parallel.push_back(std::async(std::launch::async, [client] {
    HttpRequestConfig config;
    config.url = "https://fixture/payload";
    assert(client->request(config).body == std::vector<uint8_t>({'a', 0, 'b'}));
  }));
  for (auto& task : parallel) task.get();
  assert(requests == 0 && sessions == 0 && headers == 0 && forms == 0 && responses == 0);
  for (const auto& entry : std::filesystem::directory_iterator(root))
    assert(!entry.path().filename().string().starts_with(".rct-io-download-"));
  std::cout << "Harmony HTTP contracts passed (SDK declarations, RCP test double).\n";
}
