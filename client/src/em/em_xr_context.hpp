// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for a data structure used by the ElectricMaple XR streaming solution
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#include <openxr/openxr.h>
#include <algorithm>
#include <vector>
#include <string>

namespace em {

using ExtensionList = std::vector<std::string>;

struct XrContext {
    XrInstance instance{XR_NULL_HANDLE};
    XrSession  session {XR_NULL_HANDLE};
    const ExtensionList* enabled_extensions{nullptr};

    bool       is_valid() const;
    XrSystemId system_id() const;
    bool       is_ext_enabled(const char* ext_name) const;
};

inline bool XrContext::is_valid() const {
    return instance != XR_NULL_HANDLE && session != XR_NULL_HANDLE;
}

inline bool XrContext::is_ext_enabled(const char* ext_name) const {
    if (!is_valid() || ext_name == nullptr || enabled_extensions == nullptr)
        return false;
    return std::find(enabled_extensions->begin(),
                     enabled_extensions->end(), ext_name) != enabled_extensions->end();
}

inline XrSystemId XrContext::system_id() const {
    if (!is_valid())
        return XR_NULL_SYSTEM_ID;
    XrSystemGetInfo sys_info = {
        .type       = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
    };
    XrSystemId sys_id = XR_NULL_SYSTEM_ID;
    if (XR_FAILED(xrGetSystem(instance, &sys_info, &sys_id)))
        return XR_NULL_SYSTEM_ID;
    return sys_id;
}

}