#pragma once

// AvatarCache has been centralized to is::core — this header provides backward compatibility.
#include "core/AvatarCache.h"

namespace is::games::gravity_brawl {
    using AvatarCache = is::core::AvatarCache;
} // namespace is::games::gravity_brawl
