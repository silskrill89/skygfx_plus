# RenderWare 3.7 SDK Reference

## Location (LOCAL)
```
E:\dev(dave)\skygfx_plus_expIV\external\d3d9
```

## Location (SDK)
```
E:\SDKs\Renderware 3.7 SDK (For Windows) Full\RWSDK37\Graphics\rwsdk\include\d3d9
```

## Key Structures

### RwObject (rwplcore.h:2082)
```
Offset  Size  Field
0       1     type
1       1     subType
2       1     flags
3       1     privateFlags
4       4     parent (void*)
```
Total: 8 bytes

### RwObjectHasFrame (rwcore.h:4136)
```
Offset  Size  Field
0       8     RwObject object
8       8     RwLLLink lFrame (next, prev)
16      4     sync (function pointer)
```
Total: 20 bytes

### RwCamera (rwcore.h:4651)
```
Offset  Size  Field
0       20    RwObjectHasFrame object
20      ...   (camera-specific fields)
```

### Crash at offset 8
When `RwCameraCreate()` returns NULL, `RwCameraSetFrame(NULL, frame)` accesses
`camera->object.lFrame.next` at offset 8 of the NULL pointer → CRASH.

Fix: Always null-check `RwCameraCreate()` and `RwFrameCreate()` results.

## Include Path
Use `external/d3d9` as the primary reference for all RW SDK headers.
The vcxproj already includes this path.
