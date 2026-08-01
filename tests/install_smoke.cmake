if(NOT DEFINED BUILD_DIR OR NOT DEFINED INSTALL_PREFIX OR
   NOT DEFINED INSTALL_BINDIR)
  message(FATAL_ERROR "install smoke test requires build and install paths")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${INSTALL_PREFIX}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_stdout
  ERROR_VARIABLE install_stderr)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "install failed (${install_result})\n${install_stdout}\n${install_stderr}")
endif()

execute_process(
  COMMAND "${INSTALL_PREFIX}/${INSTALL_BINDIR}/alog2media" --version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_stdout
  ERROR_VARIABLE version_stderr)
if(NOT version_result EQUAL 0)
  message(FATAL_ERROR
    "installed executable failed (${version_result})\n"
    "${version_stdout}\n${version_stderr}")
endif()
if(NOT version_stdout MATCHES "alog2media")
  message(FATAL_ERROR
    "installed executable returned unexpected version output: ${version_stdout}")
endif()
