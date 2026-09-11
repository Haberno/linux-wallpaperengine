#include "WPSchemeHandler.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include <iostream>
#include <algorithm>

#include "MimeTypes.h"
#include "include/cef_parser.h"

#include "WallpaperEngine/Data/Model/Project.h"

using namespace WallpaperEngine::WebBrowser::CEF;

WPSchemeHandler::WPSchemeHandler (const Project& project) :
    m_project (project), m_assetLoader (*this->m_project.assetLocator) { }

bool WPSchemeHandler::Open (CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback> callback) {
    DCHECK (!CefCurrentlyOn (TID_UI) && !CefCurrentlyOn (TID_IO));

#if !NDEBUG
    std::cout << "Processing request for path " << request->GetURL ().c_str () << std::endl;
#endif
    // url contains the full path, we need to get rid of the protocol
    // otherwise files won't be found
    CefURLParts parts;

    // url parsing is a must
    if (!CefParseURL (request->GetURL (), parts)) {
	return false;
    }

    const std::string host = CefString (&parts.host);
    const std::string path = CefString (&parts.path);

    // chromium canonicalizes the url, so spaces and non-ascii in filenames arrive percent-encoded;
    // the asset loader wants the real name back
    const std::string file = CefURIDecode (
	path.substr (1), true,
	static_cast<cef_uri_unescape_rule_t> (UU_SPACES | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS)
    );

    try {
	// try to read the file on the current container, if the file doesn't exists
	// an exception will be thrown
	if (const char* mime = MimeTypes::getType (file.c_str ()); !mime) {
	    this->m_mimeType = "application/octet+stream";
	} else {
	    this->m_mimeType = mime;
	}

	this->m_contents = this->m_assetLoader.read (file);
	this->m_contents->seekg (0, std::ios::end);
	this->m_length = this->m_contents->tellg ();
	const std::string rangeHeader = request->GetHeaderByName ("Range");
	this->m_partialResponse = !rangeHeader.empty ();
	this->m_range = parseResourceRange (rangeHeader, this->m_length);
	// CEF calls Skip for explicit range starts, but not suffix ranges. Pre-seek only
	// suffixes; otherwise the range offset would be applied twice.
	this->m_position = this->m_range && rangeHeader.starts_with ("bytes=-") ? this->m_range->offset : 0;
	this->m_contents->seekg (this->m_position, std::ios::beg);
    } catch (AssetLoadException&) {
#if !NDEBUG
	std::cout << "Cannot read file " << file << std::endl;
#endif
    }

    handle_request = true;

    return true;
}

void WPSchemeHandler::GetResponseHeaders (
    CefRefPtr<CefResponse> response, int64_t& response_length, CefString& redirectUrl
) {
    CEF_REQUIRE_IO_THREAD ();

    if (!this->m_contents) {
	response->SetError (ERR_FILE_NOT_FOUND);
	response->SetStatus (404);
	response_length = 0;
	return;
    }

    response->SetMimeType (this->m_mimeType);
    response->SetHeaderByName ("Accept-Ranges", "bytes", true);
    if (!this->m_range) {
	response->SetStatus (416);
	response->SetHeaderByName ("Content-Range", "bytes */" + std::to_string (this->m_length), true);
	response_length = 0;
	return;
    }
    response->SetStatus (this->m_partialResponse ? 206 : 200);
    response_length = this->m_range->length;
    // Chromium requires both a known length and accurate offsets when seeking custom URLs.
    response->SetHeaderByName ("Content-Length", std::to_string (response_length), true);
    if (this->m_partialResponse) {
	response->SetHeaderByName (
	    "Content-Range", "bytes " + std::to_string (this->m_range->offset) + "-"
		+ std::to_string (this->m_range->offset + this->m_range->length - 1) + "/" + std::to_string (this->m_length),
	    true
	);
    }
}

void WPSchemeHandler::Cancel () { CEF_REQUIRE_IO_THREAD (); }

bool WPSchemeHandler::Skip (
    int64_t bytes_to_skip, int64_t& bytes_skipped, CefRefPtr<CefResourceSkipCallback> callback
) {
    DCHECK (!CefCurrentlyOn (TID_UI) && !CefCurrentlyOn (TID_IO));
    bytes_skipped = -2;
    if (!this->m_range || bytes_to_skip < 0
	|| bytes_to_skip > this->m_range->offset + this->m_range->length - this->m_position) {
	return false;
    }
    this->m_contents->clear ();
    this->m_contents->seekg (bytes_to_skip, std::ios::cur);
    if (!*this->m_contents) {
	return false;
    }
    this->m_position += bytes_to_skip;
    bytes_skipped = bytes_to_skip;
    return true;
}

bool WPSchemeHandler::Read (
    void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefResourceReadCallback> callback
) {
    DCHECK (!CefCurrentlyOn (TID_UI) && !CefCurrentlyOn (TID_IO));

    bytes_read = 0;

    if (!this->m_range || this->m_position >= this->m_range->offset + this->m_range->length) {
	return false;
    }

    try {
	this->m_contents->read (
	    static_cast<std::istream::char_type*> (data_out),
	    std::min<int64_t> (bytes_to_read, this->m_range->offset + this->m_range->length - this->m_position)
	);
    } catch (std::ios::failure&) {
	bytes_read = -1;
	return false;
    }

    bytes_read = this->m_contents->gcount ();
    this->m_position += bytes_read;
    return bytes_read > 0;
}
