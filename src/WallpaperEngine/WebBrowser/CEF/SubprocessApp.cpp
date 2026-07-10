#include "SubprocessApp.h"
#include "WPSchemeHandlerFactory.h"
#include "WallpaperEngine/Data/Model/Project.h"

using namespace WallpaperEngine::WebBrowser::CEF;

SubprocessApp::SubprocessApp (WallpaperEngine::Application::WallpaperApplication& application) :
    m_application (&application) { }

void SubprocessApp::OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) {
    // one fixed scheme for every wallpaper; the workshop id travels in the URL host
    // (wp://<id>/<file>), so wallpapers swapped in later need no extra registration
    registrar->AddCustomScheme (
	WPENGINE_SCHEME, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_FETCH_ENABLED
    );
}

const WallpaperEngine::Application::WallpaperApplication& SubprocessApp::getApplication () const {
    return *this->m_application;
}

void SubprocessApp::OnContextCreated (
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context
) {
    // Wallpaper Engine web API shim. Defines the listener-registration functions a
    // web wallpaper expects and small __wp* entry points the browser process calls
    // (via ExecuteJavaScript) to deliver audio, media and property updates.
    static const char* shim = R"JS(
(function(){
  if (window.__wpBridge) return; window.__wpBridge = true;
  var A=[],MP=[],PB=[],TH=[],TL=[];
  function fire(a,d){for(var i=0;i<a.length;i++){try{a[i](d);}catch(e){}}}
  window.wallpaperRegisterAudioListener=function(cb){if(typeof cb==='function')A.push(cb);};
  window.wallpaperRegisterMediaPropertiesListener=function(cb){if(typeof cb==='function')MP.push(cb);};
  window.wallpaperRegisterMediaPlaybackListener=function(cb){if(typeof cb==='function')PB.push(cb);};
  window.wallpaperRegisterMediaThumbnailListener=function(cb){if(typeof cb==='function')TH.push(cb);};
  window.wallpaperRegisterMediaTimelineListener=function(cb){if(typeof cb==='function')TL.push(cb);};
  window.__wpAudio=function(d){fire(A,d);};
  window.__wpMediaProps=function(d){fire(MP,d);};
  window.__wpMediaPlayback=function(d){fire(PB,d);};
  window.__wpMediaThumb=function(d){fire(TH,d);};
  window.__wpMediaTimeline=function(d){fire(TL,d);};
  window.__wpApplyProps=function(p){var l=window.wallpaperPropertyListener;if(l&&l.applyUserProperties){try{l.applyUserProperties(p);}catch(e){}}};
  window.__wpApplyGeneral=function(p){var l=window.wallpaperPropertyListener;if(l&&l.applyGeneralProperties){try{l.applyGeneralProperties(p);}catch(e){}}};
})();
)JS";
    frame->ExecuteJavaScript (shim, frame->GetURL (), 0);
}