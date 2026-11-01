# Prebuilt ONNX Runtime (CPU) for policy inference (inference-and-replay.md, "the prebuilt fallback"): Microsoft's
# release archive, downloaded once into <build>/_deps and pinned by its hash. vcpkg's onnxruntime port builds from
# source. Defines onnxruntime::onnxruntime and ONNXRUNTIME_DLLS (to copy next to the executables).
set(ORT_VERSION 1.23.2)
set(ORT_NAME onnxruntime-win-x64-${ORT_VERSION})
set(ORT_SHA256 0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd)
set(ORT_DIR ${CMAKE_BINARY_DIR}/_deps/${ORT_NAME})

if (NOT EXISTS ${ORT_DIR}/lib/onnxruntime.dll)
    set(ORT_ZIP ${CMAKE_BINARY_DIR}/_deps/${ORT_NAME}.zip)
    file(DOWNLOAD https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_NAME}.zip ${ORT_ZIP}
        EXPECTED_HASH SHA256=${ORT_SHA256} STATUS ORT_STATUS)
    list(GET ORT_STATUS 0 ORT_CODE)
    if (NOT ORT_CODE EQUAL 0)
        message(FATAL_ERROR "ONNX Runtime download failed: ${ORT_STATUS} (or configure with -DDYNAMICAL_ONNX=OFF)")
    endif()
    file(ARCHIVE_EXTRACT INPUT ${ORT_ZIP} DESTINATION ${CMAKE_BINARY_DIR}/_deps PATTERNS ${ORT_NAME}/include ${ORT_NAME}/lib/*.dll ${ORT_NAME}/lib/*.lib ${ORT_NAME}/LICENSE)
endif()

add_library(onnxruntime::onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ORT_DIR}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${ORT_DIR}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${ORT_DIR}/include)
set(ONNXRUNTIME_DLLS ${ORT_DIR}/lib/onnxruntime.dll ${ORT_DIR}/lib/onnxruntime_providers_shared.dll)
