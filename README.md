# Cocos2d

Base version is: **cocos2d-x-3.0alpha0-pre** (commit: 50f2cba03fd7bec4cccba86147003fc83d93e610)

## Manually building

You can build cocos2d by emscripten with this commands:

```
make PLATFORM=emscripten DEBUG=1 -j10 # (for debug build)
make PLATFORM=emscripten DEBUG=0 -j10 # (for release build)
```

## Using as emscripten-ports

Add flags ```-s USE_COCOS2D=3 -s STB_IMAGE=1``` to build