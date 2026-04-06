JOBS ?= 8

.PHONY: help debug release pregen clean fclean

ARGS ?=

help:
	@echo "Usage: make <target>"
	@echo "Targets:"
	@echo "  debug     Configure & build debug"
	@echo "  release   Configure & build release"
	@echo "  pregen    Build VoxPlacePregen tool"
	@echo "  clean     Remove build directories"
	@echo "  fclean    Full clean"

debug:
	BUILD_TYPE=Debug scripts/build_native.sh

release:
	BUILD_TYPE=Release scripts/build_native.sh

pregen:
	BUILD_TYPE=Release scripts/build_chunky.sh

clean:
	rm -rf build_debug build_release

fclean:
	rm -rf build_debug build_release build build_chunky build_asan build_relwithdebinfo build_minsizerel
	rm -rf build/win-debug build/win-release
	rm -rf vcpkg_installed
	find . -maxdepth 1 -name "*.sqlite3*" -delete
	find . -maxdepth 1 -name "*.tracy" -delete
