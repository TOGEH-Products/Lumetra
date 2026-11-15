#include "profiles/ProfileStorage.h"

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void TestProfileStorageFileSizeLimit() {
    using prismshift::IsProfileStorageFileSizeAllowed;
    using prismshift::kMaxProfileStorageFileSize;

    Check(!IsProfileStorageFileSizeAllowed(0), "empty profile files are rejected");
    Check(IsProfileStorageFileSizeAllowed(1), "non-empty profile files are accepted");
    Check(IsProfileStorageFileSizeAllowed(kMaxProfileStorageFileSize - 1),
          "files below the size limit are accepted");
    Check(IsProfileStorageFileSizeAllowed(kMaxProfileStorageFileSize),
          "a file exactly at the 4 MiB limit is accepted");
    Check(!IsProfileStorageFileSizeAllowed(kMaxProfileStorageFileSize + 1),
          "files larger than the 4 MiB limit are rejected");
}

} // namespace

int main() {
    TestProfileStorageFileSizeLimit();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All profile storage tests passed\n";
    return 0;
}
