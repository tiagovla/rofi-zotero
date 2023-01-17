# Installation

```bash
    mkdir build
    cmake -B build .
    cmake --build build
    sudo cmake --install build
```

# Dev

```bash
    G_MESSAGES_DEBUG=Plugin_Zotero rofi -show zotero -plugin-path ./build/lib -theme ./theme/zotero.rasi
```
