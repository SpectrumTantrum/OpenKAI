# OpenKAI SDK Guide Questions

This file records source or config ambiguities noticed while writing the SDK guide. These are documentation questions only; no source files or existing configs were changed.

## `_Resize` Size Field

`jsonCfg/_Camera.json:161` defines a `_Resize` module with `"vSize"`, but `_Resize` inherits `_VisionBase`, whose `init()` reads `"vSizeRGB"` at `src/Vision/_VisionBase.cpp:36`. Should `_Resize` configs use `vSizeRGB`, or is there another path where `vSize` is intended to be read?
