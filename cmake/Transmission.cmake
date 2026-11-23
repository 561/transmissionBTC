include(ExternalProject)
include(ProcessorCount)

option(TR_SRC_DIR "Transmission sources" "${CMAKE_CURRENT_BINARY_DIR}/transmission/src")
set(TR_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/transmission/src/transmission-build")
set(TR_TP_BUILD_DIR "${TR_BUILD_DIR}/third-party")
string(REPLACE "-fno-exceptions" "-fexceptions" TR_CXX_FLAGS "${EXT_CXX_FLAGS}")
set(TR_CMAKE_ARGS -DENABLE_TESTS=OFF -DENABLE_DAEMON=OFF -DINSTALL_DOC=OFF -DENABLE_UTILS=OFF
        -DENABLE_CLI=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF -DENABLE_WEB=OFF
        -DINSTALL_WEB=OFF -DENABLE_NLS=OFF -DWITH_CRYPTO=openssl -DRUN_CLANG_TIDY=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DUSE_SYSTEM_B64=OFF -DUSE_SYSTEM_DEFLATE=OFF -DUSE_SYSTEM_DHT=OFF -DUSE_SYSTEM_MINIUPNPC=OFF
        -DUSE_SYSTEM_NATPMP=OFF -DUSE_SYSTEM_PSL=OFF -DUSE_SYSTEM_UTP=OFF -DUSE_SYSTEM_EVENT2=ON
        ${EXT_CMAKE_ARGS} "-DCMAKE_CXX_FLAGS=${TR_CXX_FLAGS}")

set(TR_LIBRARIES
        "${TR_BUILD_DIR}/libtransmission/libtransmission.a"
        "${TR_TP_BUILD_DIR}/dht.bld/pfx/lib/libdht.a"
        "${TR_TP_BUILD_DIR}/libb64.bld/src/libb64.a"
        "${TR_TP_BUILD_DIR}/libdeflate.bld/pfx/lib/libdeflate.a"
        "${TR_TP_BUILD_DIR}/libnatpmp.bld/pfx/lib/libnatpmp.a"
        "${TR_TP_BUILD_DIR}/libpsl.bld/pfx/lib/libpsl.a"
        "${TR_TP_BUILD_DIR}/libutp.bld/libutp.a"
        "${TR_TP_BUILD_DIR}/madler-crcany/libMadlerCrcany.a"
        "${TR_TP_BUILD_DIR}/miniupnp/miniupnpc.bld/pfx/lib/libminiupnpc.a"
        "${TR_TP_BUILD_DIR}/wildmat/libwildmat.a")

set(TR_INCLUDE_DIR
        "${TR_SRC_DIR}"
        "${TR_BUILD_DIR}"
        "${TR_SRC_DIR}/third-party/fmt/include"
        "${TR_SRC_DIR}/third-party/small/include"
        "${TR_SRC_DIR}/third-party/fast_float/include"
        "${TR_SRC_DIR}/third-party/utfcpp/source"
        "${TR_SRC_DIR}/third-party/wide-integer"
        "${TR_SRC_DIR}/third-party/rapidjson/include"
        "${TR_SRC_DIR}/third-party/libutp/include"
        "${TR_SRC_DIR}/third-party/libb64/include"
        "${TR_TP_BUILD_DIR}/dht.bld/pfx/include"
        "${TR_TP_BUILD_DIR}/libnatpmp.bld/pfx/include"
        "${TR_TP_BUILD_DIR}/miniupnp/miniupnpc.bld/pfx/include"
        "${TR_TP_BUILD_DIR}/libpsl.bld/pfx/include"
        "${TR_TP_BUILD_DIR}/libdeflate.bld/pfx/include")

set(TR_DEFINITIONS
        FMT_HEADER_ONLY=1 FMT_USE_EXCEPTIONS=0 SMALL_DISABLE_EXCEPTIONS=1
        WIDE_INTEGER_DISABLE_FLOAT_INTEROP WIDE_INTEGER_DISABLE_IOSTREAM
        WITH_UTP WITH_OPENSSL RAPIDJSON_HAS_STDSTRING=1)

ProcessorCount(NCPU)
find_program(MAKE_EXE NAMES make gmake nmake)

ExternalProject_Add(transmission
        GIT_REPOSITORY "https://github.com/transmission/transmission.git"
        GIT_TAG "4.1.4"
        SOURCE_DIR ${TR_SRC_DIR}
        PREFIX transmission
        GIT_SHALLOW 1
        GIT_PROGRESS 1
        GIT_SUBMODULES_RECURSE 1
        UPDATE_COMMAND ""
        PATCH_COMMAND sh -c "git apply --check '${CMAKE_CURRENT_SOURCE_DIR}/patches/transmission-engine.patch' 2>/dev/null && git apply '${CMAKE_CURRENT_SOURCE_DIR}/patches/transmission-engine.patch' || true"
        BUILD_ALWAYS 1
        CONFIGURE_COMMAND ${CMAKE_COMMAND} ${TR_SRC_DIR} ${TR_CMAKE_ARGS}
        BUILD_COMMAND ${MAKE_EXE} -j${NCPU} transmission
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${TR_LIBRARIES})

add_dependencies(transmission openssl curl libevent)
add_dependencies(${PROJECT_NAME} transmission)
