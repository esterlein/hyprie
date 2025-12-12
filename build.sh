#!/usr/bin/env bash

set -e

build_dir="build"
build_shd_dir="build_shd"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "${compiler:-}" ]]; then
	if command -v clang++ >/dev/null 2>&1; then
		compiler="clang++"
	elif command -v g++ >/dev/null 2>&1; then
		compiler="g++"
	else
		echo "no suitable compiler found" >&2
		exit 1
	fi
fi

cxx="$compiler"

cores()
{
	if command -v nproc >/dev/null 2>&1; then
		nproc
		return
	fi
	if command -v sysctl >/dev/null 2>&1; then
		sysctl -n hw.ncpu
		return
	fi
	echo 4
}

cpu_count="$(cores)"

clean=0
run=0
shd_only=0

while [[ "$1" =~ ^(clean|run|shd)$ ]]; do
	case "$1" in
		clean) clean=1 ;;
		run) run=1 ;;
		shd) shd_only=1 ;;
	esac
	shift
done

args="$*"

clean_build_dir()
{
	rm -rf "$build_dir"
	rm -rf "$build_shd_dir"
	mkdir -p "$build_dir"
	mkdir -p "$build_shd_dir"
}

find_shdc()
{
	if [[ -n "${SHDC:-}" && -x "${SHDC}" ]]; then
		printf '%s\n' "$SHDC"
		return
	fi
	if command -v sokol-shdc >/dev/null 2>&1; then
		command -v sokol-shdc
		return
	fi

	local shdc_candidate_path
	shdc_candidate_path="$(find "$script_dir" -maxdepth 4 -type f -name sokol-shdc 2>/dev/null | head -n1 || true)"
	if [[ -n "$shdc_candidate_path" ]]; then
		printf '%s\n' "$shdc_candidate_path"
		return
	fi

	printf ''
}

shdc_generate()
{
	local shdc_command
	shdc_command="$(find_shdc)"
	if [[ -z "$shdc_command" ]]; then
		echo "sokol-shdc not found" >&2
		exit 1
	fi

	mkdir -p "$build_shd_dir"

	if [[ -d "$script_dir/shaders" ]]; then
		shopt -s nullglob
		for shader_path in "$script_dir"/shaders/*.glsl
		do
			shader_name="$(basename "$shader_path" .glsl)"
			output_header_path="$build_shd_dir/${shader_name}.glsl.h"
			if [[ ! -f "$output_header_path" || "$shader_path" -nt "$output_header_path" ]]; then
				"$shdc_command" \
					--input "$shader_path" \
					--output "$output_header_path" \
					--slang glsl410:metal_macos \
					--format sokol \
					--module "$shader_name"
			fi
		done
		shopt -u nullglob
	fi
}

configure_and_build()
{
	extra_args="$1"
	cxx_flags="-std=c++23"

	local shdc_command
	shdc_command="$(find_shdc)"

	cmake -B "$build_dir" -S . \
		-DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_CXX_FLAGS="$cxx_flags" \
		-DSHADER_OUTPUT_DIR="$(cd "$build_shd_dir" && pwd)" \
		${shdc_command:+-DSHDC_COMMAND="$shdc_command"} \
		$extra_args

	export VERBOSE=1
	cmake --build "$build_dir" -j"$cpu_count"

	if [[ -f "$script_dir/$build_dir/compile_commands.json" ]]; then
		ln -sf "$script_dir/$build_dir/compile_commands.json" "$script_dir/compile_commands.json"
	fi
}

[[ "$clean" -eq 1 ]] && clean_build_dir

shdc_generate

if [[ "$shd_only" -eq 1 ]]; then
	exit 0
fi

cmake_args=""
if [[ -n "$args" ]]; then
	cmake_args="-DARGS=$args"
fi

configure_and_build "$cmake_args"

if [[ "$run" -eq 1 ]]; then
	cmake --build "$build_dir" --target run
fi

