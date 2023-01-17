all: configure build

configure:
	mkdir build && cmake -B build -S .

build:
	cmake --build build

debug:
	cmake --build build && G_MESSAGES_DEBUG=Plugin_Zotero rofi -show zotero -plugin-path ./build/lib -theme ./theme/zotero.rasi

install:
	cmake --install build

clear:
	rm -r build | exit

rerun:
	cmake --build build && G_MESSAGES_DEBUG=Plugin_Zotero rofi -show zotero -plugin-path ./build/lib -theme ./theme/zotero.rasi


