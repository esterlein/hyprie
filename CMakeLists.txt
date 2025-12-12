cmake_minimum_required(VERSION 3.31 FATAL_ERROR)
project(hyprie LANGUAGES C CXX)

option(MTP_ENABLE_TRACE    "enable allocation trace"        ON)
option(MTP_CONTAINERS_BOTH "compile mtp and std containers" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_LINKER "lld")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "build type" FORCE)
endif()

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/*.glsl")

if(NOT DEFINED SHDC_COMMAND)
	set(SHDC_COMMAND sokol-shdc)
endif()

set(SHDC_FLAGS
	--slang glsl410:metal_macos
	--format sokol
)
set(SHADER_FLAG_FILE "${CMAKE_BINARY_DIR}/shader_flags.txt")
file(WRITE ${SHADER_FLAG_FILE} "${SHDC_FLAGS}\n")

if(NOT DEFINED SHADER_OUTPUT_DIR)
	set(SHADER_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build_shd")
endif()
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

unset(SHADER_OUTPUTS)

foreach(SHADER_FILE ${SHADER_SOURCES})
	get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME_WE)
	set(SHADER_HEADER "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.glsl.h")

	add_custom_command(
		OUTPUT ${SHADER_HEADER}
		BYPRODUCTS ${SHADER_HEADER}
		COMMAND ${CMAKE_COMMAND} -E echo "compiling shader: ${SHADER_NAME}.glsl"
		COMMAND ${CMAKE_COMMAND} -E env VERBOSE=1 ${SHDC_COMMAND}
			--input ${SHADER_FILE}
			--output ${SHADER_HEADER}
			${SHDC_FLAGS}
			--module ${SHADER_NAME}
		DEPENDS ${SHADER_FILE} ${SHADER_FLAG_FILE}
		COMMENT "compiling shader: ${SHADER_NAME}.glsl"
		VERBATIM
	)

	list(APPEND SHADER_OUTPUTS ${SHADER_HEADER})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(hyprie)

file(GLOB_RECURSE HYPRIE_SOURCES
	"${CMAKE_SOURCE_DIR}/hpr/*.cpp"
	"${CMAKE_SOURCE_DIR}/hpr/*.tpp"
	"${CMAKE_SOURCE_DIR}/hpr/*.mm"
)

target_sources(hyprie PRIVATE ${HYPRIE_SOURCES})

target_include_directories(hyprie PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/hpr/core"
	"${CMAKE_SOURCE_DIR}/hpr/editor"
	"${CMAKE_SOURCE_DIR}/hpr/entity"
	"${CMAKE_SOURCE_DIR}/hpr/event"
	"${CMAKE_SOURCE_DIR}/hpr/input"
	"${CMAKE_SOURCE_DIR}/hpr/layer"
	"${CMAKE_SOURCE_DIR}/hpr/render"
	"${CMAKE_SOURCE_DIR}/hpr/resource"
	"${CMAKE_SOURCE_DIR}/hpr/runtime"
	"${CMAKE_SOURCE_DIR}/hpr/scene"
	"${CMAKE_SOURCE_DIR}/hpr/ui"

	"${CMAKE_SOURCE_DIR}/imports/metapool"
	"${CMAKE_SOURCE_DIR}/imports/sokol"
	"${CMAKE_SOURCE_DIR}/imports/stb"
	"${CMAKE_SOURCE_DIR}/imports/glm/glm"
	"${CMAKE_SOURCE_DIR}/imports/cgltf"
	"${CMAKE_SOURCE_DIR}/imports/nuklear"
	"${CMAKE_SOURCE_DIR}/imports/toml"

	"${SHADER_OUTPUT_DIR}"
)

target_compile_features(hyprie PRIVATE cxx_std_23)

target_compile_options(hyprie PRIVATE
	$<$<CONFIG:Debug>:-g>
	$<$<CONFIG:Debug>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.>
	$<$<CONFIG:Debug>:-Wall>
	$<$<CONFIG:Debug>:-Wextra>
	$<$<CONFIG:Debug>:-Wpedantic>
	$<$<CONFIG:Debug>:-Wshadow>
	$<$<CONFIG:Debug>:-Wconversion>
	$<$<CONFIG:Debug>:-Wsign-conversion>
	$<$<CONFIG:Debug>:-Wno-implicit-float-conversion>

	$<$<CONFIG:Release>:-O3>
	$<$<CONFIG:Release>:-march=native>
	$<$<CONFIG:Release>:-fstrict-aliasing>
	$<$<CONFIG:Release>:-flto>
	$<$<CONFIG:Release>:-DNDEBUG>

	$<$<CONFIG:ReleaseWithDebugInfo>:-O3>
	$<$<CONFIG:ReleaseWithDebugInfo>:-march=native>
	$<$<CONFIG:ReleaseWithDebugInfo>:-fstrict-aliasing>
	$<$<CONFIG:ReleaseWithDebugInfo>:-flto>
	$<$<CONFIG:ReleaseWithDebugInfo>:-DNDEBUG>
	$<$<CONFIG:ReleaseWithDebugInfo>:-g>
)

if(UNIX AND NOT APPLE)
	target_compile_definitions(hyprie PRIVATE
		SOKOL_GLCORE
		SOKOL_WAYLAND
		SOKOL_DONT_USE_X11
		SOKOL_NO_DEPRECATED
	)
	target_link_libraries(hyprie PRIVATE
		GL
		EGL
		wayland-client
		wayland-cursor
		wayland-egl
		xkbcommon
		pthread
		dl
		m
	)
else()
	message(FATAL_ERROR "non-linux platforms are not supported")
endif()

if(UNIX AND NOT APPLE)
	target_compile_definitions(hyprie PRIVATE SOKOL_GLCORE)
	target_link_libraries(hyprie PRIVATE
		GL X11 Xcursor Xi pthread dl m
	)
else()
	message(FATAL_ERROR "non-linux platforms are not supported")
endif()

if(MTP_ENABLE_TRACE)
	target_compile_definitions(hyprie PRIVATE MTP_ENABLE_TRACE=1)
endif()

if(MTP_CONTAINERS_BOTH)
	target_compile_definitions(hyprie PRIVATE MTP_CONTAINERS_BOTH=1)
endif()

add_dependencies(hyprie shaders)

add_custom_target(run
	COMMAND $<TARGET_FILE:hyprie> ${ARGS}
	DEPENDS hyprie
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	VERBATIM
)

