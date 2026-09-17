// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ads.h"

#include <atomic>

namespace t2_ht {

namespace {
std::atomic<AdsMode> g_adsMode{kDefaultAdsMode};
}  // namespace

AdsMode GetAdsMode() { return g_adsMode.load(std::memory_order_relaxed); }
void SetAdsMode(AdsMode mode) { g_adsMode.store(mode, std::memory_order_relaxed); }

}  // namespace t2_ht
