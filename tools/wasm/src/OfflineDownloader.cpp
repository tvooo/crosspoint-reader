#include <Logging.h>

#include "network/HttpDownloader.h"

namespace {
bool unavailable(int* statusOut = nullptr) {
  LOG_ERR("PREVIEW", "Network downloads are unavailable in the browser preview");
  if (statusOut) *statusOut = 0;
  return false;
}
}  // namespace

int HttpDownloader::lastStatus() { return 0; }

bool HttpDownloader::lastAnswerRedirected() { return false; }

bool HttpDownloader::fetchUrl(const std::string&, Stream&, const std::string&, const std::string&,
                              const std::vector<Header>&) {
  return unavailable();
}

bool HttpDownloader::fetchUrl(const std::string&, const DataCallback&, const std::string&, const std::string&,
                              const std::vector<Header>&) {
  return unavailable();
}

bool HttpDownloader::fetchUrl(const std::string&, const DataCallback&, const FetchOptions& options) {
  return unavailable(options.statusOut);
}

bool HttpDownloader::postForm(const std::string&, const std::string&, std::string&, int* statusOut) {
  return unavailable(statusOut);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string&, const std::string&, ProgressCallback,
                                                             const bool* cancelFlag, const std::string&,
                                                             const std::string&, const std::vector<Header>&, bool) {
  unavailable();
  return cancelFlag && *cancelFlag ? ABORTED : HTTP_ERROR;
}

HttpDownloader::DownloadError HttpDownloader::postToFile(const std::string&, const std::string&, const std::string&,
                                                         const std::string&, ProgressCallback,
                                                         const bool* cancelFlag) {
  unavailable();
  return cancelFlag && *cancelFlag ? ABORTED : HTTP_ERROR;
}
