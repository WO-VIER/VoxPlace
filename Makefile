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
	rm -rf build_debug build_release
	find . -name "*.sqlite3*" -delete
	find . -name "*.tracy" -delete
