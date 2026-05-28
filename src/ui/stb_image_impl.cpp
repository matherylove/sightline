// stb_image_impl.cpp
// This translation unit:
//   1. Defines ThumbnailCache::s_instance (exactly once)
//   2. Provides the stb_image implementation
//
// Download the real stb_image.h from:
//   https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
// and place it at third_party/stb_image.h

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "../../third_party/stb_image.h"

#include "ThumbnailCache.h"

// Single definition of the static singleton pointer
ThumbnailCache* ThumbnailCache::s_instance = nullptr;
