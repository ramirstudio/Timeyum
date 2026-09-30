# Included from the root CMakeLists.txt when AE_SDK_DIR is set (Windows, MSVC).
#   cmake -S . -B build -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples"
#   cmake --build build --config Release
# AE_SDK_DIR is the SDK's "Examples" folder (it contains Headers, Util and Resources).
# Output: build/Release/Timeyum.aex

if(NOT MSVC)
  message(FATAL_ERROR "The After Effects plug-in builds with MSVC only")
endif()

set(AE_INCLUDES
  ${AE_SDK_DIR}/Headers
  ${AE_SDK_DIR}/Headers/SP
  ${AE_SDK_DIR}/Util
  ${AE_SDK_DIR}/Resources)

# PiPL: the SDK recipe is cl /EP -> PiPLTool -> cl /EP -> .rc. If PiPLTool produces an empty
# resource on your machine, generate the .rc once by hand and pass it as TIMEYUM_PIPL_RC.
if(TIMEYUM_PIPL_RC)
  set(PIPL_RC ${TIMEYUM_PIPL_RC})
else()
  set(PIPL_SRC ${CMAKE_CURRENT_LIST_DIR}/TimeyumPiPL.r)
  set(PIPL_RR ${CMAKE_CURRENT_BINARY_DIR}/TimeyumPiPL.rr)
  set(PIPL_RRC ${CMAKE_CURRENT_BINARY_DIR}/TimeyumPiPL.rrc)
  set(PIPL_RC ${CMAKE_CURRENT_BINARY_DIR}/TimeyumPiPL.rc)
  set(PIPL_INC /I "${AE_SDK_DIR}/Headers" /I "${CMAKE_CURRENT_LIST_DIR}" /I "${AE_SDK_DIR}/Resources")
  add_custom_command(
    OUTPUT ${PIPL_RC}
    COMMAND ${CMAKE_CXX_COMPILER} /nologo ${PIPL_INC} /EP ${PIPL_SRC} > ${PIPL_RR}
    COMMAND "${AE_SDK_DIR}/Resources/PiPLtool.exe" ${PIPL_RR} ${PIPL_RRC}
    COMMAND ${CMAKE_CXX_COMPILER} /nologo /D MSWindows /EP ${PIPL_RRC} > ${PIPL_RC}
    DEPENDS ${PIPL_SRC} ${CMAKE_CURRENT_LIST_DIR}/TimeyumFlags.h
    COMMENT "Generating the PiPL resource")
endif()

add_library(timeyum_ae MODULE
  ${CMAKE_CURRENT_LIST_DIR}/TimeyumAE.cpp
  ${AE_SDK_DIR}/Util/AEGP_SuiteHandler.cpp
  ${AE_SDK_DIR}/Util/MissingSuiteError.cpp
  ${PIPL_RC})
target_include_directories(timeyum_ae PRIVATE ${AE_INCLUDES} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(timeyum_ae PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(timeyum_ae PRIVATE timeyum_core)
set_target_properties(timeyum_ae PROPERTIES PREFIX "" OUTPUT_NAME "Timeyum" SUFFIX ".aex")
