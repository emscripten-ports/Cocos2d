# Cocos2d

Base version is: **cocos2d-x-3.0alpha0-pre**

## Manually building

You can build cocos2d with emscripten bu running this commands:

```
make PLATFORM=emscripten DEBUG=1 -j10 # (for debug build)
make PLATFORM=emscripten DEBUG=0 -j10 # (for release build)
```
