foreach(required ALOG2MEDIA FIXTURE_MAP FFMPEG FFPROBE FIXTURE_DIR WORK_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "render_smoke requires -D${required}=...")
  endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${FIXTURE_DIR}/basic.alog" DESTINATION "${WORK_DIR}")
file(COPY "${FIXTURE_DIR}/basic.info" DESTINATION "${WORK_DIR}")

execute_process(
  COMMAND "${FIXTURE_MAP}" "${WORK_DIR}/basic.tif"
  RESULT_VARIABLE map_result
  ERROR_VARIABLE map_error)
if(NOT map_result EQUAL 0)
  message(FATAL_ERROR "fixture map generation failed: ${map_error}")
endif()
file(COPY_FILE "${WORK_DIR}/basic.tif" "${WORK_DIR}/basic.tiff")

function(run_render map_suffix output_suffix output_name)
  execute_process(
    COMMAND "${ALOG2MEDIA}" "${WORK_DIR}/basic.alog"
      --map "${WORK_DIR}/basic.${map_suffix}"
      --view fit
      --start 0
      --duration 1.5
      --fps 4
      --size 320x180
      --output "${WORK_DIR}/${output_name}.${output_suffix}"
      --force
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_output
    ERROR_VARIABLE render_error)
  if(NOT render_result EQUAL 0)
    message(FATAL_ERROR
      "${map_suffix}/${output_suffix} render failed:\n${render_output}\n${render_error}")
  endif()
endfunction()

run_render(tif mp4 map-tif)
run_render(tiff mp4 map-tiff)
run_render(tiff gif animation)

execute_process(
  COMMAND "${ALOG2MEDIA}" "${WORK_DIR}/basic.alog"
    --map "${WORK_DIR}/basic.tif"
    --view fit
    --at 0.5
    --size 319x179
    --output "${WORK_DIR}/snapshot.png"
    --force
  RESULT_VARIABLE png_result
  OUTPUT_VARIABLE png_output
  ERROR_VARIABLE png_error)
if(NOT png_result EQUAL 0)
  message(FATAL_ERROR "PNG snapshot render failed:\n${png_output}\n${png_error}")
endif()

execute_process(
  COMMAND "${FFMPEG}" -v error -i "${WORK_DIR}/map-tif.mp4" -f framemd5 -
  RESULT_VARIABLE tif_decode_result
  OUTPUT_VARIABLE tif_frames
  ERROR_VARIABLE tif_decode_error)
execute_process(
  COMMAND "${FFMPEG}" -v error -i "${WORK_DIR}/map-tiff.mp4" -f framemd5 -
  RESULT_VARIABLE tiff_decode_result
  OUTPUT_VARIABLE tiff_frames
  ERROR_VARIABLE tiff_decode_error)
if(NOT tif_decode_result EQUAL 0 OR NOT tiff_decode_result EQUAL 0)
  message(FATAL_ERROR
    "MP4 frame decode failed: ${tif_decode_error}${tiff_decode_error}")
endif()
if(NOT tif_frames STREQUAL tiff_frames)
  message(FATAL_ERROR ".tif and .tiff renders produced different decoded frames")
endif()

execute_process(
  COMMAND "${FFPROBE}" -v error -select_streams v:0
    -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames
    -of default=noprint_wrappers=1 "${WORK_DIR}/animation.gif"
  RESULT_VARIABLE probe_result
  OUTPUT_VARIABLE probe_output
  ERROR_VARIABLE probe_error)
if(NOT probe_result EQUAL 0)
  message(FATAL_ERROR "GIF probe failed: ${probe_error}")
endif()
foreach(expected "codec_name=gif" "width=320" "height=180"
                 "r_frame_rate=4/1" "nb_frames=6")
  string(FIND "${probe_output}" "${expected}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "GIF metadata lacks '${expected}':\n${probe_output}")
  endif()
endforeach()

execute_process(
  COMMAND "${FFPROBE}" -v error -select_streams v:0
    -show_entries stream=codec_name,pix_fmt,width,height,nb_frames
    -of default=noprint_wrappers=1 "${WORK_DIR}/snapshot.png"
  RESULT_VARIABLE png_probe_result
  OUTPUT_VARIABLE png_probe_output
  ERROR_VARIABLE png_probe_error)
if(NOT png_probe_result EQUAL 0)
  message(FATAL_ERROR "PNG probe failed: ${png_probe_error}")
endif()
foreach(expected "codec_name=png" "pix_fmt=rgb24" "width=319" "height=179")
  string(FIND "${png_probe_output}" "${expected}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "PNG metadata lacks '${expected}':\n${png_probe_output}")
  endif()
endforeach()
