#pragma once

#include <string>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "ResourceRange.h"

#include "include/cef_resource_handler.h"
#include "include/wrapper/cef_helpers.h"

namespace WallpaperEngine::Data::Model {
struct Project;
}

namespace WallpaperEngine::WebBrowser::CEF {

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Data::Model;

/**
 * wp{id}:// actual handler called by cef to access files
 */
class WPSchemeHandler : public CefResourceHandler {
public:
    explicit WPSchemeHandler (const Project& project);

    bool Open (CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback> callback) override;

    void
    GetResponseHeaders (CefRefPtr<CefResponse> response, int64_t& response_length, CefString& redirectUrl) override;

    void Cancel () override;

    bool Skip (int64_t bytes_to_skip, int64_t& bytes_skipped, CefRefPtr<CefResourceSkipCallback> callback) override;

    bool
    Read (void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefResourceReadCallback> callback) override;

private:
    const Project& m_project;

    const AssetLocator& m_assetLoader;
    ReadStreamSharedPtr m_contents = nullptr;
    std::string m_mimeType;
    int64_t m_length = 0;
    int64_t m_position = 0;
    std::optional<ResourceRange> m_range;
    bool m_partialResponse = false;

    IMPLEMENT_REFCOUNTING (WPSchemeHandler);
    DISALLOW_COPY_AND_ASSIGN (WPSchemeHandler);
};
};
