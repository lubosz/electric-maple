// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for display refresh configuration on runtimes supporting XR_FB_display_refresh_rates
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#pragma once
#include "em_xr_context.hpp"
#include <vector>
#include <optional>

namespace em {

using RefreshRateList = std::vector<float>;

struct XrDisplayRefreshRates final {

    XrDisplayRefreshRates(const XrContext& xr_ctx) noexcept;

    bool is_supported() const;
    std::optional<float> current_refresh_rate() const;
    RefreshRateList available_refresh_rates() const;

    bool set_refresh_rate(const float new_rate);

private:
    bool load_ext_functions();

#ifndef XR_EXTENSION_PROTOTYPES
    PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB{nullptr};
    PFN_xrRequestDisplayRefreshRateFB    xrRequestDisplayRefreshRateFB{nullptr};
    PFN_xrGetDisplayRefreshRateFB        xrGetDisplayRefreshRateFB{nullptr};
#endif
    XrContext m_xr_ctx;
};

}