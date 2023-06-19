#include "experiment/include/blobstore.h"
#include "experiment/src/lib.rs.h"

#include <functional>
#include <memory>
#include <string>

BlobstoreClient::BlobstoreClient() = default;

std::unique_ptr<BlobstoreClient> new_blobstore_client() {
  return std::make_unique<BlobstoreClient>();
}

// Upload a new blob and return a blobid that serves as a handle to the blob.
uint64_t BlobstoreClient::put(MultiBuf &buf) const {
  // Traverse the caller's chunk iterator.
  std::string contents;
  while (true) {
    auto chunk = next_chunk(buf);
    if (chunk.size() == 0) {
      break;
    }
    contents.append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
  }

  // Pretend we did something useful to persist the data.
  auto blobid = std::hash<std::string>{}(contents);
  return blobid;
}
