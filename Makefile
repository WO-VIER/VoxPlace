JOBS ?= 8

.PHONY: help native wasm serve clean fclean

help:
	@echo "Usage: make <target>"
	@echo "Targets:"
	@echo "  native    Configure & build native (CMake)"
	@echo "  wasm      Configure & build WebAssembly (Emscripten)"
	@echo "  serve     Serve build_wasm/ on http://localhost:8000"
	@echo "  clean     Remove build/ and build_wasm/"
	@echo "  fclean    Full clean: removes build dirs and generated artifacts"

native:
	scripts/build_native.sh

wasm:
	scripts/build_wasm.sh

serve:
	scripts/serve_wasm.sh

clean:
	rm -rf build build_wasm

fclean:
	# full-clean: supprime les dossiers de build et les fichiers générés
	# depuis la racine du repo
	scripts/clean_all.sh
