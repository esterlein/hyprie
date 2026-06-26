#!/usr/bin/env bash

set -euo pipefail

apply_submodule_checkout()
{
	submodule_path="$1"
	sparse_patterns_file="$(mktemp)"
	awk -v p="$submodule_path" -F'\t' '$1==p{print $2}' .gitmodules.sparse > "$sparse_patterns_file"
	[ -s "$sparse_patterns_file" ] || { rm -f "$sparse_patterns_file"; return; }
	git submodule sync -- "$submodule_path"
	git submodule update --init --depth 1 --filter=blob:none "$submodule_path"
	git -C "$submodule_path" config core.sparseCheckout true
	git -C "$submodule_path" config core.sparseCheckoutCone false
	git -C "$submodule_path" sparse-checkout init --no-cone
	git -C "$submodule_path" sparse-checkout set --no-cone --stdin < "$sparse_patterns_file"
	git -C "$submodule_path" read-tree -mu HEAD
	git -C "$submodule_path" sparse-checkout reapply
	git -C "$submodule_path" config remote.origin.partialclonefilter blob:none
	rm -f "$sparse_patterns_file"
}

main()
{
	git submodule sync --recursive
	git submodule update --init --recursive --depth 1 --filter=blob:none
	git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | while read -r _ submodule_path
	do
		apply_submodule_checkout "$submodule_path"
	done
	git submodule foreach --recursive 'git config fetch.recurseSubmodules on-demand' >/dev/null
}

main "$@"

