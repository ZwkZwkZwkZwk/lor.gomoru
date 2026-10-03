.DEFAULT_GOAL := app
CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc
CORE = src/game.cpp src/storage.cpp
APP_BUNDLE = build/v1.3/lor.gomoku.app
build:
	mkdir -p build
build/core_test: tests/core_test.cpp $(CORE) src/game.hpp src/storage.hpp | build
	$(CXX) $(CXXFLAGS) tests/core_test.cpp $(CORE) -o $@
test-core: build/core_test
	./build/core_test
test: test-core
.PHONY: test-core test
build/ai_test: tests/ai_test.cpp src/ai.cpp src/ai.hpp src/game.cpp src/game.hpp | build
	$(CXX) $(CXXFLAGS) tests/ai_test.cpp src/ai.cpp src/game.cpp -o $@
test-ai: build/ai_test
	./build/ai_test
build/network_test: tests/network_test.cpp src/network.cpp src/network.hpp $(CORE) | build
	$(CXX) $(CXXFLAGS) tests/network_test.cpp src/network.cpp $(CORE) -o $@
test-network: build/network_test
	./build/network_test
build/ui_state_test: tests/ui_state_test.cpp src/ui_state.cpp src/ui_state.hpp $(CORE) | build
	$(CXX) $(CXXFLAGS) tests/ui_state_test.cpp src/ui_state.cpp $(CORE) -o $@
test-ui: build/ui_state_test
	./build/ui_state_test
test: test-ai test-network test-ui
vendor/raylib-5.5/src/libraylib.a:
	$(MAKE) -C vendor/raylib-5.5/src PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC -j4
assets/glyphs.txt: $(wildcard src/*.cpp) $(wildcard src/*.hpp) tools/glyphs.py
	python3 tools/glyphs.py
build/Gomoku: src/main.cpp $(CORE) src/ai.cpp src/network.cpp src/ui_state.cpp $(wildcard src/*.hpp) vendor/raylib-5.5/src/libraylib.a assets/glyphs.txt | build
	$(CXX) $(CXXFLAGS) -Ivendor/raylib-5.5/src src/main.cpp $(CORE) src/ai.cpp src/network.cpp src/ui_state.cpp vendor/raylib-5.5/src/libraylib.a -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo -o $@
assets/AppIcon.icns: assets/icon-source.png tools/icon.py
	python3 tools/icon.py
app: build/Gomoku assets/AppIcon.icns
	mkdir -p $(APP_BUNDLE)/Contents/MacOS $(APP_BUNDLE)/Contents/Resources/assets
	cp build/Gomoku $(APP_BUNDLE)/Contents/MacOS/Gomoku
	cp assets/NotoSansSC-Medium.ttf assets/NotoSansSC-SemiBold.ttf assets/OFL.txt assets/glyphs.txt assets/icon-source.png $(APP_BUNDLE)/Contents/Resources/assets/
	cp assets/AppIcon.icns $(APP_BUNDLE)/Contents/Resources/AppIcon.icns
	cp assets/Info.plist $(APP_BUNDLE)/Contents/Info.plist
	cp vendor/raylib-5.5/LICENSE $(APP_BUNDLE)/Contents/Resources/RAYLIB-LICENSE.txt
	codesign --force --sign - $(APP_BUNDLE)
	codesign --verify --deep --strict $(APP_BUNDLE)
.PHONY: test-ai test-network test-ui app
build/font_test: tests/font_test.cpp assets/glyphs.txt vendor/raylib-5.5/src/libraylib.a | build
	$(CXX) $(CXXFLAGS) -Ivendor/raylib-5.5/src tests/font_test.cpp vendor/raylib-5.5/src/libraylib.a -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo -o $@
test-font: build/font_test
	./build/font_test
test: test-font
.PHONY: test-font
