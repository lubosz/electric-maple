// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal source for a data structure used by the ElectricMaple XR streaming solution
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#include "em_passthrough.hpp"
#include "em_status.h"
#include "em_app_log.h"
#include <algorithm>
#include <cassert>

namespace em {

inline constexpr const XrCompositionLayerFlags DefaultProjectionLayerFlags =
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;

// Passthrough
bool Passthrough::set_blend_mode(const XrEnvironmentBlendMode new_mode) {
    if (!xr_ctx().is_valid() || new_mode == m_eb_mode)
        return false;
    if (!set_blend_mode_handler(new_mode))
        return false;
    m_eb_mode = new_mode;
    return true;
}

Passthrough::ClearColor Passthrough::clear_color() const {
    switch (env_blend_mode()) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
        return {0.f,0.f,0.f,0.0f};
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
    default: return {0.f,0.f,0.f,1.f};
    }
}
// End Passthrough

// FBPassthrough
FBPassthrough::FBPassthrough(const XrContext& xr_ctx) noexcept
: Passthrough(xr_ctx)
, m_passthrough_layer {
    .type  = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
    .next  = nullptr,
    .flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
    .space = XR_NULL_HANDLE,
    .layerHandle = XR_NULL_HANDLE,
}
{
    if (!is_supported())
        return ;
    if (!load_ext_functions())
        return ;
    create_passthrough();
}

FBPassthrough::~FBPassthrough(){
    if (m_recon_pt_layer != XR_NULL_HANDLE) {
        assert(xrDestroyPassthroughLayerFB);
        xrDestroyPassthroughLayerFB(m_recon_pt_layer);
        m_recon_pt_layer = XR_NULL_HANDLE;
    }

    if (m_passthrough != XR_NULL_HANDLE) {
        assert(xrDestroyPassthroughFB);
        xrDestroyPassthroughFB(m_passthrough);
        m_passthrough = XR_NULL_HANDLE;
    }
}

bool FBPassthrough::is_supported() const {
    if (!xr_ctx().is_ext_enabled(XR_FB_PASSTHROUGH_EXTENSION_NAME))
        return false;
    XrSystemPassthroughPropertiesFB pt_sys_properties = {
        .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB,
        .next = nullptr,
        .supportsPassthrough = XR_FALSE
    };
    XrSystemProperties sys_properties = {
        .type = XR_TYPE_SYSTEM_PROPERTIES,
        .next = &pt_sys_properties
    };
    if (XR_FAILED(xrGetSystemProperties(xr_ctx().instance, xr_ctx().system_id(), &sys_properties)))
        return false;
    return pt_sys_properties.supportsPassthrough == XR_TRUE;
}

bool FBPassthrough::use_alpha_blend_for_additive() const {
    switch (env_blend_mode()) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return true;
    default: return false;
    }
}

PassthroughLayer FBPassthrough::composition_layer() const {
    if (m_recon_pt_layer == XR_NULL_HANDLE)
        return {};    
    switch (env_blend_mode()) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
        return {
            .comp_layer  = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&m_passthrough_layer),
            .projection_layer_flags = DefaultProjectionLayerFlags,
            .env_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        };
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
    default: return {};
    }
}

bool FBPassthrough::set_blend_mode_handler(const XrEnvironmentBlendMode mode) {
    if (!xr_ctx().is_valid()) {
        return false;
    }
    switch (mode) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
        return resume_passthrough_layer();
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
    default:
        return pasue_passthrough_layer();
    }
}

bool FBPassthrough::resume_passthrough_layer() {
    if (m_recon_pt_layer == XR_NULL_HANDLE)
        return false;

    if (XR_FAILED(xrPassthroughStartFB(m_passthrough))) {
        ALOGE("Failed to start passthrough.");
        return false;
    }

    if (XR_FAILED(xrPassthroughLayerResumeFB(m_recon_pt_layer))) {
        ALOGE("Failed to resume passthrough layer.");
        return false;
    }

    ALOGI("FPassthrough (Layer) is started/resumed.");

    constexpr const XrPassthroughStyleFB style {
        .type = XR_TYPE_PASSTHROUGH_STYLE_FB,
        .next = nullptr,
        .textureOpacityFactor = 0.5f,
        .edgeColor = { 0.0f, 0.0f, 0.0f, 0.0f },
    };
    return XR_SUCCEEDED(xrPassthroughLayerSetStyleFB(m_recon_pt_layer, &style));
}

bool FBPassthrough::pasue_passthrough_layer() {
    if (m_recon_pt_layer == XR_NULL_HANDLE)
        return false;
    if (XR_FAILED(xrPassthroughLayerPauseFB(m_recon_pt_layer))) {
        ALOGW("Failed to pause passthrough layer.");
    }
    if (XR_FAILED(xrPassthroughPauseFB(m_passthrough))) {
        ALOGW("Failed to pause/stop passthrough.");
    }
    return true;
}

bool FBPassthrough::create_passthrough() {
    if (!xr_ctx().is_valid())
        return false;
    assert(is_supported());

    constexpr const XrPassthroughCreateInfoFB ptci = {
        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
        .next = nullptr            
    };
    if (XR_FAILED(xrCreatePassthroughFB(xr_ctx().session, &ptci, &m_passthrough)) ||
        m_passthrough == XR_NULL_HANDLE) {
        return false;
    }

    const XrPassthroughLayerCreateInfoFB plci = {
        .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
        .next = nullptr,
        .passthrough = m_passthrough,
        .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB
    };
    if (XR_FAILED(xrCreatePassthroughLayerFB(xr_ctx().session, &plci, &m_recon_pt_layer)) ||
        m_recon_pt_layer == XR_NULL_HANDLE) {
        xrDestroyPassthroughFB(m_passthrough);
        m_passthrough = XR_NULL_HANDLE;
        return false;
    }

    if (m_recon_pt_layer != XR_NULL_HANDLE) {
        m_passthrough_layer.layerHandle = m_recon_pt_layer;
    }
    return m_recon_pt_layer != XR_NULL_HANDLE;
}

bool FBPassthrough::load_ext_functions() {
    if (!xr_ctx().is_valid())
        return false;
    assert(is_supported());

#ifndef XR_EXTENSION_PROTOTYPES
#define EM_INIT_PFN(ExtName)\
    if (XR_FAILED(xrGetInstanceProcAddr(xr_ctx().instance, #ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&ExtName))) || ExtName == nullptr) { \
        return false; \
    }

    EM_INIT_PFN(xrCreatePassthroughFB);
    EM_INIT_PFN(xrDestroyPassthroughFB);
    EM_INIT_PFN(xrPassthroughStartFB);
    EM_INIT_PFN(xrPassthroughPauseFB);
    EM_INIT_PFN(xrCreatePassthroughLayerFB);
    EM_INIT_PFN(xrDestroyPassthroughLayerFB);
    EM_INIT_PFN(xrPassthroughLayerSetStyleFB);
    EM_INIT_PFN(xrPassthroughLayerPauseFB);
    EM_INIT_PFN(xrPassthroughLayerResumeFB);
#undef EM_INIT_PFN
#endif
    return true;
}
// End FBPassthrough

// HTCPassthrough
inline constexpr const XrPassthroughColorHTC HTCPassthroughColor = {
    .type  = XR_TYPE_PASSTHROUGH_COLOR_HTC,
    .next  = nullptr,
    .alpha = 0.5f
};

HTCPassthrough::HTCPassthrough(const XrContext& xr_ctx) noexcept
: Passthrough(xr_ctx)
, m_passthrough_layer {
    .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC,
    .next = nullptr,
    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
    .space = XR_NULL_HANDLE,
    .passthrough = XR_NULL_HANDLE,
    .color = HTCPassthroughColor
}
{
    if (!is_supported())
        return ;
    load_ext_functions();
}

HTCPassthrough::~HTCPassthrough() {
    pasue_passthrough_layer();
}

bool HTCPassthrough::is_supported() const {
    return xr_ctx().is_ext_enabled(XR_HTC_PASSTHROUGH_EXTENSION_NAME);
}

bool HTCPassthrough::use_alpha_blend_for_additive() const {
    switch (env_blend_mode()) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return true;
    default: return false;
    }
}

PassthroughLayer HTCPassthrough::composition_layer() const {
    if (m_passthroughHTC == XR_NULL_HANDLE)
        return {};
    
    switch (env_blend_mode()) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
        return {
            .comp_layer  = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&m_passthrough_layer),
            .projection_layer_flags = DefaultProjectionLayerFlags,
            .env_blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        };
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
    default: return {};
    }
}

bool HTCPassthrough::set_blend_mode_handler(const XrEnvironmentBlendMode mode) {
    if (!xr_ctx().is_valid()) {
        return false;
    }
    switch (mode) {
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
        return resume_passthrough_layer();
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
    default:
        return pasue_passthrough_layer();
    }
}

bool HTCPassthrough::resume_passthrough_layer() {
    if (!is_supported())
        return false;
     if (m_passthroughHTC != XR_NULL_HANDLE) // already active.
        return true;
    constexpr const XrPassthroughCreateInfoHTC createInfo = {
        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC,
        .next = nullptr,
        .form = XR_PASSTHROUGH_FORM_PLANAR_HTC,
    };
    if (XR_FAILED(xrCreatePassthroughHTC(xr_ctx().session, &createInfo, &m_passthroughHTC))) {
        ALOGE("Failed to start/resume passthrough layer");
        m_passthroughHTC = XR_NULL_HANDLE;
        return false;
    }
    ALOGI("Passthrough (Layer) is started/resumed.");
    return true;
}

bool HTCPassthrough::pasue_passthrough_layer() {
    if (!is_supported())
        return false;
    if (m_passthroughHTC != XR_NULL_HANDLE) {
        if (XR_FAILED(xrDestroyPassthroughHTC(m_passthroughHTC))) {
            ALOGW("Failed to stop/pase passthrough layer");
        }
    }
    m_passthroughHTC = XR_NULL_HANDLE;
    return true;
}

bool HTCPassthrough::load_ext_functions() {
    if (!xr_ctx().is_valid())
        return false;
    assert(is_supported());

#ifndef XR_EXTENSION_PROTOTYPES
#define EM_INIT_PFN(ExtName)\
    if (XR_FAILED(xrGetInstanceProcAddr(xr_ctx().instance, #ExtName, reinterpret_cast<PFN_xrVoidFunction*>(&ExtName))) || ExtName == nullptr) { \
        return false; \
    }

    EM_INIT_PFN(xrCreatePassthroughHTC);
    EM_INIT_PFN(xrDestroyPassthroughHTC);
#undef EM_INIT_PFN
#endif
    return true;
}
// End FBPassthrough

// EMBPassthrough
EBMPassthrough::EBMPassthrough(const XrContext& xr_ctx) noexcept
: Passthrough{xr_ctx} {
    if (!xr_ctx.is_valid())
        return;
    const XrSystemId sysId = xr_ctx.system_id();
    if (sysId == XR_NULL_SYSTEM_ID)
        return;
    constexpr const auto view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    std::uint32_t count = 0;
    if (XR_FAILED(xrEnumerateEnvironmentBlendModes(xr_ctx.instance, sysId, view_type, 0, &count, nullptr)))
        return;
    m_availableBlendModes.resize(count, XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
    xrEnumerateEnvironmentBlendModes(xr_ctx.instance, sysId, view_type, count, &count, m_availableBlendModes.data());

    m_use_alpha_blend_for_additive = !has_mode(XR_ENVIRONMENT_BLEND_MODE_ADDITIVE) &&
                                      has_mode(XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
}

bool EBMPassthrough::has_mode(const XrEnvironmentBlendMode mode) const {
    return std::find(m_availableBlendModes.begin(), m_availableBlendModes.end(), mode) != m_availableBlendModes.end();
}

bool EBMPassthrough::is_supported() const {
    return std::find_if(m_availableBlendModes.begin(), m_availableBlendModes.end(), [](const XrEnvironmentBlendMode emb) { 
        switch (emb) {
        case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
        case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
            return true;
        default: return false;
        }
    }) != m_availableBlendModes.end();
}

PassthroughLayer EBMPassthrough::composition_layer() const {
    PassthroughLayer result = {
        .env_blend_mode = env_blend_mode(),
    };
    if (result.env_blend_mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND ||
        (result.env_blend_mode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE && use_alpha_blend_for_additive())) {
        result.projection_layer_flags = DefaultProjectionLayerFlags;
    }
    return result;
}
// End EMBPassthrough

std::unique_ptr<Passthrough> make_passthrough(const XrContext& xr_ctx) {
    std::unique_ptr<Passthrough> result = std::make_unique<FBPassthrough>(xr_ctx);
    if (result->is_supported())
        return result;
    result = std::make_unique<HTCPassthrough>(xr_ctx);
    if (result->is_supported())
        return result;
    return std::make_unique<EBMPassthrough>(xr_ctx);
}
}
