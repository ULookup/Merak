.PHONY: build install uninstall clean

PREFIX ?= $(HOME)/.local
BUILD_DIR := build

build:
	@cmake -B $(BUILD_DIR) -DCMAKE_TOOLCHAIN_FILE=Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR)

install: build
	@PREFIX=$(PREFIX) ./scripts/install.sh

uninstall:
	rm -f $(PREFIX)/bin/merak
	@echo "Removed $(PREFIX)/bin/merak"
	@echo "Config files in ~/.merak/ were left untouched."

clean:
	rm -rf $(BUILD_DIR)
