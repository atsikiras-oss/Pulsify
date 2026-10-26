#pragma once
// ============================================================================
//  SmtcBridge — Windows System Media Transport Controls
//
//  Hooks Pulsify into the OS media overlay so:
//    • Headphone / keyboard media buttons (play/pause/next/prev) work globally
//    • The Windows 11 taskbar mini-player shows title, artist and cover art
//    • The lock-screen media card shows playback info
//
//  Uses plain Win32 COM with manually defined vtables — works with both MSVC
//  and MinGW-w64 (no WinRT C++/WinRT SDK headers required).
//  On non-Windows the class compiles to empty stubs.
// ============================================================================
#include <QtCore>
#include <QImage>
#include <QDir>

// ============================================================================
//  Windows-only plumbing
// ============================================================================
#ifdef Q_OS_WIN

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <winstring.h>          // WindowsCreateString / WindowsDeleteString
#include <roapi.h>              // RoInitialize / RoGetActivationFactory / RoActivateInstance
#include <inspectable.h>        // IInspectable
#include <windows.foundation.h> // ABI::Windows::Foundation::TimeSpan / IReference / IAsyncAction
// INITGUID causes DEFINE_GUID in the interop header to emit a definition.
#ifndef INITGUID
#define INITGUID
#define SMTC_INITGUID_SET
#endif
#include <systemmediatransportcontrolsinterop.h>
#ifdef SMTC_INITGUID_SET
#undef INITGUID
#undef SMTC_INITGUID_SET
#endif
#include <wrl/client.h>         // Microsoft::WRL::ComPtr

using Microsoft::WRL::ComPtr;

// ---- Enumerations ----------------------------------------------------------
namespace SMTC_Enums {
    enum MediaPlaybackStatus  { Closed=0, Changing=1, Stopped=2, Playing=3, Paused=4 };
    enum MediaPlaybackType    { Unknown=0, Music=1, Video=2, Image=3 };
    enum SMTCButton           { Play=0, Pause=1, Stop=2, Record=3, FF=4, RW=5, Next=6, Prev=7 };
}

// ---- Forward-declared COM interfaces (GUIDs from Windows SDK docs) ---------

// ISystemMediaTransportControlsButtonPressedEventArgs
// {b996a7bb-afe7-4538-8d43-b6de45b3e3a3}
DEFINE_GUID(IID_ISystemMediaTransportControlsButtonPressedEventArgs_,
    0xb996a7bb,0xafe7,0x4538,0x8d,0x43,0xb6,0xde,0x45,0xb3,0xe3,0xa3);
MIDL_INTERFACE("b996a7bb-afe7-4538-8d43-b6de45b3e3a3")
ISmtcButtonArgs : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Button(SMTC_Enums::SMTCButton* value) = 0;
};

// ISystemMediaTransportControls
// {99fa3ff4-1742-42a6-902e-087d41f965ec}
MIDL_INTERFACE("99fa3ff4-1742-42a6-902e-087d41f965ec")
ISmtcCore : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(SMTC_Enums::MediaPlaybackStatus*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(SMTC_Enums::MediaPlaybackStatus)  = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(IInspectable**)                   = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(int*)                                 = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean*)                              = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean)                               = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean*)                          = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean*)                          = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean*)                         = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean)                          = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean*)                        = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean)                         = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean*)                   = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean)                    = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean*)                        = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean)                         = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean*)                          = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean*)                      = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean)                       = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean*)                     = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean)                      = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean*)                   = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean)                    = 0;
    // add_ButtonPressed  {token type is INT64 / EventRegistrationToken}
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(IUnknown* handler, INT64* token)  = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(INT64 token)                   = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(IUnknown* handler, INT64* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(INT64 token)                  = 0;
};

// ISystemMediaTransportControls2 — adds UpdateTimelineProperties
// {ea37feec-1321-4a6a-8b5e-e5b78a7c9eb0}
MIDL_INTERFACE("ea37feec-1321-4a6a-8b5e-e5b78a7c9eb0")
ISmtcCore2 : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(int*)                            = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoRepeatMode(int)                             = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShuffleEnabled(boolean*)                        = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShuffleEnabled(boolean)                         = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(double*)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackRate(double)                            = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTimelineProperties(IInspectable* props)       = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackPositionChangeRequested(IUnknown*, INT64*)  = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackPositionChangeRequested(INT64)           = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackRateChangeRequested(IUnknown*, INT64*)      = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackRateChangeRequested(INT64)               = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ShuffleEnabledChangeRequested(IUnknown*, INT64*)    = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ShuffleEnabledChangeRequested(INT64)             = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AutoRepeatModeChangeRequested(IUnknown*, INT64*)    = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AutoRepeatModeChangeRequested(INT64)             = 0;
};

// ISystemMediaTransportControlsDisplayUpdater
// {ba6a89a6-1080-4e37-b0bf-b568ef43a7dc}
MIDL_INTERFACE("ba6a89a6-1080-4e37-b0bf-b568ef43a7dc")
ISmtcDisplayUpdater : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Type(SMTC_Enums::MediaPlaybackType*)           = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(SMTC_Enums::MediaPlaybackType)             = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING*)                            = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING)                             = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IUnknown**)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(IUnknown*)                            = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IInspectable**)                 = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(IInspectable**)                 = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(IInspectable**)                 = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(SMTC_Enums::MediaPlaybackType, IUnknown*, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAll()                                          = 0;
    virtual HRESULT STDMETHODCALLTYPE Update()                                            = 0;
};

// IMusicDisplayProperties
// {6bbf0c59-d0a0-4d26-92a0-f978e1d18e7e}
MIDL_INTERFACE("6bbf0c59-d0a0-4d26-92a0-f978e1d18e7e")
ISmtcMusicProps : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*)                                 = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING)                                  = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*)                           = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING)                            = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*)                                = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING)                                 = 0;
};

// ISystemMediaTransportControlsTimelineProperties
// {5125316a-c3a2-475b-8507-93534dc88f15}
MIDL_INTERFACE("5125316a-c3a2-475b-8507-93534dc88f15")
ISmtcTimeline : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(ABI::Windows::Foundation::TimeSpan*)   = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StartTime(ABI::Windows::Foundation::TimeSpan)    = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(ABI::Windows::Foundation::TimeSpan*)     = 0;
    virtual HRESULT STDMETHODCALLTYPE put_EndTime(ABI::Windows::Foundation::TimeSpan)      = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinSeekTime(ABI::Windows::Foundation::TimeSpan)  = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MaxSeekTime(ABI::Windows::Foundation::TimeSpan)  = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(ABI::Windows::Foundation::TimeSpan*)    = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Position(ABI::Windows::Foundation::TimeSpan)     = 0;
};

// IRandomAccessStreamReference — for Thumbnail
// {33ee3134-1dd6-4e3a-8067-d1c162e8642b}
MIDL_INTERFACE("33ee3134-1dd6-4e3a-8067-d1c162e8642b")
ISmtcStreamRef : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(IUnknown** op) = 0;
};

// IRandomAccessStreamReferenceStatics
// {857309dc-3fbf-4e7a-b5a9-a3d9c7b26fce}
MIDL_INTERFACE("857309dc-3fbf-4e7a-b5a9-a3d9c7b26fce")
ISmtcStreamRefStatics : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateFromFile(IUnknown*, ISmtcStreamRef**)     = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromUri(IUnknown*, ISmtcStreamRef**)      = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromStream(IUnknown*, ISmtcStreamRef**)   = 0;
};

// IUriRuntimeClass — WinRT Uri object; we only need IUnknown here
// IUriRuntimeClassFactory {44A9796F-723E-4FDF-A218-033E75B0C084}
MIDL_INTERFACE("44A9796F-723E-4FDF-A218-033E75B0C084")
ISmtcUriFac : public IInspectable {
public:
    virtual HRESULT STDMETHODCALLTYPE CreateUri(HSTRING, IInspectable**)            = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWithRelativeUri(HSTRING, HSTRING, IInspectable**) = 0;
};

// ---- Typed event handler (non-template version for button press) -----------
// TypedEventHandler<ISystemMediaTransportControls*, ISmtcButtonArgs*>
// IID for this specific instantiation: {9de1c534-6ae1-11e0-84e1-18a905bcea72}
MIDL_INTERFACE("9de1c534-6ae1-11e0-84e1-18a905bcea72")
ISmtcButtonHandler : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ISmtcCore* sender, ISmtcButtonArgs* args) = 0;
};

// ---- HSTRING helpers -------------------------------------------------------
namespace SmtcUtil {
    inline HSTRING toHS(const QString& s)
    {
        HSTRING h = nullptr;
        WindowsCreateString(reinterpret_cast<const wchar_t*>(s.utf16()),
                            static_cast<UINT32>(s.length()), &h);
        return h;
    }
    inline void freeHS(HSTRING h) { if (h) WindowsDeleteString(h); }

    inline ABI::Windows::Foundation::TimeSpan msToTs(qint64 ms)
    {
        ABI::Windows::Foundation::TimeSpan ts;
        ts.Duration = ms * 10000LL;   // 100-ns ticks
        return ts;
    }

    template<typename T>
    inline bool activateFactory(const wchar_t* name, ComPtr<T>& out, REFIID iid)
    {
        HSTRING cls = nullptr;
        if (FAILED(WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &cls)))
            return false;
        HRESULT hr = RoGetActivationFactory(cls, iid,
                                            reinterpret_cast<void**>(out.ReleaseAndGetAddressOf()));
        WindowsDeleteString(cls);
        return SUCCEEDED(hr);
    }

    template<typename T>
    inline bool activateInstance(const wchar_t* name, ComPtr<T>& out, REFIID iid)
    {
        HSTRING cls = nullptr;
        if (FAILED(WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &cls)))
            return false;
        IInspectable* raw = nullptr;
        HRESULT hr = RoActivateInstance(cls, &raw);
        WindowsDeleteString(cls);
        if (FAILED(hr) || !raw) return false;
        hr = raw->QueryInterface(iid, reinterpret_cast<void**>(out.ReleaseAndGetAddressOf()));
        raw->Release();
        return SUCCEEDED(hr);
    }
}

#endif // Q_OS_WIN

// ---- IID constants — defined inline so they link in any TU ---------------
static constexpr GUID IID_ISmtcCore_        = {0x99fa3ff4,0x1742,0x42a6,{0x90,0x2e,0x08,0x7d,0x41,0xf9,0x65,0xec}};
static constexpr GUID IID_ISmtcCore2_       = {0xea37feec,0x1321,0x4a6a,{0x8b,0x5e,0xe5,0xb7,0x8a,0x7c,0x9e,0xb0}};
static constexpr GUID IID_ISmtcDisplayUpd_  = {0xba6a89a6,0x1080,0x4e37,{0xb0,0xbf,0xb5,0x68,0xef,0x43,0xa7,0xdc}};
static constexpr GUID IID_ISmtcMusicProps_  = {0x6bbf0c59,0xd0a0,0x4d26,{0x92,0xa0,0xf9,0x78,0xe1,0xd1,0x8e,0x7e}};
static constexpr GUID IID_ISmtcTimeline_    = {0x5125316a,0xc3a2,0x475b,{0x85,0x07,0x93,0x53,0x4d,0xc8,0x8f,0x15}};
static constexpr GUID IID_ISmtcStreamRef_   = {0x33ee3134,0x1dd6,0x4e3a,{0x80,0x67,0xd1,0xc1,0x62,0xe8,0x64,0x2b}};
static constexpr GUID IID_ISmtcStreamRefSt_ = {0x857309dc,0x3fbf,0x4e7a,{0xb5,0xa9,0xa3,0xd9,0xc7,0xb2,0x6f,0xce}};
static constexpr GUID IID_ISmtcUriFac_      = {0x44A9796F,0x723E,0x4FDF,{0xA2,0x18,0x03,0x3E,0x75,0xB0,0xC0,0x84}};
// IID for ISystemMediaTransportControlsInterop — local copy avoids ODR conflict with
// the extern declaration in systemmediatransportcontrolsinterop.h.
static constexpr GUID IID_ISmtcInterop_     = {0xddb0472d,0xc911,0x4a1f,{0x86,0xd9,0xdc,0x3d,0x71,0xa9,0x5f,0x5a}};

// Tell MinGW WRL what IID to use when calling m_smtc.As<ISmtcCore2>()
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ISmtcCore2, 0xea37feec,0x1321,0x4a6a,0x8b,0x5e,0xe5,0xb7,0x8a,0x7c,0x9e,0xb0)
#endif

// ============================================================================
//  Public class
// ============================================================================
class SmtcBridge : public QObject
{
    Q_OBJECT
public:
    explicit SmtcBridge(QObject* parent = nullptr);
    ~SmtcBridge() override;

    void setTrack(const QString& title, const QString& artist, const QString& album,
                  const QImage& cover, qint64 durationMs);
    void setPlaybackState(bool playing, bool paused);
    void setPosition(qint64 ms);

signals:
    void playPauseRequested();
    void nextRequested();
    void prevRequested();
    void seekRequested(qint64 ms);

private:
#ifdef Q_OS_WIN
    // ---- Button-press COM handler (inner class) ----------------------------
    class ButtonHandler final : public ISmtcButtonHandler
    {
    public:
        explicit ButtonHandler(SmtcBridge* b) : m_bridge(b) {}
        // IUnknown
        STDMETHOD_(ULONG, AddRef)()  override { return ++m_refs; }
        STDMETHOD_(ULONG, Release)() override
        {
            ULONG r = --m_refs;
            if (r == 0) delete this;
            return r;
        }
        STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
        {
            static const GUID btn_iid = {0x9de1c534,0x6ae1,0x11e0,{0x84,0xe1,0x18,0xa9,0x05,0xbc,0xea,0x72}};
            if (riid == IID_IUnknown || riid == btn_iid)
            { *ppv = static_cast<ISmtcButtonHandler*>(this); AddRef(); return S_OK; }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        // ISmtcButtonHandler
        STDMETHOD(Invoke)(ISmtcCore*, ISmtcButtonArgs* args) override
        {
            SMTC_Enums::SMTCButton btn = SMTC_Enums::Play;
            if (SUCCEEDED(args->get_Button(&btn))) {
                SmtcBridge* b = m_bridge;
                if (btn == SMTC_Enums::Play || btn == SMTC_Enums::Pause)
                    QMetaObject::invokeMethod(b, [b]{ emit b->playPauseRequested(); }, Qt::QueuedConnection);
                else if (btn == SMTC_Enums::Next)
                    QMetaObject::invokeMethod(b, [b]{ emit b->nextRequested(); }, Qt::QueuedConnection);
                else if (btn == SMTC_Enums::Prev)
                    QMetaObject::invokeMethod(b, [b]{ emit b->prevRequested(); }, Qt::QueuedConnection);
            }
            return S_OK;
        }
    private:
        SmtcBridge* m_bridge;
        ULONG       m_refs = 1;
    };

    void init();
    void pushArt(const QImage& img);

    ComPtr<ISmtcCore>            m_smtc;
    ComPtr<ISmtcDisplayUpdater>  m_upd;
    ComPtr<ISmtcMusicProps>      m_music;
    INT64                        m_btnToken = 0;
    bool                         m_ready = false;
    qint64                       m_durationMs = 0;
    qint64                       m_posMs = 0;
    QTimer                       m_posTimer;
#endif
};

// ============================================================================
//  Implementation — all inline so this header can be included in multiple TUs
// ============================================================================
#ifdef Q_OS_WIN

inline SmtcBridge::SmtcBridge(QObject* parent) : QObject(parent)
{
    RoInitialize(RO_INIT_SINGLETHREADED);
    init();

    // Throttle position updates to ~4 per second
    m_posTimer.setInterval(250);
    m_posTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_posTimer, &QTimer::timeout, this, [this] {
        if (!m_ready || m_durationMs <= 0) return;
        ComPtr<ISmtcTimeline> tl;
        if (!SmtcUtil::activateInstance<ISmtcTimeline>(
                L"Windows.Media.SystemMediaTransportControlsTimelineProperties", tl,
                IID_ISmtcTimeline_)) return;
        tl->put_StartTime(SmtcUtil::msToTs(0));
        tl->put_EndTime(SmtcUtil::msToTs(m_durationMs));
        tl->put_Position(SmtcUtil::msToTs(m_posMs));
        tl->put_MinSeekTime(SmtcUtil::msToTs(0));
        tl->put_MaxSeekTime(SmtcUtil::msToTs(m_durationMs));
        ComPtr<ISmtcCore2> s2;
        if (SUCCEEDED(m_smtc.As(&s2))) s2->UpdateTimelineProperties(tl.Get());
    });
    m_posTimer.start();
}

inline SmtcBridge::~SmtcBridge()
{
    m_posTimer.stop();
    if (m_ready && m_smtc && m_btnToken)
        m_smtc->remove_ButtonPressed(m_btnToken);
    if (m_ready && m_smtc)
        m_smtc->put_IsEnabled(false);
}

inline void SmtcBridge::init()
{
    // Get the ISystemMediaTransportControlsInterop factory
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    {
        HSTRING cls = nullptr;
        const wchar_t* name = L"Windows.Media.SystemMediaTransportControls";
        if (FAILED(WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &cls))) return;
        HRESULT hr = RoGetActivationFactory(cls,
            IID_ISmtcInterop_,
            reinterpret_cast<void**>(interop.GetAddressOf()));
        WindowsDeleteString(cls);
        if (FAILED(hr) || !interop) return;
    }

    // Find this process's main visible window (GetForWindow needs a real HWND)
    HWND hwnd = nullptr;
    struct Finder { DWORD pid; HWND found; } f{ GetCurrentProcessId(), nullptr };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* p = reinterpret_cast<Finder*>(lp);
        DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
        if (pid == p->pid && IsWindowVisible(h)) { p->found = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&f));
    hwnd = f.found ? f.found : GetConsoleWindow();
    if (!hwnd) return;

    if (FAILED(interop->GetForWindow(hwnd, IID_ISmtcCore_,
                                     reinterpret_cast<void**>(m_smtc.GetAddressOf())))) return;

    m_smtc->put_IsEnabled(true);
    m_smtc->put_IsPlayEnabled(true);
    m_smtc->put_IsPauseEnabled(true);
    m_smtc->put_IsStopEnabled(false);
    m_smtc->put_IsNextEnabled(true);
    m_smtc->put_IsPreviousEnabled(true);
    m_smtc->put_IsFastForwardEnabled(false);
    m_smtc->put_IsRewindEnabled(false);
    m_smtc->put_IsRecordEnabled(false);
    m_smtc->put_IsChannelUpEnabled(false);
    m_smtc->put_IsChannelDownEnabled(false);
    m_smtc->put_PlaybackStatus(SMTC_Enums::Stopped);

    // Grab the display updater
    IInspectable* rawUpd = nullptr;
    if (FAILED(m_smtc->get_DisplayUpdater(&rawUpd)) || !rawUpd) return;
    rawUpd->QueryInterface(IID_ISmtcDisplayUpd_,
                           reinterpret_cast<void**>(m_upd.GetAddressOf()));
    rawUpd->Release();
    if (!m_upd) return;

    m_upd->put_Type(SMTC_Enums::Music);

    // Grab the music-properties sub-object
    IInspectable* rawMusic = nullptr;
    if (SUCCEEDED(m_upd->get_MusicProperties(&rawMusic)) && rawMusic) {
        rawMusic->QueryInterface(IID_ISmtcMusicProps_,
                                 reinterpret_cast<void**>(m_music.GetAddressOf()));
        rawMusic->Release();
    }

    // Wire the button handler
    auto* bh = new ButtonHandler(this);
    m_smtc->add_ButtonPressed(bh, &m_btnToken);
    bh->Release();

    m_ready = true;
}

inline void SmtcBridge::setTrack(const QString& title, const QString& artist, const QString& album,
                                  const QImage& cover, qint64 durationMs)
{
    if (!m_ready) return;
    m_durationMs = durationMs;

    if (m_music) {
        HSTRING ht = SmtcUtil::toHS(title),
                ha = SmtcUtil::toHS(artist),
                hb = SmtcUtil::toHS(album);
        m_music->put_Title(ht);
        m_music->put_Artist(ha);
        m_music->put_AlbumArtist(hb);
        SmtcUtil::freeHS(ht); SmtcUtil::freeHS(ha); SmtcUtil::freeHS(hb);
    }
    pushArt(cover);
    if (m_upd) m_upd->Update();
}

inline void SmtcBridge::pushArt(const QImage& img)
{
    if (!m_upd) return;
    if (img.isNull()) { m_upd->put_Thumbnail(nullptr); return; }

    // Save the cover to a temp PNG and give it to Windows as a stream reference.
    static QString tmpPath;
    if (tmpPath.isEmpty())
        tmpPath = QDir::temp().filePath(QStringLiteral("pulsify_art.png"));

    QImage scaled = img;
    if (scaled.width() > 300 || scaled.height() > 300)
        scaled = img.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!scaled.save(tmpPath, "PNG")) return;

    // Build a file:// URI as a WinRT Uri object
    ComPtr<ISmtcUriFac> uriFac;
    if (!SmtcUtil::activateFactory<ISmtcUriFac>(
            L"Windows.Foundation.Uri", uriFac, IID_ISmtcUriFac_)) return;

    const QString fileUri = QStringLiteral("file:///") +
        QDir::toNativeSeparators(tmpPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    HSTRING hUri = SmtcUtil::toHS(fileUri);
    ComPtr<IInspectable> uriObj;
    HRESULT hr = uriFac->CreateUri(hUri, uriObj.GetAddressOf());
    SmtcUtil::freeHS(hUri);
    if (FAILED(hr) || !uriObj) return;

    // RandomAccessStreamReference.CreateFromUri
    ComPtr<ISmtcStreamRefStatics> rasFac;
    if (!SmtcUtil::activateFactory<ISmtcStreamRefStatics>(
            L"Windows.Storage.Streams.RandomAccessStreamReference",
            rasFac, IID_ISmtcStreamRefSt_)) return;

    ComPtr<ISmtcStreamRef> streamRef;
    if (FAILED(rasFac->CreateFromUri(uriObj.Get(), streamRef.GetAddressOf())) || !streamRef) return;

    m_upd->put_Thumbnail(streamRef.Get());
}

inline void SmtcBridge::setPlaybackState(bool playing, bool paused)
{
    if (!m_ready) return;
    SMTC_Enums::MediaPlaybackStatus s =
        playing ? SMTC_Enums::Playing :
        paused  ? SMTC_Enums::Paused  :
                  SMTC_Enums::Stopped;
    m_smtc->put_PlaybackStatus(s);
}

inline void SmtcBridge::setPosition(qint64 ms)
{
    m_posMs = ms;
}

#else // !Q_OS_WIN — empty inline stubs

inline SmtcBridge::SmtcBridge(QObject* parent) : QObject(parent) {}
inline SmtcBridge::~SmtcBridge() = default;
inline void SmtcBridge::setTrack(const QString&, const QString&, const QString&, const QImage&, qint64) {}
inline void SmtcBridge::setPlaybackState(bool, bool) {}
inline void SmtcBridge::setPosition(qint64) {}

#endif // Q_OS_WIN
