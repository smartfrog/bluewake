set(WW_DIAGNOSTIC_TLS "${RECOMPCORE_DIR}/Externals/mbedtls")
add_library(ww_diagnostic_io STATIC ../diagnostic_io.c
    "${WW_DIAGNOSTIC_TLS}/library/sha256.c"
    "${WW_DIAGNOSTIC_TLS}/library/platform_util.c")
target_include_directories(ww_diagnostic_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${WW_DIAGNOSTIC_TLS}/include")
target_compile_definitions(ww_diagnostic_io PRIVATE MBEDTLS_CONFIG_FILE="sha256_config.h")
target_compile_options(ww_diagnostic_io PRIVATE -O2 -Wall -Wextra)
