# Vulkan headers

Only the core and Win32 declarations needed by the optional DXVK GPU bridge are
vendored, from Khronos Vulkan-Headers `v1.2.198`. No loader DLL or Vulkan SDK is
bundled. The loader already used by DXVK supplies all entry points at runtime.

`source.json` records exact upstream URLs and SHA256; `.gitattributes` preserves
those bytes. See `LICENSE.txt` and copyright notices in each header.
