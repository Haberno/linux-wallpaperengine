/* Bounded Windows MediaEngine frame-server diagnostic, not a player.
 * Uses Windows system components; no bundled Microsoft binaries.
 * Output records API observations, never infers the internal VP/decoder route.
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <d3d10.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <propvarutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WIDTH 320
#define HEIGHT 180
#define RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)
#define CHECK(call) do { hr = (call); if (FAILED(hr)) { failure = #call; goto cleanup; } } while (0)

typedef struct Notify {
    IMFMediaEngineNotify iface;
    LONG refs;
    volatile LONG metadata, canplay, ended, error;
    volatile LONG event_error, event_extended;
} Notify;

static HRESULT WINAPI notify_query(IMFMediaEngineNotify *self, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IMFMediaEngineNotify))
        return E_NOINTERFACE;
    *out = self;
    InterlockedIncrement(&((Notify *)self)->refs);
    return S_OK;
}
static ULONG WINAPI notify_add(IMFMediaEngineNotify *self) {
    return InterlockedIncrement(&((Notify *)self)->refs);
}
static ULONG WINAPI notify_release(IMFMediaEngineNotify *self) {
    Notify *n = (Notify *)self;
    LONG refs = InterlockedDecrement(&n->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, n);
    return refs;
}
static HRESULT WINAPI notify_event(IMFMediaEngineNotify *self, DWORD event, DWORD_PTR p1, DWORD p2) {
    Notify *n = (Notify *)self;
    /* WAITFORSTABLE_STATE supplies a borrowed event. Do not close it. */
    if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE) SetEvent((HANDLE)p1);
    if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) InterlockedExchange(&n->metadata, 1);
    if (event == MF_MEDIA_ENGINE_EVENT_CANPLAY) InterlockedExchange(&n->canplay, 1);
    if (event == MF_MEDIA_ENGINE_EVENT_ENDED) InterlockedExchange(&n->ended, 1);
    if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
        InterlockedExchange(&n->event_error, (LONG)p1);
        InterlockedExchange(&n->event_extended, (LONG)p2);
        InterlockedExchange(&n->error, 1);
    }
    return S_OK;
}
static IMFMediaEngineNotifyVtbl notify_vtable = {
    notify_query, notify_add, notify_release, notify_event
};

static void json_wide(FILE *f, const WCHAR *s) {
    /* ASCII JSON with escaped UTF-16, preserving exact paths/device names. */
    fputc('"', f);
    for (; *s; ++s) {
        unsigned c = *s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 32 || c >= 127) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}
static void json_guid(FILE *f, const GUID *g) {
    WCHAR text[40];
    StringFromGUID2(g, text, 40);
    json_wide(f, text);
}
static void json_variant(FILE *f, const PROPVARIANT *v) {
    fprintf(f, "\"vt\":%u,\"value\":", v->vt);
    switch (v->vt) {
    case VT_UI4: fprintf(f, "%lu", v->ulVal); break;
    case VT_UI8: fprintf(f, "%llu", (unsigned long long)v->uhVal.QuadPart); break;
    case VT_I4: fprintf(f, "%ld", v->lVal); break;
    case VT_R8: if (_finite(v->dblVal)) fprintf(f, "%.17g", v->dblVal); else fputs("null", f); break;
    case VT_CLSID: json_guid(f, v->puuid); break;
    case VT_LPWSTR: json_wide(f, v->pwszVal); break;
    case VT_BLOB:
        fputc('"', f);
        for (ULONG i = 0; i < v->blob.cbSize; ++i) fprintf(f, "%02x", v->blob.pBlobData[i]);
        fputc('"', f); break;
    default: fputs("null", f); break;
    }
}

static void source_metadata(FILE *f, IMFMediaEngineEx *engine) {
    struct Attr { const char *name; const GUID *key; } attrs[] = {
        {"major_type", &MF_MT_MAJOR_TYPE}, {"subtype", &MF_MT_SUBTYPE},
        {"frame_size", &MF_MT_FRAME_SIZE}, {"frame_rate", &MF_MT_FRAME_RATE},
        {"primaries", &MF_MT_VIDEO_PRIMARIES}, {"transfer", &MF_MT_TRANSFER_FUNCTION},
        {"matrix", &MF_MT_YUV_MATRIX}, {"range", &MF_MT_VIDEO_NOMINAL_RANGE},
        {"mastering_max", &MF_MT_MAX_MASTERING_LUMINANCE},
        {"mastering_min", &MF_MT_MIN_MASTERING_LUMINANCE},
        {"max_cll", &MF_MT_MAX_LUMINANCE_LEVEL},
        {"max_fall", &MF_MT_MAX_FRAME_AVERAGE_LUMINANCE_LEVEL}
    };
    DWORD count = 0, flags = 0;
    HRESULT hr = IMFMediaEngineEx_GetNumberOfStreams(engine, &count);
    fprintf(f, "{\"kind\":\"source_stream_count\",\"hr\":\"%08lx\",\"count\":%lu}\n", hr, count);
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; ++i) {
        BOOL selected = FALSE;
        HRESULT sr = IMFMediaEngineEx_GetStreamSelection(engine, i, &selected);
        fprintf(f, "{\"kind\":\"stream_selection\",\"stream\":%lu,\"hr\":\"%08lx\",\"selected\":%s}\n", i, sr, selected ? "true" : "false");
        for (unsigned a = 0; a < sizeof(attrs)/sizeof(attrs[0]); ++a) {
            PROPVARIANT value = {0};
            HRESULT r = IMFMediaEngineEx_GetStreamAttribute(engine, i, attrs[a].key, &value);
            fprintf(f, "{\"kind\":\"source_attribute\",\"stream\":%lu,\"name\":\"%s\",\"hr\":\"%08lx\",", i, attrs[a].name, r);
            json_variant(f, &value); fputs("}\n", f); PropVariantClear(&value);
        }
    }
    hr = IMFMediaEngineEx_GetResourceCharacteristics(engine, &flags);
    fprintf(f, "{\"kind\":\"source_characteristics\",\"hr\":\"%08lx\",\"flags\":%lu}\n", hr, flags);
}

static void display_metadata(FILE *f, IDXGIAdapter1 *adapter) {
    DXGI_ADAPTER_DESC1 desc;
    HRESULT hr = IDXGIAdapter1_GetDesc1(adapter, &desc);
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER version = {0};
        HRESULT vr = IDXGIAdapter1_CheckInterfaceSupport(adapter, &IID_IDXGIDevice, &version);
        fprintf(f, "{\"kind\":\"adapter\",\"description\":"); json_wide(f, desc.Description);
        fprintf(f, ",\"vendor\":%u,\"device\":%u,\"subsystem\":%u,\"revision\":%u,\"luid_low\":%lu,\"luid_high\":%ld,\"flags\":%u,\"driver_version_hr\":\"%08lx\",\"driver_version\":\"%016llx\"}\n",
                desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision, desc.AdapterLuid.LowPart,
                desc.AdapterLuid.HighPart, desc.Flags, vr, (unsigned long long)version.QuadPart);
    }
    for (UINT i = 0; ; ++i) {
        IDXGIOutput *output = NULL; IDXGIOutput6 *output6 = NULL;
        hr = IDXGIAdapter1_EnumOutputs(adapter, i, &output);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        fprintf(f, "{\"kind\":\"output_query\",\"index\":%u,\"hr\":\"%08lx\"}\n", i, hr);
        if (FAILED(hr)) break;
        hr = IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput6, (void **)&output6);
        DXGI_OUTPUT_DESC1 d = {0};
        if (SUCCEEDED(hr)) hr = IDXGIOutput6_GetDesc1(output6, &d);
        fprintf(f, "{\"kind\":\"output_desc1\",\"index\":%u,\"hr\":\"%08lx\",\"device\":", i, hr);
        json_wide(f, d.DeviceName);
        fprintf(f, ",\"attached\":%d,\"color_space\":%u,\"bits\":%u,\"min_nits\":%.9g,\"max_nits\":%.9g,\"full_frame_nits\":%.9g,\"red\":[%.9g,%.9g],\"green\":[%.9g,%.9g],\"blue\":[%.9g,%.9g],\"white\":[%.9g,%.9g]}\n",
                d.AttachedToDesktop, d.ColorSpace, d.BitsPerColor, d.MinLuminance, d.MaxLuminance,
                d.MaxFullFrameLuminance, d.RedPrimary[0], d.RedPrimary[1], d.GreenPrimary[0],
                d.GreenPrimary[1], d.BluePrimary[0], d.BluePrimary[1], d.WhitePoint[0], d.WhitePoint[1]);
        RELEASE(output6); RELEASE(output);
    }
    UINT32 paths_count = 0, modes_count = 0;
    LONG r = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths_count, &modes_count);
    DISPLAYCONFIG_PATH_INFO *paths = NULL; DISPLAYCONFIG_MODE_INFO *modes = NULL;
    if (!r) {
        paths = calloc(paths_count, sizeof(*paths)); modes = calloc(modes_count, sizeof(*modes));
        if (!paths || !modes) r = ERROR_NOT_ENOUGH_MEMORY;
        else r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths_count, paths, &modes_count, modes, NULL);
    }
    fprintf(f, "{\"kind\":\"active_display_query\",\"win32_error\":%ld}\n", r);
    for (UINT32 i = 0; !r && i < paths_count; ++i) {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color = {0};
        DISPLAYCONFIG_SDR_WHITE_LEVEL white = {0};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color); color.header.adapterId = paths[i].targetInfo.adapterId;
        color.header.id = paths[i].targetInfo.id;
        white.header = color.header; white.header.size = sizeof(white);
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        LONG cr = DisplayConfigGetDeviceInfo(&color.header), wr = DisplayConfigGetDeviceInfo(&white.header);
        fprintf(f, "{\"kind\":\"active_display\",\"luid_low\":%lu,\"luid_high\":%ld,\"target_id\":%u,\"color_error\":%ld,\"color_flags\":%u,\"color_encoding\":%u,\"bits\":%u,\"white_error\":%ld,\"white_raw\":%lu}\n",
                color.header.adapterId.LowPart, color.header.adapterId.HighPart, color.header.id,
                cr, color.value, color.colorEncoding, color.bitsPerColorChannel, wr, white.SDRWhiteLevel);
    }
    free(paths); free(modes);
}

static float half_float(unsigned h) {
    unsigned sign = (h & 0x8000u) << 16, exp = (h >> 10) & 31u, mant = h & 1023u;
    union { unsigned u; float f; } value;
    if (!exp) return (h & 0x8000u ? -1.0f : 1.0f) * (float)mant / 16777216.0f;
    value.u = sign | (exp == 31 ? 0x7f800000u : (exp + 112u) << 23) | (mant << 13);
    return value.f;
}
static void sample(FILE *csv, unsigned frame, LONGLONG pts, const char *kind,
                   unsigned index, unsigned x, unsigned y, const D3D11_MAPPED_SUBRESOURCE *map) {
    const unsigned short *p = (const unsigned short *)((const BYTE *)map->pData + y * map->RowPitch) + 4*x;
    fprintf(csv, "%u,%lld,%s,%u,%u,%u", frame, (long long)pts, kind, index, x, y);
    for (unsigned c = 0; c < 4; ++c) fprintf(csv, ",%04x,%.9g", p[c], half_float(p[c]));
    fputc('\n', csv);
}

static int probe_main(int argc, WCHAR **argv) {
    HRESULT hr = S_OK, shutdown_hr = S_OK, mf_shutdown_hr = S_OK;
    const char *failure = "none"; int result = 1, com_started = 0, mf_started = 0;
    FILE *meta = NULL, *csv = NULL, *frames = NULL;
    Notify *notify = NULL;
    IDXGIFactory1 *factory = NULL; IDXGIAdapter1 *adapter = NULL;
    ID3D11Device *device = NULL, *owner = NULL;
    ID3D11DeviceContext *context = NULL, *owner_context = NULL;
    ID3D10Multithread *multithread = NULL;
    ID3D11Texture2D *target = NULL, *opened = NULL, *staging = NULL;
    IDXGIResource *resource = NULL; IDXGIKeyedMutex *producer_mutex = NULL, *owner_mutex = NULL;
    IMFDXGIDeviceManager *manager = NULL; IMFAttributes *attrs = NULL;
    IMFMediaEngineClassFactory *media_factory = NULL; IMFMediaEngine *engine = NULL;
    IMFMediaEngineEx *extended = NULL;
    BSTR source = NULL; HANDLE shared = NULL;
    unsigned frame_count = 0, phase_mask = 0; LONGLONG prior_pts = -1;
    BOOL producer_locked = FALSE, owner_locked = FALSE, mapped = FALSE;
    D3D_FEATURE_LEVEL feature = 0, owner_feature = 0;
    UINT token = 0;
    static WCHAR path[32768]; /* Main-thread-only; avoid a large cross-CRT stack frame. */
    if (argc < 3 || argc > 4) return 2;
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) return 4;
    /* A new directory is mandatory: no existing evidence can be overwritten. */
    if (!CreateDirectoryW(argv[2], NULL)) return 3;
    const WCHAR *names[] = {L"metadata.jsonl", L"samples.csv", L"frames.rgba16f"};
    FILE **files[] = {&meta, &csv, &frames};
    for (unsigned i = 0; i < 3; ++i) {
        if (wcslen(argv[2]) + wcslen(names[i]) + 2 >= sizeof(path)/sizeof(path[0])) { failure = "output path too long"; goto cleanup; }
        wcscpy(path, argv[2]); wcscat(path, L"\\"); wcscat(path, names[i]);
        *files[i] = _wfopen(path, i == 2 ? L"wb" : L"w");
        if (!*files[i]) { failure = "open output"; goto cleanup; }
    }
    fprintf(meta, "{\"kind\":\"contract\",\"input\":"); json_wide(meta, argv[1]);
    fputs(",\"startup_dxgi\":87,\"destination_dxgi\":10,\"create_flags\":22,\"shared_key\":0,\"width\":320,\"height\":180,\"raw_layout\":\"frames concatenated; top-down tight RGBA binary16 little-endian\",\"internal_processing_route\":\"not observable through queried public MediaEngine APIs\",\"display_affinity\":\"no window/output override; observed displays do not establish selected sink metadata\"}\n", meta);
    fputs("frame,pts_100ns,kind,index,x,y,r_half,r,g_half,g,b_half,b,a_half,a\n", csv);
    CHECK(CoInitializeEx(NULL, COINIT_MULTITHREADED)); com_started = 1;
    CHECK(MFStartup(0x20070, MFSTARTUP_LITE)); mf_started = 1;
    CHECK(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory));
    WCHAR *end = NULL; unsigned long adapter_index = argc == 4 ? wcstoul(argv[3], &end, 10) : 0;
    if (argc == 4 && (!argv[3][0] || *end || adapter_index > 32)) { failure = "invalid adapter index"; goto cleanup; }
    CHECK(IDXGIFactory1_EnumAdapters1(factory, (UINT)adapter_index, &adapter));
    fputs("{\"kind\":\"display_snapshot\",\"stage\":\"before\"}\n", meta);
    display_metadata(meta, adapter);
    CHECK(D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
          D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &device, &feature, &context));
    CHECK(ID3D11Device_QueryInterface(device, &IID_ID3D10Multithread, (void **)&multithread));
    ID3D10Multithread_SetMultithreadProtected(multithread, TRUE);
    CHECK(D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
          D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &owner, &owner_feature, &owner_context));
    D3D11_TEXTURE2D_DESC td = {0};
    td.Width = WIDTH; td.Height = HEIGHT; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    CHECK(ID3D11Device_CreateTexture2D(owner, &td, NULL, &target));
    CHECK(ID3D11Texture2D_QueryInterface(target, &IID_IDXGIResource, (void **)&resource));
    CHECK(IDXGIResource_GetSharedHandle(resource, &shared)); /* Legacy handle is not CloseHandle-owned. */
    CHECK(ID3D11Device_OpenSharedResource(device, shared, &IID_ID3D11Texture2D, (void **)&opened));
    CHECK(ID3D11Texture2D_QueryInterface(opened, &IID_IDXGIKeyedMutex, (void **)&producer_mutex));
    CHECK(ID3D11Texture2D_QueryInterface(target, &IID_IDXGIKeyedMutex, (void **)&owner_mutex));
    ID3D11Texture2D_GetDesc(opened, &td);
    fprintf(meta, "{\"kind\":\"actual_destination\",\"format\":%u,\"width\":%u,\"height\":%u,\"mips\":%u,\"array\":%u,\"samples\":%u,\"bind\":%u,\"misc\":%u,\"feature\":%u,\"owner_feature\":%u}\n",
            td.Format, td.Width, td.Height, td.MipLevels, td.ArraySize, td.SampleDesc.Count, td.BindFlags, td.MiscFlags, feature, owner_feature);
    if (td.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || td.Width != WIDTH || td.Height != HEIGHT) { failure = "destination contract"; goto cleanup; }
    td.BindFlags = 0; td.MiscFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(ID3D11Device_CreateTexture2D(owner, &td, NULL, &staging));
    CHECK(MFCreateDXGIDeviceManager(&token, &manager));
    CHECK(IMFDXGIDeviceManager_ResetDevice(manager, (IUnknown *)device, token));
    CHECK(MFCreateAttributes(&attrs, 3));
    notify = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*notify));
    if (!notify) { hr = E_OUTOFMEMORY; failure = "notify allocation"; goto cleanup; }
    notify->iface.lpVtbl = &notify_vtable; notify->refs = 1;
    CHECK(IMFAttributes_SetUnknown(attrs, &MF_MEDIA_ENGINE_CALLBACK, (IUnknown *)notify));
    CHECK(IMFAttributes_SetUnknown(attrs, &MF_MEDIA_ENGINE_DXGI_MANAGER, (IUnknown *)manager));
    CHECK(IMFAttributes_SetUINT32(attrs, &MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM));
    CHECK(CoCreateInstance(&CLSID_MFMediaEngineClassFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMFMediaEngineClassFactory, (void **)&media_factory));
    CHECK(IMFMediaEngineClassFactory_CreateInstance(media_factory,
          MF_MEDIA_ENGINE_WAITFORSTABLE_STATE | MF_MEDIA_ENGINE_FORCEMUTE | MF_MEDIA_ENGINE_DISABLE_LOCAL_PLUGINS,
          attrs, &engine));
    CHECK(IMFMediaEngine_QueryInterface(engine, &IID_IMFMediaEngineEx, (void **)&extended));
    CHECK(IMFMediaEngine_SetAutoPlay(engine, FALSE));
    CHECK(IMFMediaEngine_SetLoop(engine, FALSE)); /* One finite 40-frame diagnostic. */
    source = SysAllocString(argv[1]);
    if (!source) { hr = E_OUTOFMEMORY; failure = "source allocation"; goto cleanup; }
    CHECK(IMFMediaEngine_SetSource(engine, source));
    CHECK(IMFMediaEngine_Load(engine));
    ULONGLONG deadline = GetTickCount64() + 20000;
    BOOL started = FALSE, got_metadata = FALSE;
    while (GetTickCount64() < deadline && frame_count < 120) {
        if (InterlockedCompareExchange(&notify->error, 0, 0)) { failure = "media engine error event"; hr = E_FAIL; goto cleanup; }
        if (!got_metadata && InterlockedCompareExchange(&notify->metadata, 0, 0)) {
            DWORD w = 0, h = 0; CHECK(IMFMediaEngine_GetNativeVideoSize(engine, &w, &h));
            fprintf(meta, "{\"kind\":\"native_size\",\"width\":%lu,\"height\":%lu,\"duration\":%.17g}\n", w, h, IMFMediaEngine_GetDuration(engine));
            if (w != WIDTH || h != HEIGHT) { failure = "fixture must be 320x180"; goto cleanup; }
            source_metadata(meta, extended); got_metadata = TRUE; fflush(meta);
        }
        if (!started && InterlockedCompareExchange(&notify->canplay, 0, 0)) { CHECK(IMFMediaEngine_Play(engine)); started = TRUE; }
        LONGLONG pts = 0;
        hr = started ? IMFMediaEngine_OnVideoStreamTick(engine, &pts) : S_FALSE;
        if (FAILED(hr)) { failure = "OnVideoStreamTick"; goto cleanup; }
        if (hr == S_OK && pts != prior_pts) {
            /* AcquireSync may return positive WAIT_TIMEOUT/WAIT_ABANDONED. Only S_OK owns the mutex. */
            hr = IDXGIKeyedMutex_AcquireSync(producer_mutex, 0, 1000);
            if (hr != S_OK) { failure = "producer AcquireSync"; goto cleanup; }
            producer_locked = TRUE;
            MFVideoNormalizedRect src = {0, 0, 1, 1}; RECT dst = {0, 0, WIDTH, HEIGHT}; MFARGB border = {0, 0, 0, 255};
            CHECK(IMFMediaEngine_TransferVideoFrame(engine, (IUnknown *)opened, &src, &dst, &border));
            ID3D11DeviceContext_Flush(context);
            CHECK(IDXGIKeyedMutex_ReleaseSync(producer_mutex, 0)); producer_locked = FALSE;
            hr = IDXGIKeyedMutex_AcquireSync(owner_mutex, 0, 1000);
            if (hr != S_OK) { failure = "owner AcquireSync"; goto cleanup; }
            owner_locked = TRUE;
            ID3D11DeviceContext_CopyResource(owner_context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
            ID3D11DeviceContext_Flush(owner_context);
            D3D11_MAPPED_SUBRESOURCE map = {0};
            CHECK(ID3D11DeviceContext_Map(owner_context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)); mapped = TRUE;
            for (unsigned y = 0; y < HEIGHT; ++y)
                if (fwrite((BYTE *)map.pData + y*map.RowPitch, 8, WIDTH, frames) != WIDTH) { failure = "write raw frame"; goto cleanup; }
            for (unsigned i = 0; i < 10; ++i) sample(csv, frame_count, pts, "band", i, 16+32*i, 80, &map);
            float marker[4];
            for (unsigned i = 0; i < 4; ++i) {
                sample(csv, frame_count, pts, "marker", i, 40+80*i, 170, &map);
                const unsigned short *p = (const unsigned short *)((BYTE *)map.pData + 170*map.RowPitch) + 4*(40+80*i);
                marker[i] = half_float(p[0]);
            }
            for (unsigned i = 0; i < 4; ++i) sample(csv, frame_count, pts, "checker", i, 4+8*i, 4, &map);
            unsigned winner = 0; for (unsigned i = 1; i < 4; ++i) if (marker[i] > marker[winner]) winner = i;
            float others = 0; for (unsigned i = 0; i < 4; ++i) if (i != winner) others += fabsf(marker[i]);
            if (_finite(marker[winner]) && marker[winner] > others + 0.0001f) phase_mask |= 1u << winner;
            ID3D11DeviceContext_Unmap(owner_context, (ID3D11Resource *)staging, 0); mapped = FALSE;
            CHECK(IDXGIKeyedMutex_ReleaseSync(owner_mutex, 0)); owner_locked = FALSE;
            fprintf(meta, "{\"kind\":\"frame\",\"index\":%u,\"pts_100ns\":%lld,\"row_pitch\":%u}\n", frame_count, (long long)pts, map.RowPitch);
            if (ferror(csv) || ferror(meta)) { failure = "write samples or metadata"; goto cleanup; }
            ++frame_count; prior_pts = pts;
        }
        if (InterlockedCompareExchange(&notify->ended, 0, 0)) break;
        Sleep(1); /* Offscreen diagnostic: polls video ticks, never presents a desktop surface. */
    }
    if (!got_metadata || frame_count < 20 || phase_mask != 15 || !InterlockedCompareExchange(&notify->ended, 0, 0)) {
        failure = "incomplete fixture/timeout/marker readiness"; hr = E_FAIL; goto cleanup;
    }
    result = 0;
cleanup:
    if (mapped) ID3D11DeviceContext_Unmap(owner_context, (ID3D11Resource *)staging, 0);
    if (producer_locked) IDXGIKeyedMutex_ReleaseSync(producer_mutex, 0);
    if (owner_locked) IDXGIKeyedMutex_ReleaseSync(owner_mutex, 0);
    if (engine) {
        if (meta && notify) fprintf(meta, "{\"kind\":\"event_status\",\"metadata\":%ld,\"canplay\":%ld,\"ended\":%ld,\"error\":%ld,\"extended\":\"%08lx\"}\n",
                InterlockedCompareExchange(&notify->metadata, 0, 0), InterlockedCompareExchange(&notify->canplay, 0, 0),
                InterlockedCompareExchange(&notify->ended, 0, 0), InterlockedCompareExchange(&notify->event_error, 0, 0),
                InterlockedCompareExchange(&notify->event_extended, 0, 0));
        IMFMediaError *error = NULL;
        if (meta && SUCCEEDED(IMFMediaEngine_GetError(engine, &error)) && error) {
            fprintf(meta, "{\"kind\":\"media_error\",\"code\":%u,\"extended\":\"%08lx\"}\n", IMFMediaError_GetErrorCode(error), IMFMediaError_GetExtendedErrorCode(error));
            RELEASE(error);
        }
        if (meta && extended) for (unsigned i = 0; i <= 7; ++i) {
            PROPVARIANT value = {0}; HRESULT r = IMFMediaEngineEx_GetStatistics(extended, (MF_MEDIA_ENGINE_STATISTIC)i, &value);
            fprintf(meta, "{\"kind\":\"statistic\",\"id\":%u,\"hr\":\"%08lx\",", i, r);
            json_variant(meta, &value); fputs("}\n", meta); PropVariantClear(&value);
        }
    }
    if (meta) {
        const WCHAR *modules[] = {L"mfmediaengine.dll", L"mfsvr.dll", L"msvproc.dll", L"mfplat.dll", L"d3d11.dll", L"dxgi.dll"};
        for (unsigned i = 0; i < sizeof(modules)/sizeof(modules[0]); ++i) {
            HMODULE module = GetModuleHandleW(modules[i]); path[0] = 0;
            if (module) GetModuleFileNameW(module, path, sizeof(path)/sizeof(path[0]));
            fputs("{\"kind\":\"loaded_component\",\"name\":", meta); json_wide(meta, modules[i]);
            fprintf(meta, ",\"loaded\":%s,\"path\":", module ? "true" : "false"); json_wide(meta, path); fputs("}\n", meta);
        }
        if (adapter) {
            fputs("{\"kind\":\"display_snapshot\",\"stage\":\"after\"}\n", meta);
            display_metadata(meta, adapter);
        }
    }
    if (engine) {
        IMFMediaEngine_Pause(engine);
        shutdown_hr = IMFMediaEngine_Shutdown(engine);
    }
    SysFreeString(source);
    RELEASE(extended); RELEASE(engine); RELEASE(media_factory); RELEASE(attrs); RELEASE(manager);
    if (notify) notify_release(&notify->iface);
    RELEASE(producer_mutex); RELEASE(owner_mutex); RELEASE(resource); RELEASE(staging); RELEASE(opened); RELEASE(target);
    RELEASE(multithread); RELEASE(context); RELEASE(owner_context); RELEASE(device); RELEASE(owner); RELEASE(adapter); RELEASE(factory);
    if (mf_started) mf_shutdown_hr = MFShutdown();
    if (com_started) CoUninitialize();
    if (FAILED(shutdown_hr) || FAILED(mf_shutdown_hr)) result = 1;
    if (frames && fclose(frames)) { result = 1; failure = "close raw frame output"; }
    if (csv && fclose(csv)) { result = 1; failure = "close sample output"; }
    if (meta) {
        fprintf(meta, "{\"kind\":\"result\",\"capture_complete\":%s,\"frames\":%u,\"phase_mask\":%u,\"failure\":\"%s\",\"last_hr\":\"%08lx\",\"engine_shutdown_hr\":\"%08lx\",\"mf_shutdown_hr\":\"%08lx\",\"windows_policy_acceptance\":false}\n",
                result == 0 ? "true" : "false", frame_count, phase_mask, failure, hr, shutdown_hr, mf_shutdown_hr);
        if (fclose(meta)) result = 1;
    }
    return result;
}

int wmain(int argc, WCHAR **argv) { return probe_main(argc, argv); }
#ifdef PROBE_CROSS_ENTRY
int _fltused;
void mainCRTStartup(void) {
    int argc = 0; WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = argv ? probe_main(argc, argv) : 2;
    if (argv) LocalFree(argv);
    ExitProcess((UINT)result);
}
#endif
