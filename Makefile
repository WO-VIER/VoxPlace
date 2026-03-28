JOBS ?= 8
ASAN_BUILD_DIR ?= build_asan

.PHONY: help native native-release native-asan asan asan-client asan-server clean fclean
ARGS ?=

help:
	@echo "Usage: make <target>"
	@echo "Targets:"
	@echo "  native          Configure & build native debug (CMake)"
	@echo "  native-release  Configure & build native release (CMake)"
	@echo "  native-asan     Configure & build native with Address Sanitizer"
	@echo "  asan            Alias pratique pour native-asan"
	@echo "  asan-client     Build ASAN puis lance le client"
	@echo "  asan-server     Build ASAN puis lance le serveur"
	@echo "  clean           Remove build directories"
	@echo "  fclean          Full clean: removes build dirs and generated artifacts"

native:
	scripts/build_native.sh

native-release:
	BUILD_TYPE=Release scripts/build_native.sh

native-asan:
	scripts/build_native_asan.sh

asan: native-asan

asan-client: native-asan
	scripts/run_client_asan.sh $(ARGS)

asan-server: native-asan
	./$(ASAN_BUILD_DIR)/VoxPlaceServer

clean:
	rm -rf build build_release build_relwithdebinfo build_minsizerel

fclean:
	scripts/clean_all.sh
