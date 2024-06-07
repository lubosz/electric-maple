// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for a data structure used by the ElectricMaple XR streaming solution
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#pragma once
#include <memory>
#include <array>
#include <vector>
#include <openxr/openxr.h>
#include "em_xr_context.hpp"

namespace em {

struct PassthroughLayer final {
    const XrCompositionLayerBaseHeader* comp_layer{nullptr};
    XrCompositionLayerFlags             projection_layer_flags{0};
    XrEnvironmentBlendMode              env_blend_mode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
};

struct Passthrough {
    Passthrough(const XrContext& xr_ctx) noexcept
    : m_xr_ctx{xr_ctx}{}
    virtual ~Passthrough() = default;

    const XrContext& xr_ctx() const noexcept { return m_xr_ctx; }
    virtual bool is_supported() const = 0;
    virtual bool use_alpha_blend_for_additive() const { return false; }

    using ClearColor = XrQuaternionf;    
    virtual ClearColor clear_color() const;
    virtual PassthroughLayer composition_layer() const = 0;

    bool set_blend_mode(const XrEnvironmentBlendMode new_mode);
    
    Passthrough(Passthrough&&) = delete;
    Passthrough(const Passthrough&) = delete;    
    Passthrough& operator=(Passthrough&&) = delete;
    Passthrough& operator=(const Passthrough&) = delete;

protected:
    XrEnvironmentBlendMode env_blend_mode() const noexcept {return m_eb_mode;}
private:    
    virtual bool set_blend_mode_handler(const XrEnvironmentBlendMode mode) = 0;

    XrContext m_xr_ctx;
    XrEnvironmentBlendMode m_eb_mode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
};

// XR_FB_passthrough
struct FBPassthrough final : Passthrough {
    FBPassthrough(const XrContext& xr_ctx) noexcept;
    ~FBPassthrough() override;

    bool is_supported() const override;
    bool use_alpha_blend_for_additive() const override;
    PassthroughLayer composition_layer() const override;

private:
    bool set_blend_mode_handler(const XrEnvironmentBlendMode mode) override;
    bool resume_passthrough_layer();
    bool pasue_passthrough_layer();
    bool create_passthrough();
    bool load_ext_functions();

#ifndef XR_EXTENSION_PROTOTYPES
    PFN_xrCreatePassthroughFB        xrCreatePassthroughFB{nullptr};
    PFN_xrDestroyPassthroughFB       xrDestroyPassthroughFB{nullptr};
    PFN_xrPassthroughStartFB         xrPassthroughStartFB{nullptr};
    PFN_xrPassthroughPauseFB         xrPassthroughPauseFB{nullptr};
    PFN_xrCreatePassthroughLayerFB   xrCreatePassthroughLayerFB{nullptr};
    PFN_xrDestroyPassthroughLayerFB  xrDestroyPassthroughLayerFB{nullptr};
    PFN_xrPassthroughLayerSetStyleFB xrPassthroughLayerSetStyleFB{nullptr};
    PFN_xrPassthroughLayerPauseFB    xrPassthroughLayerPauseFB{nullptr};
    PFN_xrPassthroughLayerResumeFB   xrPassthroughLayerResumeFB{nullptr};
#endif
    XrPassthroughFB      m_passthrough{XR_NULL_HANDLE};
    XrPassthroughLayerFB m_recon_pt_layer{XR_NULL_HANDLE};
    XrCompositionLayerPassthroughFB m_passthrough_layer;
};

// XR_HTC_passthrough
struct HTCPassthrough final : Passthrough {
    HTCPassthrough(const XrContext& xr_ctx) noexcept;
    ~HTCPassthrough() override;

    bool is_supported() const override;
    bool use_alpha_blend_for_additive() const override;
    PassthroughLayer composition_layer() const override;

private:
    bool set_blend_mode_handler(const XrEnvironmentBlendMode mode) override;
    bool resume_passthrough_layer();
    bool pasue_passthrough_layer();
    bool load_ext_functions();

#ifndef XR_EXTENSION_PROTOTYPES
    PFN_xrCreatePassthroughHTC  xrCreatePassthroughHTC{nullptr};
    PFN_xrDestroyPassthroughHTC xrDestroyPassthroughHTC{nullptr};
#endif
    XrPassthroughHTC m_passthroughHTC{XR_NULL_HANDLE};
    XrCompositionLayerPassthroughHTC m_passthrough_layer;
};

// Enviroment Blend Mode based method
struct EBMPassthrough final : Passthrough {
    EBMPassthrough(const XrContext& xr_ctx) noexcept;

    bool is_supported() const override;
    bool use_alpha_blend_for_additive() const override {
        return m_use_alpha_blend_for_additive;
    }
    virtual PassthroughLayer composition_layer() const override;

private:
    bool has_mode(const XrEnvironmentBlendMode mode) const;
    bool set_blend_mode_handler(const XrEnvironmentBlendMode mode) override {
        return has_mode(mode);
    }

    std::vector<XrEnvironmentBlendMode> m_availableBlendModes;
    bool m_use_alpha_blend_for_additive = false;
};

std::unique_ptr<Passthrough>
make_passthrough(const XrContext& xr_ctx);

}
