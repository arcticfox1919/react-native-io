# Hash library - lightweight cryptographic hash functions
# Based on hash-library by Stephan Brumme
# http://create.stephan-brumme.com/hash-library/

set(CRYPTO_SOURCES
    md5.cpp
    sha1.cpp
    sha256.cpp
    sha3.cpp
    keccak.cpp
    crc32.cpp
)

set(CRYPTO_HEADERS
    hash.h
    hmac.h
    md5.h
    sha1.h
    sha256.h
    sha3.h
    keccak.h
    crc32.h
)

# Add as a static library
add_library(io_hash STATIC ${CRYPTO_SOURCES} ${CRYPTO_HEADERS})

target_include_directories(io_hash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
