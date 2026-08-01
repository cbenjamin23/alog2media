if(NOT DEFINED ALOG2MEDIA OR NOT DEFINED PMV_REFERENCE OR
   NOT DEFINED FIXTURE_MAP OR NOT DEFINED FFMPEG OR
   NOT DEFINED PYTHON OR NOT DEFINED FIXTURE_DIR OR
   NOT DEFINED PROOF_DIR OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "pmv_fidelity.cmake is missing a required -D argument")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy
          "${FIXTURE_DIR}/basic.alog" "${WORK_DIR}/basic.alog"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy
          "${FIXTURE_DIR}/basic.info" "${WORK_DIR}/basic.info"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${FIXTURE_MAP}" "${WORK_DIR}/basic.tif"
  COMMAND_ERROR_IS_FATAL ANY)

foreach(_time IN ITEMS 0.5 1.5)
  execute_process(
    COMMAND "${PMV_REFERENCE}"
            "${WORK_DIR}/basic.alog"
            "${WORK_DIR}/basic.tif"
            "${FIXTURE_DIR}/pmv_reference.moos"
            "${_time}" 640 360 "${WORK_DIR}/pmv-${_time}.ppm"
    COMMAND_ERROR_IS_FATAL ANY)
endforeach()

execute_process(
  COMMAND "${ALOG2MEDIA}"
          "${WORK_DIR}/basic.alog"
          --mission "${FIXTURE_DIR}/pmv_reference.moos"
          --map "${WORK_DIR}/basic.tif"
          --start 0.5 --duration 1.5 --fps 1 --size 640x360
          --trails off --grid off
          --output "${WORK_DIR}/alog2media.mp4" --force
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND "${PYTHON}" "${PROOF_DIR}/compare_pmv_reference.py"
          "${WORK_DIR}/alog2media.mp4"
          "${WORK_DIR}/pmv-0.5.ppm"
          "${WORK_DIR}/pmv-1.5.ppm"
          --width 640 --height 360
  WORKING_DIRECTORY "${PROOF_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
