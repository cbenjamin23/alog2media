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

# Model a repository-local custom map whose path is retained only by the
# mission. pMarineViewer logs the active map as a basename in REGION_INFO.
file(MAKE_DIRECTORY "${WORK_DIR}/custom-mission/logs"
                    "${WORK_DIR}/custom-mission/maps")
file(COPY_FILE "${WORK_DIR}/basic.tif"
     "${WORK_DIR}/custom-mission/maps/custom.tif")
file(COPY_FILE "${WORK_DIR}/basic.info"
     "${WORK_DIR}/custom-mission/maps/custom.info")
file(WRITE "${WORK_DIR}/custom-mission/viewer.moos" [=[
LatOrigin = 42.0
LongOrigin = -71.0
ProcessConfig = pMarineViewer
{
  tiff_file = maps/custom.tif
  zoom = 1
  set_pan_x = 0
  set_pan_y = 0
}
]=])
file(WRITE "${WORK_DIR}/custom-mission/logs/custom.alog" [=[
%%%% LOGSTART 1000.0
0.00000 DB_TIME MOOSDB_shoreside 1000.0
0.00001 REGION_INFO pMarineViewer lat_datum=42.0,lon_datum=-71.0,img_file=custom.tif,zoom=1,pan_x=0,pan_y=0
0.00002 NODE_REPORT_LOCAL pNodeReporter NAME=alpha,TYPE=kayak,COLOR=yellow,LENGTH=4
0.00003 NAV_X uSimMarine 0
0.00004 NAV_Y uSimMarine 0
0.00005 NAV_HEADING uSimMarine 90
1.00000 APPCAST pMarineViewer end
]=])

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

# With no positional input, discover a regular .alog regardless of filename
# prefix or extension case. Options may precede the discovered input.
file(MAKE_DIRECTORY "${WORK_DIR}/automatic-input/arbitrary-directory")
file(COPY_FILE "${WORK_DIR}/basic.alog"
     "${WORK_DIR}/automatic-input/arbitrary-directory/latest.ALOG")
execute_process(
  COMMAND "${ALOG2MEDIA}"
    --map "${WORK_DIR}/basic.tif"
    --view fit
    --at 0.5
    --size 320x180
    --output "${WORK_DIR}/automatic-input.png"
    --force
  WORKING_DIRECTORY "${WORK_DIR}/automatic-input"
  RESULT_VARIABLE automatic_result
  OUTPUT_VARIABLE automatic_output
  ERROR_VARIABLE automatic_error)
if(NOT automatic_result EQUAL 0)
  message(FATAL_ERROR
    "automatic input render failed:\n${automatic_output}\n${automatic_error}")
endif()
string(FIND "${automatic_output}" "Using latest log:" selected_log_at)
if(selected_log_at EQUAL -1)
  message(FATAL_ERROR
    "automatic input selection was not reported:\n${automatic_output}")
endif()

execute_process(
  COMMAND "${ALOG2MEDIA}"
    "${WORK_DIR}/custom-mission/logs/custom.alog"
    --at 0.5
    --size 320x180
    --output "${WORK_DIR}/custom-map.png"
    --force
  RESULT_VARIABLE custom_result
  OUTPUT_VARIABLE custom_output
  ERROR_VARIABLE custom_error)
if(NOT custom_result EQUAL 0)
  message(FATAL_ERROR
    "automatic custom-map render failed:\n${custom_output}\n${custom_error}")
endif()
string(FIND "${custom_output}"
  "custom-mission/maps/custom.tif" custom_map_path)
if(custom_map_path EQUAL -1)
  message(FATAL_ERROR
    "custom map was not resolved through the mission:\n${custom_output}")
endif()

# A custom mission outside the bounded adjacent/parent discovery layout must
# fail with both ways to identify the missing context, rather than a vague map
# loader error.
file(MAKE_DIRECTORY "${WORK_DIR}/orphan/logs")
file(COPY_FILE "${WORK_DIR}/custom-mission/logs/custom.alog"
     "${WORK_DIR}/orphan/logs/custom.alog")
execute_process(
  COMMAND "${ALOG2MEDIA}" "${WORK_DIR}/orphan/logs/custom.alog"
    --at 0.5
    --output "${WORK_DIR}/orphan.png"
  RESULT_VARIABLE orphan_result
  OUTPUT_VARIABLE orphan_output
  ERROR_VARIABLE orphan_error)
if(orphan_result EQUAL 0)
  message(FATAL_ERROR "orphan custom-map render unexpectedly succeeded")
endif()
foreach(expected "--mission FILE.moos" "--map FILE.tif" "matching .info")
  string(FIND "${orphan_error}" "${expected}" diagnostic_at)
  if(diagnostic_at EQUAL -1)
    message(FATAL_ERROR
      "orphan diagnostic lacks '${expected}':\n${orphan_error}")
  endif()
endforeach()

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

foreach(map_suffix tif tiff)
  execute_process(
    COMMAND "${ALOG2MEDIA}" "${WORK_DIR}/basic.alog"
      --map "${WORK_DIR}/basic.${map_suffix}"
      --view fit
      --at 0.5
      --size 320x180
      --output "${WORK_DIR}/map-${map_suffix}.png"
      --force
    RESULT_VARIABLE alias_result
    OUTPUT_VARIABLE alias_output
    ERROR_VARIABLE alias_error)
  if(NOT alias_result EQUAL 0)
    message(FATAL_ERROR
      "${map_suffix} lossless render failed:\n${alias_output}\n${alias_error}")
  endif()
endforeach()

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

execute_process(
  COMMAND "${FFMPEG}" -v error -i "${WORK_DIR}/map-tif.png" -f framemd5 -
  RESULT_VARIABLE tif_alias_result
  OUTPUT_VARIABLE tif_alias_frame
  ERROR_VARIABLE tif_alias_error)
execute_process(
  COMMAND "${FFMPEG}" -v error -i "${WORK_DIR}/map-tiff.png" -f framemd5 -
  RESULT_VARIABLE tiff_alias_result
  OUTPUT_VARIABLE tiff_alias_frame
  ERROR_VARIABLE tiff_alias_error)
if(NOT tif_alias_result EQUAL 0 OR NOT tiff_alias_result EQUAL 0)
  message(FATAL_ERROR
    "lossless alias decode failed: ${tif_alias_error}${tiff_alias_error}")
endif()
if(NOT tif_alias_frame STREQUAL tiff_alias_frame)
  message(FATAL_ERROR ".tif and .tiff lossless renders produced different frames")
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
