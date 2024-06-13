// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal source for display refresh configuration on runtimes supporting XR_FB_display_refresh_rates
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
 #include "em_display_refresh_rates.hpp"
#include <optional>

namespace em {

XrDisplayRefreshRates::XrDisplayRefreshRates(const XrContext& xr_ctx) noexcept
: m_xr_ctx{xr_ctx} {
    load_ext_functions();
}

bool XrDisplayRefreshRates::is_supported() const {
    return m_xr_ctx.is_ext_enabled(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
}

std::optional<float> XrDisplayRefreshRates::current_refresh_rate() const {
    if (xrGetDisplayRefreshRateFB == nullptr || !is_supported())
        return std::nullopt;
    float current_rate = 0;
    if (XR_FAILED(xrGetDisplayRefreshRateFB(m_xr_ctx.session, &current_rate)))
        return std::nullopt;
    return current_rate;
}

RefreshRateList XrDisplayRefreshRates::available_refresh_rates() const {
    if (xrRequestDisplayRefreshRateFB == nullptr || !is_supported())
        return {};

    std::uint32_t size = 0;
    if (XR_FAILED(xrEnumerateDisplayRefreshRatesFB(m_xr_ctx.session, 0, &size, nullptr)) || size == 0)
        return {};

    RefreshRateList refresh_rates;
    refresh_rates.resize(size, 0);
    if (XR_FAILED(xrEnumerateDisplayRefreshRatesFB(m_xr_ctx.session, size, &size, refresh_rates.data())))
        return {};
    return refresh_rates;
}

bool XrDisplayRefreshRates::set_refresh_rate(const float new_rate) { 
    if (xrRequestDisplayRefreshRateFB == nullptr || !is_supported())
        return false;
    return XR_SUCCEEDED(xrRequestDisplayRefreshRateFB(m_xr_ctx.session, new_rate));
}

bool XrDisplayRefreshRates::load_ext_functions() {
    if (!is_supported())
        return false;

    #ifndef XR_EXTENSION_PROTOTYPES
#define EM_INIT_PFN(ExtName)\
    if (XR_FAILED(xrGetInstanceProcAddr(m_xr_ctx.instance, #ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&ExtName))) || ExtName == nullptr) { \
        return false; \
    }

    EM_INIT_PFN(xrGetDisplayRefreshRateFB);
    EM_INIT_PFN(xrRequestDisplayRefreshRateFB);
    EM_INIT_PFN(xrEnumerateDisplayRefreshRatesFB);

#undef EM_INIT_PFN
#endif
    return false;
}

}