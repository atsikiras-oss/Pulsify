#include <QtWidgets>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QMediaFormat>
#include <unordered_map>
#include <algorithm>

#include "widgets.h"
#include "library.h"
#include "online.h"
#include "playlists.h"
#include "google.h"
#include "importdialog.h"
#include "dialogs.h"
#include "ui.h"
#include "themedialog.h"
#include "home.h"
#include "lyrics.h"
#include "relay.h"
#include "infopanel.h"
#include "discord.h"

static const QSet<QString>& audioExts()
{
    static const QSet<QString> e = { "mp3", "flac", "wav", "ogg", "m4a", "aac", "opus", "wma" };
    return e;
}

// Expands files/folders (recursively) into a flat list of audio files.
static QStringList collectAudio(const QStringList& inputs)
{
    QStringList out;
    for (const QString& in : inputs) {
        const QFileInfo fi(in);
        if (fi.isDir()) {
            QDirIterator it(in, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (audioExts().contains(QFileInfo(f).suffix().toLower())) out << f;
            }
        } else if (fi.isFile() && audioExts().contains(fi.suffix().toLower())) {
            out << fi.absoluteFilePath();
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

// The shelves on the Home page. Each one is a YouTube search; edit freely.
struct DiscoverDef { const char* title; const char* query; };
static const DiscoverDef kDiscover[] = {
    { "Trending now",         "top hits this week official audio" },
    { "Chill vibes",          "chill songs to relax" },
    { "Workout energy",       "workout motivation songs" },
    { "Throwback hits",       "2000s hits" },
    { "Late night drive",     "late night drive songs" },
    { "Acoustic & unplugged", "acoustic songs" },
};
static constexpr int kDiscoverCount = int(sizeof(kDiscover) / sizeof(kDiscover[0]));

static QIcon searchIcon()
{
    const qreal dpr = 2.0;
    QPixmap pm(QSize(22, 22) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::subtext(), 1.9, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(3, 3, 11.5, 11.5));
    p.drawLine(QPointF(12.3, 12.3), QPointF(18, 18));
    p.end();
    return QIcon(pm);
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("Pulsify"));
        resize(1280, 800);
        setMinimumSize(960, 620);
        setAcceptDrops(true);
        buildUi();
        buildPlayer();
        buildShortcuts();
        restore();
        refreshNowPlaying();
    }

    void openPaths(const QStringList& inputs)
    {
        const QStringList files = collectAudio(inputs);
        if (!files.isEmpty()) showTab(TabLibrary);
        addPaths(files);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent* e) override
    {
        QStringList in;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile()) in << u.toLocalFile();
        openPaths(in);
    }
    void closeEvent(QCloseEvent* e) override
    {
        QSettings s;
        s.setValue("geometry", saveGeometry());
        s.setValue("volume", m_volume->value() > 0.001 ? m_volume->value() : m_preMute);
        if (m_rpc) m_rpc->shutdown();           // removes the "Listening to" status from Discord
        QMainWindow::closeEvent(e);
    }

private:
    enum Tab { TabHome = 0, TabLibrary = 1, TabPlaylist = 2 };       // Home (online) is the first tab
    enum class Banner { None, Setup, Update, Busy };
    enum class Src { None, Library, Playlist, Other };               // where the current queue came from
    enum class MenuFrom { Library, Search, Playlist, Other };

    struct AddMenu {
        QAction* create = nullptr;
        QHash<QAction*, QString> lists;
    };

    // =====================================================================
    //  UI construction
    // =====================================================================
    void buildUi()
    {
        auto* central = new ThemedCentral(this);      // flat canvas, gradient or custom image behind the panels
        setCentralWidget(central);

        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_upper = new QWidget;
        auto* upper = new QHBoxLayout(m_upper);
        upper->setContentsMargins(8, 8, 8, 0);
        upper->setSpacing(8);

        m_model  = new TrackModel(this);      // offline library
        m_smodel = new TrackModel(this);      // search results
        m_pmodel = new TrackModel(this);      // the playlist that is open
        m_loader = new MetaLoader(this);
        m_google = new GoogleAccount(this);

        upper->addWidget(buildSidebar());
        buildLibraryPage();
        buildHomePage();
        buildPlaylistPage();
        m_stack = new QStackedWidget;
        m_stack->addWidget(m_onlinePage);     // 0 = Home / search (online)
        m_stack->addWidget(m_page);           // 1 = offline library
        m_stack->addWidget(m_plPage);         // 2 = open playlist
        upper->addWidget(m_stack, 1);

        root->addWidget(m_upper, 1);
        root->addWidget(buildPlayerBar());

        // sheet + toast are created last so they sit on top
        m_sheet = new NowPlayingSheet(m_upper);
        m_toast = new Toast(central);

        // Info panel (right sidebar): slides in from the right; toggled by the player-bar icon or Ctrl+Shift+I
        m_infoPanel = new InfoPanel;
        m_infoHost = new SlideHost(m_infoPanel, InfoPanel::kWidth, m_upper);
        m_upper->layout()->addWidget(m_infoHost);    // far right, next to the pages
        connect(m_infoPanel, &InfoPanel::closeRequested, this, [this] { m_infoHost->setOpen(false); });
        connect(m_infoHost, &SlideHost::openChanged, this, [this](bool on) { if (m_infoBtn) m_infoBtn->setActive(on); });
        m_sheet->raise();                            // the full-screen sheet always stays on top
        m_toast->raise();

        // Lyrics service + view
        m_lyricsService = new LyricsService(this);
        m_lyricsView = new LyricsView;
        // The lyrics view is shown inside the NowPlayingSheet via its layout
        m_sheet->addLyricsView(m_lyricsView);
        connect(m_lyricsService, &LyricsService::ready, this, [this](const QString& key, const LyricsDoc& doc) {
            if (!m_lyricsView || key != m_lyricsKey) return;            // a song that is no longer the current one
            if (doc.instrumental) { m_lyricsView->setMessage(QStringLiteral("This track is instrumental.")); return; }
            m_lyricsView->setDoc(doc, 0);
            m_lyricsView->setPosition(m_player ? m_player->position() : 0);
        });
        connect(m_lyricsService, &LyricsService::failed, this, [this](const QString& key, const QString& msg) {
            if (m_lyricsView && key == m_lyricsKey) m_lyricsView->setMessage(msg);
        });
        connect(m_sheet, &NowPlayingSheet::stateChanged, this, [this] {
            if (m_lyricsBtn) m_lyricsBtn->setActive(m_sheet->isOpen() && m_sheet->lyricsMode());
        });
        connect(m_lyricsView, &LyricsView::seekRequested, this, [this](qint64 ms) { seekTo(ms); });

        // Deezer chart service (Home page trending content)
        m_imgCache = new ImageCache(this);
        m_charts = new ChartService(this);
        connect(m_charts, &ChartService::tracksReady, this, [this](const QVector<Track>& tracks) {
            m_chartSongs = tracks;
            populateChartShelf();
        });
        connect(m_charts, &ChartService::artistsReady, this, [this](const QVector<ChartItem>& items) {
            m_chartArtists = items;
        });
        connect(m_charts, &ChartService::albumsReady, this, [this](const QVector<ChartItem>& items) {
            m_chartAlbums = items;
        });

        wireUi();
    }

    QWidget* buildSidebar()
    {
        auto* side = new QWidget;
        side->setFixedWidth(272);
        auto* sv = new QVBoxLayout(side);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(8);

        // ---- navigation card
        auto* nav = new TintWidget(0.0, 0, 12);
        auto* nl = new QVBoxLayout(nav);
        nl->setContentsMargins(12, 18, 12, 12);
        nl->setSpacing(2);

        auto* logo = new QLabel(QStringLiteral("<span style='color:#1DB954'>&#9679;</span>&nbsp;Pulsify"));
        logo->setTextFormat(Qt::RichText);
        sty(logo, "font-size:26px;font-weight:800;color:white;padding:2px 14px 14px 14px;background:transparent;");
        nl->addWidget(logo);

        m_navHome = new NavButton(NavButton::Home, QStringLiteral("Home"));
        m_navLib = new NavButton(NavButton::Library, QStringLiteral("Your Library"));
        auto* navFiles = new NavButton(NavButton::AddFiles, QStringLiteral("Add files"));
        auto* navFolder = new NavButton(NavButton::AddFolder, QStringLiteral("Add folder"));
        nl->addWidget(m_navHome);
        nl->addWidget(m_navLib);
        nl->addWidget(navFiles);
        nl->addWidget(navFolder);
        sv->addWidget(nav);

        connect(m_navHome, &QAbstractButton::clicked, this, [this] { showHome(); });
        connect(m_navLib, &QAbstractButton::clicked, this, [this] { showTab(TabLibrary); });
        connect(navFiles, &QAbstractButton::clicked, this, &MainWindow::addFiles);
        connect(navFolder, &QAbstractButton::clicked, this, &MainWindow::addFolder);

        // ---- playlists card
        auto* lib = new TintWidget(0.0, 0, 12);
        auto* ll = new QVBoxLayout(lib);
        ll->setContentsMargins(8, 14, 8, 8);
        ll->setSpacing(6);

        auto* hdr = new QHBoxLayout;
        hdr->setContentsMargins(10, 0, 4, 4);
        auto* hl = new QLabel(QStringLiteral("Playlists"));
        sty(hl, "font-size:15px;font-weight:700;color:#b3b3b3;background:transparent;");
        auto* create = new QPushButton(QStringLiteral("+  Create"));
        create->setCursor(Qt::PointingHandCursor);
        create->setFocusPolicy(Qt::NoFocus);
        create->setFixedHeight(32);
        sty(create, 
            "QPushButton{background:rgba(255,255,255,0.09);color:white;border:none;border-radius:16px;"
            "padding:0 14px;font-size:13px;font-weight:700;}"
            "QPushButton:hover{background:rgba(255,255,255,0.18);}");
        auto* importBtn = new QPushButton(QStringLiteral("Import"));
        importBtn->setCursor(Qt::PointingHandCursor);
        importBtn->setFocusPolicy(Qt::NoFocus);
        importBtn->setFixedHeight(32);
        importBtn->setToolTip(QStringLiteral("Import playlists from YouTube / YouTube Music (Ctrl+I)"));
        sty(importBtn, Theme::origSheet(create));
        hdr->addWidget(hl);
        hdr->addStretch(1);
        hdr->addWidget(importBtn);
        hdr->addSpacing(4);
        hdr->addWidget(create);
        ll->addLayout(hdr);
        connect(create, &QPushButton::clicked, this, [this] { createPlaylist(-1); });
        connect(importBtn, &QPushButton::clicked, this, [this] { openImportDialog(); });

        m_plEmpty = new QFrame;
        m_plEmpty->setObjectName(QStringLiteral("plEmpty"));
        sty(m_plEmpty, "QFrame#plEmpty{background:rgba(255,255,255,0.07);border-radius:10px;}");
        auto* el = new QVBoxLayout(m_plEmpty);
        el->setContentsMargins(18, 18, 18, 18);
        el->setSpacing(6);
        auto* t1 = new QLabel(QStringLiteral("Create your first playlist"));
        sty(t1, "font-size:15px;font-weight:700;color:white;background:transparent;");
        auto* t2 = new QLabel(QStringLiteral("Add songs from your library or YouTube. Drag them in, or right-click a song."));
        t2->setWordWrap(true);
        sty(t2, "font-size:13px;color:#b3b3b3;background:transparent;");
        auto* eb = new QPushButton(QStringLiteral("Create playlist"));
        eb->setCursor(Qt::PointingHandCursor);
        eb->setFocusPolicy(Qt::NoFocus);
        eb->setFixedHeight(34);
        sty(eb, 
            "QPushButton{background:white;color:black;border:none;border-radius:17px;padding:0 18px;font-size:13px;font-weight:700;}"
            "QPushButton:hover{background:#f0f0f0;}QPushButton:pressed{background:#d8d8d8;}");
        el->addWidget(t1);
        el->addWidget(t2);
        el->addSpacing(8);
        auto* ebRow = new QHBoxLayout;
        ebRow->setSpacing(8);
        auto* ib = new QPushButton(QStringLiteral("Import from YouTube"));
        ib->setCursor(Qt::PointingHandCursor);
        ib->setFocusPolicy(Qt::NoFocus);
        ib->setFixedHeight(34);
        sty(ib, 
            "QPushButton{background:rgba(255,255,255,0.12);color:white;border:none;border-radius:17px;padding:0 16px;font-size:13px;font-weight:700;}"
            "QPushButton:hover{background:rgba(255,255,255,0.22);}");
        ebRow->addWidget(eb);
        ebRow->addWidget(ib);
        ebRow->addStretch(1);
        el->addLayout(ebRow);
        ll->addWidget(m_plEmpty);
        connect(eb, &QPushButton::clicked, this, [this] { createPlaylist(-1); });
        connect(ib, &QPushButton::clicked, this, [this] { openImportDialog(); });

        m_plScroll = new SmoothScrollArea;
        auto* body = new QWidget;
        m_plRowsLayout = new QVBoxLayout(body);
        m_plRowsLayout->setContentsMargins(0, 0, 0, 0);
        m_plRowsLayout->setSpacing(0);
        m_plRowsLayout->addStretch(1);
        m_plScroll->setBody(body);
        ll->addWidget(m_plScroll, 1);

        auto* tip = new QLabel(QStringLiteral("Tip: drop audio files anywhere in the window."));
        tip->setWordWrap(true);
        sty(tip, "color:#7a7a7a;font-size:12px;padding:4px 10px 2px 10px;background:transparent;");
        ll->addWidget(tip);

        sv->addWidget(lib, 1);
        return side;
    }

    // ---- offline library page (unchanged behaviour)
    void buildLibraryPage()
    {
        m_page = new TintWidget(0.7, 420, 12);
        auto* pl = new QVBoxLayout(m_page);
        pl->setContentsMargins(0, 28, 0, 0);
        pl->setSpacing(0);

        auto* head = new QHBoxLayout;
        head->setContentsMargins(34, 0, 34, 0);
        auto* titleCol = new QVBoxLayout;
        titleCol->setSpacing(2);
        auto* title = new QLabel(QStringLiteral("Your Library"));
        sty(title, "font-size:40px;font-weight:800;color:white;background:transparent;");
        m_count = new QLabel;
        sty(m_count, "font-size:13px;color:#c8c8c8;background:transparent;");
        titleCol->addWidget(title);
        titleCol->addWidget(m_count);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(QStringLiteral("Search songs"));
        m_search->setClearButtonEnabled(true);
        m_search->setFixedSize(280, 40);
        sty(m_search, 
            "QLineEdit{background:rgba(255,255,255,0.10);border:1px solid transparent;border-radius:20px;"
            "padding:0 16px;color:white;font-size:14px;selection-background-color:#1DB954;}"
            "QLineEdit:hover{background:rgba(255,255,255,0.14);}"
            "QLineEdit:focus{background:rgba(255,255,255,0.16);border:1px solid rgba(255,255,255,0.75);}");
        head->addLayout(titleCol, 1);
        head->addWidget(m_search, 0, Qt::AlignTop);
        pl->addLayout(head);

        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(34, 20, 34, 12);
        actions->setSpacing(16);
        m_heroPlay = new IconButton(IconButton::Play, 62);
        m_heroPlay->setCircle(Theme::accent());
        m_heroShuffle = new IconButton(IconButton::Shuffle, 42);
        actions->addWidget(m_heroPlay);
        actions->addWidget(m_heroShuffle);
        actions->addStretch(1);
        pl->addLayout(actions);

        m_proxy = new QSortFilterProxyModel(this);
        m_proxy->setSourceModel(m_model);
        m_proxy->setFilterRole(SearchRole);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_view = new TrackView;
        m_view->setModel(m_proxy);
        m_view->setContextMenuPolicy(Qt::NoContextMenu);
        m_view->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        pl->addWidget(m_view, 1);
    }

    // ---- Home: Spotify-style online page (quick tiles, shelves, search)
    void buildHomePage()
    {
        m_yt = new YtDlp(this);
        m_thumbs = new ThumbLoader(this);
        m_discItems.resize(kDiscoverCount);

        m_onlinePage = new TintWidget(0.7, 420, 12);
        auto* pl = new QVBoxLayout(m_onlinePage);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(0);

        // ---- top bar: Home button, search pill, refresh
        auto* top = new QHBoxLayout;
        top->setContentsMargins(24, 16, 24, 10);
        top->setSpacing(10);
        m_homeBtn = new IconButton(IconButton::Home, 48);
        m_homeBtn->setCircle(Theme::greyAt(0.055), Theme::fg());
        m_osearch = new QLineEdit;
        m_osearch->setPlaceholderText(QStringLiteral("What do you want to play?"));
        m_osearch->setClearButtonEnabled(true);
        m_osearch->setFixedHeight(48);
        m_osearch->setMinimumWidth(240);
        m_osearch->setMaximumWidth(560);
        m_searchAct = m_osearch->addAction(searchIcon(), QLineEdit::LeadingPosition);
        sty(m_osearch, 
            "QLineEdit{background:#1f1f1f;border:1px solid transparent;border-radius:24px;"
            "padding:0 16px 0 6px;color:white;font-size:15px;selection-background-color:#1DB954;}"
            "QLineEdit:hover{background:#2a2a2a;}"
            "QLineEdit:focus{background:#2a2a2a;border:1px solid rgba(255,255,255,0.85);}");
        m_refreshBtn = new QPushButton(QStringLiteral("Refresh"));
        m_refreshBtn->setCursor(Qt::PointingHandCursor);
        m_refreshBtn->setFocusPolicy(Qt::NoFocus);
        m_refreshBtn->setFixedHeight(36);
        m_refreshBtn->setToolTip(QStringLiteral("Reload the recommendations"));
        sty(m_refreshBtn, 
            "QPushButton{background:rgba(255,255,255,0.10);color:white;border:none;border-radius:18px;"
            "padding:0 18px;font-size:13px;font-weight:700;}"
            "QPushButton:hover{background:rgba(255,255,255,0.18);}"
            "QPushButton:disabled{color:#8a8a8a;background:rgba(255,255,255,0.06);}");
        auto* right = new QWidget;
        auto* rl = new QHBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(12);
        rl->addStretch(1);
        rl->addWidget(m_refreshBtn);
        m_themeBtn = new QPushButton(QStringLiteral("Themes"));
        m_themeBtn->setCursor(Qt::PointingHandCursor);
        m_themeBtn->setFocusPolicy(Qt::NoFocus);
        m_themeBtn->setFixedHeight(36);
        m_themeBtn->setToolTip(QStringLiteral("Themes, gradients and custom backgrounds (Ctrl+T)"));
        sty(m_themeBtn, Theme::origSheet(m_refreshBtn));
        rl->addWidget(m_themeBtn);
        // Settings button (yt-dlp, JS runtime, cookies, lyrics, Google account)
        auto* settingsBtn = new QPushButton(QStringLiteral("Settings"));
        settingsBtn->setCursor(Qt::PointingHandCursor);
        settingsBtn->setFocusPolicy(Qt::NoFocus);
        settingsBtn->setFixedHeight(36);
        settingsBtn->setToolTip(QStringLiteral("Settings (yt-dlp, lyrics, Google account)"));
        sty(settingsBtn, Theme::origSheet(m_refreshBtn));
        rl->addWidget(settingsBtn);
        connect(settingsBtn, &QPushButton::clicked, this, [this] { openSettingsDialog(); });
        m_avatar = new AvatarButton(44);
        rl->addWidget(m_avatar);
        top->addStretch(1);
        top->addWidget(m_homeBtn);
        top->addWidget(m_osearch, 4);
        top->addWidget(right, 1);
        pl->addLayout(top);

        m_searchDebounce.setSingleShot(true);
        m_resultsTimer.setSingleShot(true);
        m_coverTimer.setSingleShot(true);

        // ---- banner: install / update yt-dlp
        m_banner = new QFrame;
        m_banner->setObjectName(QStringLiteral("banner"));
        sty(m_banner, 
            "QFrame#banner{background:rgba(255,255,255,0.09);border-radius:10px;margin:0px 34px 10px 34px;}"
            "QLabel{background:transparent;color:#ececec;font-size:13px;}"
            "QPushButton{background:rgba(255,255,255,0.16);color:white;border:none;border-radius:15px;"
            "padding:0 16px;font-size:13px;font-weight:600;min-height:30px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.26);}"
            "QPushButton:disabled{color:#888;background:rgba(255,255,255,0.07);}");
        auto* bl = new QHBoxLayout(m_banner);
        bl->setContentsMargins(16, 10, 12, 10);
        bl->setSpacing(10);
        m_bannerText = new QLabel;
        m_bannerText->setWordWrap(true);
        m_bannerA = new QPushButton;
        m_bannerB = new QPushButton;
        for (auto* b : { m_bannerA, m_bannerB }) { b->setCursor(Qt::PointingHandCursor); b->setFocusPolicy(Qt::NoFocus); }
        bl->addWidget(m_bannerText, 1);
        bl->addWidget(m_bannerA);
        bl->addWidget(m_bannerB);
        m_banner->hide();
        pl->addWidget(m_banner);

        m_oStatus = new QLabel;
        m_oStatus->setContentsMargins(34, 0, 34, 6);
        sty(m_oStatus, "font-size:13px;color:#c8c8c8;background:transparent;");
        m_oStatus->hide();
        pl->addWidget(m_oStatus);

        m_oStack = new QStackedWidget;
        m_oStack->addWidget(buildHomeFeed());
        m_oStack->addWidget(buildSearchResults());
        pl->addWidget(m_oStack, 1);
    }

    QWidget* buildHomeFeed()
    {
        m_homeScroll = new SmoothScrollArea;
        auto* body = new QWidget;
        auto* v = new QVBoxLayout(body);
        v->setContentsMargins(22, 4, 22, 36);
        v->setSpacing(0);

        // filter chips
        auto* chipsWrap = new QWidget;
        auto* cl = new QHBoxLayout(chipsWrap);
        cl->setContentsMargins(12, 0, 12, 16);
        cl->setSpacing(8);
        m_chipGroup = new QButtonGroup(this);
        m_chipGroup->setExclusive(true);
        const QStringList chips = { QStringLiteral("All"), QStringLiteral("Your playlists"),
                                    QStringLiteral("Recently played"), QStringLiteral("Discover") };
        for (int i = 0; i < chips.size(); ++i) {
            auto* b = new QPushButton(chips[i]);
            b->setCheckable(true);
            b->setChecked(i == 0);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setFixedHeight(32);
            sty(b, 
                "QPushButton{background:rgba(255,255,255,0.10);color:white;border:none;border-radius:16px;"
                "padding:0 16px;font-size:14px;font-weight:600;}"
                "QPushButton:hover{background:rgba(255,255,255,0.18);}"
                "QPushButton:checked{background:white;color:black;}");
            m_chipGroup->addButton(b, i);
            cl->addWidget(b);
        }
        cl->addStretch(1);
        v->addWidget(chipsWrap);
        connect(m_chipGroup, &QButtonGroup::idClicked, this, [this](int id) { m_chip = id; applyHomeVisibility(); });

        // quick-access tiles
        m_tilesWrap = new QWidget;
        auto* tl = new QVBoxLayout(m_tilesWrap);
        tl->setContentsMargins(12, 0, 12, 18);
        m_tiles = new TileGrid;
        tl->addWidget(m_tiles);
        v->addWidget(m_tilesWrap);

        // shelves
        m_shPlaylists = new Shelf(QStringLiteral("Your playlists"));
        m_shRecent = new Shelf(QStringLiteral("Recently played"));
        m_shMore = new Shelf(QStringLiteral("More like this"));
        v->addWidget(m_shPlaylists);
        v->addWidget(m_shRecent);
        v->addWidget(m_shMore);
        for (int i = 0; i < kDiscoverCount; ++i) {
            auto* s = new Shelf(QString::fromUtf8(kDiscover[i].title));
            m_disc.push_back(s);
            v->addWidget(s);
        }

        m_homeEmpty = new QLabel;
        m_homeEmpty->setWordWrap(true);
        m_homeEmpty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_homeEmpty->setContentsMargins(40, 60, 40, 0);
        sty(m_homeEmpty, "font-size:15px;color:#b3b3b3;background:transparent;");
        v->addWidget(m_homeEmpty);
        v->addStretch(1);

        for (Shelf* s : { m_shPlaylists, m_shRecent, m_shMore }) s->hide();
        for (Shelf* s : m_disc) s->hide();
        m_tilesWrap->hide();
        m_homeScroll->setBody(body);
        return m_homeScroll;
    }

    QWidget* buildSearchResults()
    {
        m_searchScroll = new SmoothScrollArea;
        auto* body = new QWidget;
        auto* v = new QVBoxLayout(body);
        v->setContentsMargins(22, 4, 22, 36);
        v->setSpacing(0);

        const QString h2 = QStringLiteral("font-size:24px;font-weight:800;color:white;background:transparent;");
        m_heroWrap = new QWidget;
        auto* h = new QHBoxLayout(m_heroWrap);
        h->setContentsMargins(12, 0, 12, 16);
        h->setSpacing(28);

        auto* left = new QVBoxLayout;
        left->setSpacing(10);
        auto* lt = new QLabel(QStringLiteral("Top result"));
        sty(lt, h2);
        m_topCard = new MediaCard(MediaCard::Hero, MediaCard::TrackCard);
        left->addWidget(lt);
        left->addWidget(m_topCard);
        left->addStretch(1);

        auto* rightCol = new QVBoxLayout;
        rightCol->setSpacing(10);
        auto* rt = new QLabel(QStringLiteral("Songs"));
        sty(rt, h2);
        m_sproxy = new HeadProxy(this);
        m_sproxy->setSourceModel(m_smodel);
        m_slist = new TrackView;
        m_slist->setModel(m_sproxy);
        m_slist->setScrollable(false);
        m_slist->setFixedHeight(4 * 58 + 6);
        m_slist->setContextMenuPolicy(Qt::NoContextMenu);
        m_slist->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        rightCol->addWidget(rt);
        rightCol->addWidget(m_slist);
        rightCol->addStretch(1);

        h->addLayout(left, 5);
        h->addLayout(rightCol, 7);
        v->addWidget(m_heroWrap);

        m_shPlRes = new Shelf(QStringLiteral("Playlists"));
        v->addWidget(m_shPlRes);
        m_shMoreRes = new Shelf(QStringLiteral("More results"));
        v->addWidget(m_shMoreRes);
        v->addStretch(1);
        m_heroWrap->hide();
        m_shPlRes->hide();
        m_shMoreRes->hide();
        m_searchScroll->setBody(body);
        return m_searchScroll;
    }

    // ---- a playlist's own page
    void buildPlaylistPage()
    {
        m_plPage = new TintWidget(0.7, 420, 12);
        auto* pl = new QVBoxLayout(m_plPage);
        pl->setContentsMargins(0, 28, 0, 0);
        pl->setSpacing(0);

        auto* head = new QHBoxLayout;
        head->setContentsMargins(16, 0, 34, 0);
        head->setSpacing(10);
        m_plCover = new PlaylistCover(180);
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        auto* kicker = new QLabel(QStringLiteral("PLAYLIST"));
        sty(kicker, "font-size:12px;font-weight:700;color:rgba(255,255,255,0.8);background:transparent;");
        m_plName = new ElidedLabel(54, QFont::Bold, Theme::Color::fgRole());
        m_plMeta = new QLabel;
        sty(m_plMeta, "font-size:13px;color:#c8c8c8;background:transparent;");
        col->addStretch(1);
        col->addWidget(kicker);
        col->addWidget(m_plName);
        col->addWidget(m_plMeta);
        col->addSpacing(18);
        head->addWidget(m_plCover);
        head->addLayout(col, 1);
        pl->addLayout(head);

        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(34, 6, 34, 12);
        actions->setSpacing(16);
        m_plHeroPlay = new IconButton(IconButton::Play, 62);
        m_plHeroPlay->setCircle(Theme::accent());
        m_plHeroShuffle = new IconButton(IconButton::Shuffle, 42);
        m_plMore = new IconButton(IconButton::More, 42);
        actions->addWidget(m_plHeroPlay);
        actions->addWidget(m_plHeroShuffle);
        actions->addWidget(m_plMore);
        actions->addStretch(1);
        pl->addLayout(actions);

        m_pview = new TrackView;
        m_pview->setModel(m_pmodel);
        m_pview->setContextMenuPolicy(Qt::NoContextMenu);
        m_pview->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        pl->addWidget(m_pview, 1);
    }

    QWidget* buildPlayerBar()
    {
        auto* bar = new PlayerBarBg;
        bar->setFixedHeight(94);
        auto* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(18, 8, 18, 10);
        bl->setSpacing(12);

        // left: cover + title + add-to-playlist
        auto* left = new QWidget;
        left->setMinimumWidth(220);
        auto* ll = new QHBoxLayout(left);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(14);
        m_barCover = new CoverView(7, 0);
        m_barCover->setFixedSize(58, 58);
        m_barCover->setClickable(true);
        auto* tv = new QVBoxLayout;
        tv->setSpacing(0);
        m_barTitle  = new ElidedLabel(14, QFont::Medium, Theme::Color::fgRole());
        m_barArtist = new ElidedLabel(12, QFont::Normal, Theme::Color::subRole());
        tv->addStretch(1);
        tv->addWidget(m_barTitle);
        tv->addWidget(m_barArtist);
        tv->addStretch(1);
        m_addBtn = new IconButton(IconButton::Plus, 34);
        m_addBtn->setToolTip(QStringLiteral("Add to playlist"));
        ll->addWidget(m_barCover);
        ll->addLayout(tv, 1);
        ll->addWidget(m_addBtn);

        // center: transport + seek
        auto* center = new QWidget;
        center->setMaximumWidth(1100);
        auto* cl = new QVBoxLayout(center);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);
        auto* ctrl = new QHBoxLayout;
        ctrl->setSpacing(14);
        m_shuffleBtn = new IconButton(IconButton::Shuffle, 34);
        m_prevBtn    = new IconButton(IconButton::Prev, 38);
        m_playBtn    = new IconButton(IconButton::Play, 44);
        m_playBtn->setCircle(Theme::fg());
        m_nextBtn    = new IconButton(IconButton::Next, 38);
        m_repeatBtn  = new IconButton(IconButton::Repeat, 34);
        ctrl->addStretch(1);
        ctrl->addWidget(m_shuffleBtn);
        ctrl->addWidget(m_prevBtn);
        ctrl->addWidget(m_playBtn);
        ctrl->addWidget(m_nextBtn);
        ctrl->addWidget(m_repeatBtn);
        ctrl->addStretch(1);
        cl->addLayout(ctrl);

        auto* seekRow = new QHBoxLayout;
        seekRow->setSpacing(6);
        const QString timeStyle = "color:#a7a7a7;font-size:11px;background:transparent;";
        m_timeL = new QLabel(QStringLiteral("0:00"));
        m_timeL->setFixedWidth(46);
        m_timeL->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sty(m_timeL, timeStyle);
        m_timeR = new QLabel(QStringLiteral("0:00"));
        m_timeR->setFixedWidth(46);
        m_timeR->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        sty(m_timeR, timeStyle);
        m_seek = new SmoothSlider;
        seekRow->addWidget(m_timeL);
        seekRow->addWidget(m_seek, 1);
        seekRow->addWidget(m_timeR);
        cl->addLayout(seekRow);

        // right: volume
        auto* right = new QWidget;
        auto* rl = new QHBoxLayout(right);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(4);
        m_lyricsBtn = new IconButton(IconButton::Lyrics, 36);
        m_lyricsBtn->setToolTip(QStringLiteral("Lyrics (Ctrl+L)"));
        m_infoBtn = new IconButton(IconButton::Info, 36);
        m_infoBtn->setToolTip(QStringLiteral("Now playing info (Ctrl+Shift+I)"));
        m_volBtn = new IconButton(IconButton::Speaker, 36);
        m_volume = new SmoothSlider;
        m_volume->setFixedWidth(120);
        rl->addStretch(1);
        rl->addWidget(m_lyricsBtn);
        rl->addWidget(m_infoBtn);
        rl->addSpacing(6);
        rl->addWidget(m_volBtn);
        rl->addWidget(m_volume);

        left->setMinimumWidth(220);
        right->setMinimumWidth(310);
        bl->addWidget(left, 1);
        bl->addWidget(center, 4);           // no alignment flag: the column (and the seek bar) fills its slot
        bl->addWidget(right, 1);
        return bar;
    }

    // =====================================================================
    //  Signal wiring
    // =====================================================================
    void wireUi()
    {
        connect(m_loader, &MetaLoader::loaded, this, [this](const MetaResult& r) { applyMetaEverywhere(r); });
        connect(m_model, &TrackModel::libraryChanged, this, &MainWindow::updateCount);
        connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
            m_proxy->setFilterFixedString(t);
            updateCount();
        });

        // ---- library list
        connect(m_view, &QAbstractItemView::clicked, this, [this](const QModelIndex& ix) {
            const int id = ix.data(IdRole).toInt();
            if (id == m_currentId) togglePlay();
            else playFrom(idsOf(m_proxy), id, Src::Library);
        });
        connect(m_view->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
            const QModelIndex ix = m_view->indexAt(pt);
            if (ix.isValid()) showTrackMenu(ix.data(IdRole).toInt(), m_view->viewport()->mapToGlobal(pt), MenuFrom::Library);
        });

        // ---- search "Songs" list + top result
        connect(m_slist, &QAbstractItemView::clicked, this, [this](const QModelIndex& ix) {
            const int id = ix.data(IdRole).toInt();
            if (id == m_currentId) { if (!m_resolving) togglePlay(); }
            else playFrom(m_smodel->ids(), id, Src::Other);
        });
        connect(m_slist->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
            const QModelIndex ix = m_slist->indexAt(pt);
            if (ix.isValid()) showTrackMenu(ix.data(IdRole).toInt(), m_slist->viewport()->mapToGlobal(pt), MenuFrom::Search);
        });
        auto topAct = [this] { playFromCard(m_topCard->trackId(), m_smodel->ids()); };
        connect(m_topCard, &MediaCard::activated, this, topAct);
        connect(m_topCard, &MediaCard::playClicked, this, topAct);
        connect(m_topCard, &MediaCard::contextRequested, this, [this](const QPoint& gp) {
            showTrackMenu(m_topCard->trackId(), gp, MenuFrom::Search);
        });
        connect(&m_resultsTimer, &QTimer::timeout, this, &MainWindow::refreshSearchPage);

        // ---- playlist page
        connect(m_pview, &QAbstractItemView::clicked, this, [this](const QModelIndex& ix) {
            const int id = ix.data(IdRole).toInt();
            if (id == m_currentId) { if (!m_resolving) togglePlay(); }
            else playFrom(m_pmodel->ids(), id, Src::Playlist, m_openPl);
        });
        connect(m_pview->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
            const QModelIndex ix = m_pview->indexAt(pt);
            if (ix.isValid()) showTrackMenu(ix.data(IdRole).toInt(), m_pview->viewport()->mapToGlobal(pt), MenuFrom::Playlist);
        });
        connect(m_plHeroPlay, &QAbstractButton::clicked, this, [this] {
            if (m_src == Src::Playlist && m_srcKey == m_openPl && m_currentId >= 0) togglePlay();
            else playPlaylist(m_openPl);
        });
        connect(m_plHeroShuffle, &QAbstractButton::clicked, this, [this] { setShuffle(!m_shuffle); });
        connect(m_plMore, &QAbstractButton::clicked, this, [this] {
            if (m_openPl.isEmpty()) return;
            showPlaylistMenu(m_openPl, m_plMore->mapToGlobal(QPoint(0, m_plMore->height())));
        });

        // ---- Home page
        connect(m_homeBtn, &QAbstractButton::clicked, this, [this] { showHome(); });
        connect(m_refreshBtn, &QPushButton::clicked, this, [this] { fetchHome(); });
        connect(m_avatar, &QAbstractButton::clicked, this, [this] { showAccountMenu(); });
        connect(m_themeBtn, &QPushButton::clicked, this, [this] { openThemeDialog(); });
        connect(&themeBus(), &ThemeBus::changed, this, [this] { applyThemeColors(); });
        connect(m_google, &GoogleAccount::stateChanged, this, [this] { updateAvatar(); });
        connect(m_google, &GoogleAccount::avatarChanged, this, [this] { updateAvatar(); });
        updateAvatar();        connect(&m_searchDebounce, &QTimer::timeout, this, [this] { runSearch(); });
        connect(&m_coverTimer, &QTimer::timeout, this, &MainWindow::refreshPlaylistVisuals);
        connect(m_osearch, &QLineEdit::returnPressed, this, [this] { runSearch(); });
        connect(m_osearch, &QLineEdit::textChanged, this, [this](const QString& t) {
            if (t.simplified().isEmpty()) {
                m_searchDebounce.stop();
                m_yt->cancelSearch();
                m_searchTok = 0;
                m_oStack->setCurrentIndex(0);
                setOnlineStatus(QString());
            } else {
                m_searchDebounce.start(650);
            }
        });

        connect(m_bannerA, &QPushButton::clicked, this, [this] {
            if (m_bannerMode == Banner::Setup) { showBanner(Banner::Busy, QStringLiteral("Preparing download...")); m_yt->install(); }
            else if (m_bannerMode == Banner::Update) { showBanner(Banner::Busy, QStringLiteral("Updating yt-dlp...")); m_yt->selfUpdate(); }
        });
        connect(m_bannerB, &QPushButton::clicked, this, [this] {
            if (m_bannerMode == Banner::Setup) {
                const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Locate yt-dlp"));
                if (f.isEmpty()) return;
                m_yt->setCustomBinary(f);
                refreshToolState();
                if (m_yt->available()) { fetchHome(); if (!m_osearch->text().trimmed().isEmpty()) runSearch(); }
            } else {
                showBanner(Banner::None);
            }
        });
        connect(m_yt, &YtDlp::toolMessage, this, [this](const QString& m) { showBanner(Banner::Busy, m); });
        connect(m_yt, &YtDlp::toolFinished, this, [this](bool ok, const QString& msg) {
            if (ok) {
                showBanner(Banner::None);
                setOnlineStatus(QString());
                if (m_yt->available()) {
                    const Track* t = trackById(m_currentId);
                    if (t && t->online && m_player->playbackState() != QMediaPlayer::PlayingState) { m_retried = false; resolveCurrent(); }
                    fetchHome();
                    if (!m_osearch->text().trimmed().isEmpty() && m_smodel->rowCount() == 0) runSearch();
                }
            } else {
                setOnlineStatus(msg, true);
                showBanner(m_yt->available() ? Banner::Update : Banner::Setup);
                m_bannerText->setText(msg);
            }
        });
        wireOnline();

        // ---- player bar buttons
        connect(m_playBtn, &QAbstractButton::clicked, this, &MainWindow::togglePlay);
        connect(m_heroPlay, &QAbstractButton::clicked, this, [this] {
            if (m_src == Src::Library && m_currentId >= 0) { togglePlay(); return; }
            const QVector<int> v = idsOf(m_proxy);
            if (v.isEmpty()) return;
            playFrom(v, m_shuffle ? v[QRandomGenerator::global()->bounded(int(v.size()))] : v.first(), Src::Library);
        });
        connect(m_nextBtn, &QAbstractButton::clicked, this, [this] { playNext(false); });
        connect(m_prevBtn, &QAbstractButton::clicked, this, &MainWindow::playPrev);
        connect(m_shuffleBtn, &QAbstractButton::clicked, this, [this] { setShuffle(!m_shuffle); });
        connect(m_heroShuffle, &QAbstractButton::clicked, this, [this] { setShuffle(!m_shuffle); });
        connect(m_repeatBtn, &QAbstractButton::clicked, this, [this] {
            m_repeat = (m_repeat + 1) % 3;
            m_repeatBtn->setActive(m_repeat > 0);
            m_repeatBtn->setRepeatOne(m_repeat == 2);
        });
        connect(m_barCover, &CoverView::clicked, this, [this] {
            if (m_currentId < 0) return;
            if (m_sheet->isOpen()) m_sheet->closeSheet();
            else m_sheet->openSheet(false);
        });
        connect(m_lyricsBtn, &QAbstractButton::clicked, this, [this] { toggleLyrics(); });
        connect(m_infoBtn, &QAbstractButton::clicked, this, [this] { m_infoHost->toggle(); });
        connect(m_addBtn, &QAbstractButton::clicked, this, [this] {
            if (m_currentId < 0) return;
            QMenu menu(this);
            const AddMenu am = fillAddMenu(&menu);
            const QPoint gp = m_addBtn->mapToGlobal(QPoint(m_addBtn->width() / 2, 0));
            QAction* a = menu.exec(gp - QPoint(menu.sizeHint().width() / 2, menu.sizeHint().height() + 6));
            if (a) handleAddChoice(am, a, m_currentId);
        });

        // ---- sliders
        connect(m_seek, &SmoothSlider::moved, this, [this](qreal v) {
            setTimeLabel(qint64(v * currentDuration()));
        });
        connect(m_seek, &SmoothSlider::released, this, [this](qreal v) {
            seekTo(qint64(v * currentDuration()));
        });
        connect(m_volume, &SmoothSlider::moved, this, [this](qreal v) { applyVolume(v); });
        connect(m_volBtn, &QAbstractButton::clicked, this, [this] {
            if (m_volume->value() > 0.001) {
                m_preMute = m_volume->value();
                m_volume->setValue(0);
                applyVolume(0);
            } else {
                const qreal v = m_preMute > 0.001 ? m_preMute : 0.7;
                m_volume->setValue(v);
                applyVolume(v);
            }
        });
    }

    void wireOnline()
    {
        // ---- search results (streamed in as yt-dlp prints them)
        connect(m_yt, &YtDlp::searchBatch, this, [this](quint64 tok, const QVector<OnlineItem>& items) {
            if (tok != m_searchTok) return;
            if (m_searchFirst) { m_searchFirst = false; m_smodel->clear(); }
            QVector<Track> ts;
            for (const OnlineItem& it : items) ts.push_back(trackFromItem(it));
            const QVector<int> ids = m_smodel->appendTracks(ts);
            m_slist->appearIds(ids);
            setOnlineStatus(QStringLiteral("Searching YouTube..."));
            m_resultsTimer.start(90);
        });
        connect(m_yt, &YtDlp::searchDone, this, [this](quint64 tok, const QString& err) {
            if (tok != m_searchTok) return;
            if (m_searchFirst) { m_searchFirst = false; m_smodel->clear(); }
            m_resultsTimer.start(0);
            if (!err.isEmpty()) {
                setOnlineStatus(QStringLiteral("Search failed: ") + err, true);
                if (m_yt->available()) showBanner(Banner::Update);
                else showBanner(Banner::Setup);
                return;
            }
            const int n = m_smodel->rowCount();
            setOnlineStatus((n || !m_plResIds.isEmpty()) ? QString() : QStringLiteral("No results for \"%1\"").arg(m_lastQuery));
        });

        // ---- thumbnails
        connect(m_thumbs, &ThumbLoader::loaded, this, [this](int id, const QImage& img) {
            const Track* t = knownTrack(id);
            if (!t || t->cover.width() >= img.width()) return;      // never downgrade a better cover
            MetaResult r;
            r.id = id;
            r.cover = img;
            applyMetaEverywhere(r);
        });

        // ---- Home shelves
        connect(m_yt, &YtDlp::fetched, this, [this](quint64 tok, const QVector<OnlineItem>& items, const QString& err) {
            const auto it = m_fetchMap.find(tok);
            if (it == m_fetchMap.end()) return;
            const int idx = it.value();
            m_fetchMap.erase(it);
            Shelf* sh = (idx >= 0) ? m_disc[idx] : m_shMore;
            if (!err.isEmpty()) {
                ++m_fetchFails;
                const bool hasOld = (idx >= 0) ? !m_discItems[idx].isEmpty() : !m_moreItems.isEmpty();
                if (!hasOld) { sh->setCards({}); }
            } else if (!items.isEmpty()) {
                if (idx >= 0) m_discItems[idx] = items;
                else          m_moreItems = items;
                fillShelf(sh, items, idx >= 0 ? QString::fromUtf8(kDiscover[idx].title) : QStringLiteral("more"));
                m_cacheDirty = true;
            } else if (!sh->hasContent() || sh->isLoading()) {
                sh->setCards({});
            }
            if (m_fetchMap.isEmpty()) finishHomeFetch();
            applyHomeVisibility();
        });

        // ---- stream resolving
        connect(m_yt, &YtDlp::resolved, this, [this](quint64, const QString& videoId, const QString& url,
                                                        const OnlineItem& meta, const QString& err) {
            const Track* t = trackById(m_currentId);
            if (!t || !t->online || t->videoId != videoId || videoId != m_resolveVideo) return;   // stale
            m_resolving = false;
            if (!err.isEmpty() || url.isEmpty()) {
                m_barArtist->setText(QStringLiteral("Couldn't load: ") + err);
                setOnlineStatus(QStringLiteral("Couldn't load stream: ") + err, true);
                // Per-video problems (removed / region-locked) can't be fixed by updating yt-dlp.
                const bool perVideo = err.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("blocked in your country"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("private video"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("removed"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("copyright"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("terminated"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("members-only"), Qt::CaseInsensitive)
                                   || err.contains(QStringLiteral("confirm your age"), Qt::CaseInsensitive);
                if (!perVideo) showBanner(Banner::Update);
                else skipUnplayable(err);          // a removed / blocked video must not end the whole playlist
                return;
            }
            startStream(url, meta);
        });

        // ---- the next song's stream, resolved ahead of time
        connect(m_yt, &YtDlp::prefetched, this, [this](const QString& videoId, const QString& url,
                                                         const OnlineItem& meta, const QString& err) {
            if (!err.isEmpty() || url.isEmpty()) return;     // problems are reported when the song is actually reached
            if (m_prefetched.size() > 6) m_prefetched.clear();
            m_prefetched.insert(videoId, Prefetched{ url, meta, QDateTime::currentSecsSinceEpoch() });
        });
    }

    void buildPlayer()
    {
        // Stream relay must exist before the player so we can feed it relay URLs.
        m_relay = new StreamRelay(new QNetworkAccessManager(this), this);
        connect(m_relay, &StreamRelay::ready, this, [this](quint64 id, const QUrl& local) {
            if (id != m_relaySession) return;
            m_player->setSource(local);
            m_player->play();
            refreshNowPlaying();
        });
        connect(m_relay, &StreamRelay::failed, this, [this](quint64 id, const QString& why) {
            if (id != m_relaySession) return;
            const Track* t = trackById(m_currentId);
            if (t && t->online && !m_retried) { m_retried = true; resolveCurrent(); return; }
            m_barArtist->setText(QStringLiteral("Stream error: ") + why);
        });
        connect(m_relay, &StreamRelay::upstreamRejected, this, [this](quint64 id) {
            if (id != m_relaySession) return;
            const Track* t = trackById(m_currentId);
            if (t && t->online && !m_retried) { m_retried = true; resolveCurrent(); }
        });

        m_player = new QMediaPlayer(this);
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);

        // Discord Rich Presence. It re-syncs on every player event; the timer also corrects any drift
        // (for example when a stream stalls and buffers) and clears the status if nothing plays for a while.
        m_rpc = new DiscordRpc(this);
        m_rpcTimer.setInterval(5000);
        connect(&m_rpcTimer, &QTimer::timeout, this, [this] { rpcSync(); });
        m_rpcTimer.start();

        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onState);
        connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPosition);
        connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDuration);
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::EndOfMedia) onEnded();
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& msg) {
            const Track* t = trackById(m_currentId);
            // Stream URLs expire / get rejected: re-resolve once before giving up.
            if (t && t->online && !m_retried && !m_resolving) {
                m_retried = true;
                resolveCurrent();
                return;
            }
            if (skipUnplayable(msg.isEmpty() ? QStringLiteral("can't be played") : msg)) return;
            m_barArtist->setText(msg.isEmpty() ? QStringLiteral("Can't play this file") : msg);
            if (t && t->online) setOnlineStatus(QStringLiteral("Playback error: ") + msg, true);
        });

        m_ui.setInterval(16);
        m_ui.setTimerType(Qt::PreciseTimer);
        connect(&m_ui, &QTimer::timeout, this, &MainWindow::tick);
    }

    void buildShortcuts()
    {
        auto add = [this](const QKeySequence& k, std::function<void()> fn) {
            auto* s = new QShortcut(k, this);
            connect(s, &QShortcut::activated, this, [fn] { fn(); });
        };
        add(QKeySequence(Qt::Key_Space), [this] { togglePlay(); });
        add(QKeySequence(Qt::Key_Right), [this] { seekTo(m_player->position() + 5000); });
        add(QKeySequence(Qt::Key_Left),  [this] { seekTo(qMax<qint64>(0, m_player->position() - 5000)); });
        add(QKeySequence(Qt::Key_Escape), [this] { if (m_sheet->isOpen()) m_sheet->closeSheet(); });
        add(QKeySequence(Qt::CTRL | Qt::Key_O), [this] { addFiles(); });
        add(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { createPlaylist(-1); });
        add(QKeySequence(Qt::CTRL | Qt::Key_I), [this] { openImportDialog(); });
        add(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { openThemeDialog(); });
        add(QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
            QLineEdit* e = (m_stack->currentIndex() == TabLibrary) ? m_search : m_osearch;
            if (m_stack->currentIndex() == TabPlaylist) showTab(TabHome);
            if (e == m_osearch && m_stack->currentIndex() != TabHome) showTab(TabHome);
            e->setFocus();
            e->selectAll();
        });
        // Ctrl+Shift+I: toggle the Now Playing info panel
        add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this] { if (m_infoHost) m_infoHost->toggle(); });
        // Ctrl+L: lyrics
        add(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { toggleLyrics(); });
    }

    void restore()
    {
        QSettings s;
        restoreGeometry(s.value("geometry").toByteArray());
        const qreal v = s.value("volume", 0.7).toReal();
        m_preMute = v;
        m_volume->setValue(v);
        applyVolume(v);

        QStringList ok;
        for (const QString& p : s.value("paths").toStringList())
            if (QFileInfo::exists(p)) ok << p;
        addPaths(ok);
        updateCount();

        m_store.load();
        rebuildPlaylistList();

        const bool fresh = loadHomeCache();
        for (int i = 0; i < kDiscoverCount; ++i)
            if (!m_discItems[i].isEmpty()) fillShelf(m_disc[i], m_discItems[i], QString::fromUtf8(kDiscover[i].title));
        if (!m_moreItems.isEmpty()) {
            m_shMore->setTitle(QStringLiteral("More like %1").arg(m_moreArtist));
            fillShelf(m_shMore, m_moreItems, QStringLiteral("more"));
        }
        refreshHomePersonal();

        showTab(TabHome);                       // Home is always the first thing you see
        refreshToolState();
        if (m_yt->available() && !fresh) fetchHome();
        // Load Deezer chart data for trending songs on the Home page
        if (m_charts) m_charts->loadHome();
        // Prune old audio cache entries
        Yt::pruneAudioCache();
    }

    // =====================================================================
    //  Tabs
    // =====================================================================
    void showTab(int i)
    {
        i = qBound(0, i, 2);
        m_stack->setCurrentIndex(i);
        m_navHome->setSelected(i == TabHome);
        m_navLib->setSelected(i == TabLibrary);
        updatePlaylistSelection();
        if (i == TabHome) {
            refreshToolState();
            if (m_homeDirty) refreshHomePersonal();
        }
    }

    // Home button / sidebar Home: back to the recommendations feed
    void showHome()
    {
        m_searchDebounce.stop();
        m_yt->cancelSearch();
        m_searchTok = 0;
        if (!m_osearch->text().isEmpty()) {
            QSignalBlocker b(m_osearch);
            m_osearch->clear();
        }
        m_oStack->setCurrentIndex(0);
        setOnlineStatus(QString());
        m_homeScroll->scrollToTop();
        showTab(TabHome);
    }

    // =====================================================================
    //  Online: search, shelves, tool state
    // =====================================================================
    void runSearch()
    {
        const QString q = m_osearch->text().simplified();
        if (q.isEmpty()) { showHome(); return; }
        if (m_stack->currentIndex() != TabHome) showTab(TabHome);
        m_searchDebounce.stop();
        m_lastQuery = q;
        if (!m_yt->available()) {
            // YouTube search needs yt-dlp, but your own playlists can still be found without it
            showBanner(Banner::Setup);
            m_searchTok = 0;
            m_smodel->clear();
            refreshSearchPage();
            if (!m_plResIds.isEmpty()) { m_oStack->setCurrentIndex(1); m_searchScroll->scrollToTop(); }
            return;
        }
        showBanner(Banner::None);
        m_searchFirst = true;
        m_oStack->setCurrentIndex(1);
        m_searchScroll->scrollToTop();
        refreshSearchPlaylists();
        m_searchTok = m_yt->search(q, 30);
        setOnlineStatus(QStringLiteral("Searching YouTube..."));
    }

    // Your own playlists whose name (or a song in them) matches the search text; name matches come first.
    QStringList matchingPlaylists(const QString& query) const
    {
        QStringList byName, bySong;
        const QStringList words = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.isEmpty()) return {};
        auto matches = [&words](const QString& hay) {
            for (const QString& w : words)
                if (!hay.contains(w, Qt::CaseInsensitive)) return false;
            return true;
        };
        for (const Playlist& pl : m_store.playlists()) {
            if (matches(pl.name)) { byName << pl.id; continue; }
            for (const PlaylistEntry& e : pl.entries)
                if (matches(e.title + QLatin1Char(' ') + e.artist)) { bySong << pl.id; break; }
        }
        return byName + bySong;
    }

    void refreshSearchPlaylists(bool force = false)
    {
        const QString q = m_osearch->text().simplified();
        const QStringList ids = q.isEmpty() ? QStringList() : matchingPlaylists(q);
        if (!force && ids == m_plResIds) return;        // streaming results call this often; don't rebuild needlessly
        QList<MediaCard*> cards;
        for (const QString& id : ids)
            if (const Playlist* pl = m_store.find(id)) cards.push_back(makePlaylistCard(*pl, MediaCard::Card));
        m_plResIds = ids;
        m_shPlRes->setCards(cards);
        m_shPlRes->setVisible(!cards.isEmpty());
    }

    void refreshSearchPage()
    {
        refreshSearchPlaylists();
        const int n = m_smodel->rowCount();
        m_heroWrap->setVisible(n > 0);
        if (n > 0) {
            const Track& t = m_smodel->at(0);
            m_topCard->setTrackId(t.id);
            m_topCard->setTexts(t.title, t.artist);
            m_topCard->setBadge(QStringLiteral("Song"));
            m_topCard->setCover(!t.cover.isNull() ? t.cover : t.thumb);
            const bool cur = (t.id == m_currentId);
            m_topCard->setCurrent(cur, cur && m_player->playbackState() == QMediaPlayer::PlayingState);
        }
        const QVector<int> queue = m_smodel->ids();
        QVector<int> rest;
        for (int i = 4; i < queue.size(); ++i) rest.push_back(queue[i]);
        if (rest != m_moreIds) {
            m_moreIds = rest;
            QList<MediaCard*> cards;
            for (int id : rest)
                if (const Track* t = trackById(id)) cards.push_back(makeTrackCard(MediaCard::Card, *t, queue));
            m_shMoreRes->setCards(cards);
        }
        m_shMoreRes->setVisible(!rest.isEmpty());
    }

    void setOnlineStatus(const QString& t, bool error = false)
    {
        sty(m_oStatus, QStringLiteral("font-size:13px;color:%1;background:transparent;")
                                     .arg(error ? "#ff8a8a" : "#c8c8c8"));
        m_oStatus->setText(t);
        m_oStatus->setVisible(!t.isEmpty());
    }

    void refreshToolState()
    {
        if (m_yt->available()) { if (m_bannerMode == Banner::Setup) showBanner(Banner::None); }
        else showBanner(Banner::Setup);
    }

    void showBanner(Banner mode, const QString& busyText = QString())
    {
        m_bannerMode = mode;
        m_banner->setVisible(mode != Banner::None);
        m_bannerA->setVisible(mode == Banner::Setup || mode == Banner::Update);
        m_bannerB->setVisible(mode == Banner::Setup || mode == Banner::Update);
        switch (mode) {
        case Banner::Setup:
            m_bannerText->setText(QStringLiteral("YouTube streaming needs yt-dlp, a free open-source tool. Pulsify can download it for you."));
            m_bannerA->setText(QStringLiteral("Install automatically"));
            m_bannerB->setText(QStringLiteral("I have it..."));
            break;
        case Banner::Update:
            m_bannerText->setText(QStringLiteral("Something went wrong talking to YouTube. Updating yt-dlp usually fixes it."));
            m_bannerA->setText(QStringLiteral("Update yt-dlp"));
            m_bannerB->setText(QStringLiteral("Dismiss"));
            break;
        case Banner::Busy:
            m_bannerText->setText(busyText);
            break;
        case Banner::None:
            break;
        }
    }

    // ---- Home shelves: fetch, cache, fill
    static QString homeCachePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/home-cache.json");
    }

    // returns true when the cache is recent enough to skip refreshing
    bool loadHomeCache()
    {
        QFile f(homeCachePath());
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const qint64 when = qint64(root.value(QLatin1String("time")).toDouble());
        const QJsonObject shelves = root.value(QLatin1String("shelves")).toObject();
        for (int i = 0; i < kDiscoverCount; ++i) {
            QVector<OnlineItem> v;
            for (const QJsonValue& x : shelves.value(QString::fromUtf8(kDiscover[i].title)).toArray()) {
                const OnlineItem it = Yt::fromStored(x.toObject());
                if (it.valid()) v.push_back(it);
            }
            m_discItems[i] = v;
        }
        const QJsonObject more = root.value(QLatin1String("more")).toObject();
        m_moreArtist = more.value(QLatin1String("artist")).toString();
        m_moreItems.clear();
        for (const QJsonValue& x : more.value(QLatin1String("items")).toArray()) {
            const OnlineItem it = Yt::fromStored(x.toObject());
            if (it.valid()) m_moreItems.push_back(it);
        }
        return QDateTime::currentMSecsSinceEpoch() - when < qint64(8) * 3600 * 1000;
    }

    void saveHomeCache() const
    {
        QJsonObject shelves;
        for (int i = 0; i < kDiscoverCount; ++i) {
            QJsonArray a;
            for (const OnlineItem& it : m_discItems[i]) a.push_back(Yt::toJson(it));
            shelves.insert(QString::fromUtf8(kDiscover[i].title), a);
        }
        QJsonArray ma;
        for (const OnlineItem& it : m_moreItems) ma.push_back(Yt::toJson(it));
        QJsonObject more;
        more.insert(QStringLiteral("artist"), m_moreArtist);
        more.insert(QStringLiteral("items"), ma);
        QJsonObject root;
        root.insert(QStringLiteral("time"), double(QDateTime::currentMSecsSinceEpoch()));
        root.insert(QStringLiteral("shelves"), shelves);
        root.insert(QStringLiteral("more"), more);

        QDir().mkpath(QFileInfo(homeCachePath()).absolutePath());
        QSaveFile f(homeCachePath());
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }

    void fetchHome()
    {
        if (!m_yt->available()) { showBanner(Banner::Setup); return; }
        m_yt->cancelFetches();
        m_fetchMap.clear();
        m_fetchFails = 0;
        m_cacheDirty = false;

        for (int i = 0; i < kDiscoverCount; ++i) {
            if (m_discItems[i].isEmpty()) m_disc[i]->setSkeleton(8);
            m_fetchMap.insert(m_yt->fetch(QString::fromUtf8(kDiscover[i].query), 30), i);
        }
        const QString artist = topRecentArtist();
        if (!artist.isEmpty()) {
            if (artist != m_moreArtist) { m_moreItems.clear(); m_moreArtist = artist; }
            m_shMore->setTitle(QStringLiteral("More like %1").arg(artist));
            if (m_moreItems.isEmpty()) m_shMore->setSkeleton(8);
            m_fetchMap.insert(m_yt->fetch(artist + QStringLiteral(" songs"), 30), -1);
        }
        m_fetchTotal = int(m_fetchMap.size());
        m_refreshBtn->setEnabled(false);
        m_refreshBtn->setText(QStringLiteral("Refreshing..."));
        applyHomeVisibility();
    }

    void finishHomeFetch()
    {
        m_refreshBtn->setEnabled(true);
        m_refreshBtn->setText(QStringLiteral("Refresh"));
        if (m_cacheDirty) saveHomeCache();
        m_cacheDirty = false;
        if (m_fetchTotal > 0 && m_fetchFails >= m_fetchTotal) {
            setOnlineStatus(QStringLiteral("Couldn't load recommendations. Check your connection, or update yt-dlp."), true);
            showBanner(Banner::Update);
        }
    }

    QString topRecentArtist() const
    {
        for (const PlaylistEntry& e : m_store.recent()) {
            const QString a = e.artist.trimmed();
            if (a.isEmpty() || a == QLatin1String("Unknown artist") || a == QLatin1String("YouTube")) continue;
            return a.section(QLatin1String(", "), 0, 0);
        }
        return QString();
    }

    void populateChartShelf()
    {
        // Adds a "Trending songs" shelf to the Home page from Deezer chart data.
        if (m_chartSongs.isEmpty() || m_disc.isEmpty()) return;
        // Find the last discover shelf that has no content and repurpose it.
        for (int i = int(m_disc.size()) - 1; i >= 0; --i) {
            if (!m_disc[i]->hasContent()) {
                QVector<Track> ts;
                for (const Track& s : m_chartSongs) {
                    if (ts.size() >= 24) break;
                    // Chart tracks are "pending": they have a query but no videoId yet.
                    // Register them in m_known so the player can find them.
                    int id = m_nextOnlineId++;
                    Track t = s;
                    t.id = id;
                    m_known.emplace(id, t);
                    ts.push_back(t);
                }
                QVector<int> queue;
                for (const Track& t : ts) queue.push_back(t.id);
                QList<MediaCard*> cards;
                for (const Track& t : ts) cards.push_back(makeTrackCard(MediaCard::Card, t, queue));
                m_disc[i]->setTitle(QStringLiteral("Trending songs"));
                m_disc[i]->setCards(cards);
                applyHomeVisibility();
                // Load thumbnails for chart tracks
                for (const Track& t : ts)
                    if (!t.thumbUrl.isEmpty()) m_thumbs->request(t.id, { t.thumbUrl }, 100);
                break;
            }
        }
    }

    void fillShelf(Shelf* sh, const QVector<OnlineItem>& items, const QString&)
    {
        QVector<Track> ts;
        for (const OnlineItem& it : items) {
            if (ts.size() >= 24) break;
            ts.push_back(trackFromItem(it));
        }
        QVector<int> queue;
        for (const Track& t : ts) queue.push_back(t.id);
        QList<MediaCard*> cards;
        for (const Track& t : ts) cards.push_back(makeTrackCard(MediaCard::Card, t, queue));
        sh->setCards(cards);
        applyHomeVisibility();
    }

    // Decides which Home sections show, based on the chip and on what has content.
    void applyHomeVisibility()
    {
        const bool all = (m_chip == 0);
        bool any = false;
        auto vis = [&any](QWidget* w, bool show) { w->setVisible(show); any = any || show; };
        vis(m_tilesWrap, all && m_tiles->count() > 0);
        vis(m_shPlaylists, (all || m_chip == 1) && m_shPlaylists->hasContent());
        vis(m_shRecent, (all || m_chip == 2) && m_shRecent->hasContent());
        vis(m_shMore, (all || m_chip == 3) && m_shMore->hasContent());
        for (Shelf* s : m_disc) vis(s, (all || m_chip == 3) && s->hasContent());

        QString msg;
        if (m_chip == 1)            msg = QStringLiteral("No playlists yet. Use \"+ Create\" in the sidebar.");
        else if (m_chip == 2)       msg = QStringLiteral("Nothing played yet. Songs you play show up here.");
        else if (!m_yt->available()) msg = QStringLiteral("Set up YouTube streaming with the banner above to discover music here. Your library and playlists keep working offline.");
        else                        msg = QStringLiteral("Nothing to show yet. Search above, or press Refresh.");
        m_homeEmpty->setText(msg);
        m_homeEmpty->setVisible(!any);
    }

    // Playlists, recently played and the quick-access tiles (cheap; rebuilt when they change)
    void refreshHomePersonal()
    {
        m_homeDirty = false;

        QList<MediaCard*> pcs;
        int n = 0;
        for (const Playlist& pl : m_store.playlists()) {
            if (n++ >= 24) break;
            pcs.push_back(makePlaylistCard(pl, MediaCard::Card));
        }
        m_shPlaylists->setCards(pcs);

        const QVector<Track> rec = recentTracks(24);
        QVector<int> rq;
        for (const Track& t : rec) rq.push_back(t.id);
        QList<MediaCard*> rcs;
        for (const Track& t : rec) rcs.push_back(makeTrackCard(MediaCard::Card, t, rq));
        m_shRecent->setCards(rcs);

        refreshTiles(rec);
        applyHomeVisibility();
    }

    void refreshTiles(const QVector<Track>& rec)
    {
        const QVector<Playlist>& pls = m_store.playlists();
        const int np = qMin<int>(4, int(pls.size()));
        QVector<Track> ts;
        QSet<int> used;
        for (const Track& t : rec) {
            if (np + ts.size() >= 8) break;
            ts.push_back(t);
            used.insert(t.id);
        }
        // brand-new install: borrow a few suggestions so the top of Home isn't empty
        if (np + ts.size() < 6 && !m_discItems.isEmpty()) {
            for (const OnlineItem& it : m_discItems[0]) {
                if (np + ts.size() >= 6) break;
                const Track t = trackFromItem(it);
                if (used.contains(t.id)) continue;
                used.insert(t.id);
                ts.push_back(t);
            }
        }
        QVector<int> q;
        for (const Track& t : ts) q.push_back(t.id);
        QList<MediaCard*> tiles;
        for (int i = 0; i < np; ++i) tiles.push_back(makePlaylistCard(pls[i], MediaCard::Tile));
        for (const Track& t : ts) tiles.push_back(makeTrackCard(MediaCard::Tile, t, q));
        m_tiles->setCards(tiles);
    }

    void markHomeDirty()
    {
        m_homeDirty = true;
        if (m_stack && m_stack->currentIndex() == TabHome && m_oStack->currentIndex() == 0) refreshHomePersonal();
    }

    QVector<Track> recentTracks(int maxCount)
    {
        QVector<Track> out;
        const QVector<PlaylistEntry> rec = m_store.recent();
        for (const PlaylistEntry& e : rec) {
            if (out.size() >= maxCount) break;
            const Track* t = entryTrack(e);
            if (!t) continue;
            if (t->online) ensureThumb(t->id);
            out.push_back(*t);
        }
        return out;
    }

    // ---- card factories
    MediaCard* makeTrackCard(MediaCard::Layout layout, const Track& t, const QVector<int>& queue)
    {
        auto* c = new MediaCard(layout, MediaCard::TrackCard);
        c->setTrackId(t.id);
        c->setTexts(t.title, t.artist);
        c->setCover(!t.cover.isNull() ? t.cover : t.thumb);
        const bool cur = (t.id == m_currentId);
        c->setCurrent(cur, cur && m_player->playbackState() == QMediaPlayer::PlayingState);
        const int id = t.id;
        auto act = [this, id, queue] { playFromCard(id, queue); };
        connect(c, &MediaCard::activated, this, act);
        connect(c, &MediaCard::playClicked, this, act);
        connect(c, &MediaCard::contextRequested, this, [this, id, queue](const QPoint& gp) {
            showTrackMenu(id, gp, MenuFrom::Other, queue);
        });
        return c;
    }

    MediaCard* makePlaylistCard(const Playlist& pl, MediaCard::Layout layout)
    {
        auto* c = new MediaCard(layout, MediaCard::PlaylistCard);
        c->setPlaylistId(pl.id);
        c->setTexts(pl.name, playlistSubtitle(pl));
        c->setCovers(playlistPixmaps(pl));
        const bool cur = (m_src == Src::Playlist && m_srcKey == pl.id && m_currentId >= 0);
        c->setCurrent(cur, cur && m_player->playbackState() == QMediaPlayer::PlayingState);
        const QString id = pl.id;
        connect(c, &MediaCard::activated, this, [this, id] { openPlaylist(id); });
        connect(c, &MediaCard::playClicked, this, [this, id] {
            if (m_src == Src::Playlist && m_srcKey == id && m_currentId >= 0) togglePlay();
            else playPlaylist(id);
        });
        connect(c, &MediaCard::contextRequested, this, [this, id](const QPoint& gp) { showPlaylistMenu(id, gp); });
        return c;
    }

    void playFromCard(int id, const QVector<int>& queue)
    {
        if (id < 0) return;
        if (id == m_currentId) { if (!m_resolving) togglePlay(); return; }
        playFrom(queue, id, Src::Other);
    }

    void updateCardStates()
    {
        const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        const QList<MediaCard*> cards = m_onlinePage->findChildren<MediaCard*>();
        for (MediaCard* c : cards) {
            if (c->kind() == MediaCard::TrackCard) {
                const bool cur = c->trackId() >= 0 && c->trackId() == m_currentId;
                c->setCurrent(cur, cur && playing);
            } else {
                const bool cur = m_currentId >= 0 && m_src == Src::Playlist && m_srcKey == c->playlistId();
                c->setCurrent(cur, cur && playing);
            }
        }
    }

    void refreshCardsFor(int id)
    {
        const Track* t = trackById(id);
        if (!t) return;
        const QList<MediaCard*> cards = m_onlinePage->findChildren<MediaCard*>();
        for (MediaCard* c : cards) {
            if (c->kind() != MediaCard::TrackCard || c->trackId() != id) continue;
            c->setTexts(t->title, t->artist);
            c->setCover(!t->cover.isNull() ? t->cover : t->thumb);
        }
    }

    // =====================================================================
    //  Track registry (online tracks + files that only live in playlists)
    // =====================================================================
    int onlineIdFor(const QString& vid)
    {
        const auto it = m_vidIds.constFind(vid);
        if (it != m_vidIds.constEnd()) return it.value();
        const int id = m_nextOnlineId++;
        m_vidIds.insert(vid, id);
        return id;
    }

    Track* knownTrack(int id)
    {
        const auto it = m_known.find(id);
        return it == m_known.end() ? nullptr : &it->second;
    }

    Track* ensureOnlineTrack(const QString& vid, const QString& title, const QString& artist,
                             const QString& album, qint64 dur)
    {
        const int id = onlineIdFor(vid);
        if (Track* t = knownTrack(id)) {
            if (t->durationMs <= 0 && dur > 0) t->durationMs = dur;
            return t;
        }
        Track t;
        t.id = id;
        t.online = true;
        t.videoId = vid;
        t.path = QStringLiteral("https://www.youtube.com/watch?v=") + vid;
        t.title = title.isEmpty() ? QStringLiteral("Unknown title") : title;
        t.artist = artist.isEmpty() ? QStringLiteral("YouTube") : artist;
        t.album = album;
        t.durationMs = dur;
        t.thumbUrl = Yt::thumbUrl(vid, "mqdefault");
        return &m_known.emplace(id, std::move(t)).first->second;
    }

    Track trackFromItem(const OnlineItem& it)
    {
        Track* t = ensureOnlineTrack(it.id, it.title, it.artist, it.album, it.durationMs);
        ensureThumb(t->id);
        return *t;
    }

    void ensureThumb(int id)
    {
        Track* t = knownTrack(id);
        if (!t || !t->online || !t->cover.isNull() || m_thumbAsked.contains(id)) return;
        m_thumbAsked.insert(id);
        m_thumbs->request(id, { t->thumbUrl }, 100);
    }

    const Track* localTrackForEntry(const PlaylistEntry& e)
    {
        const int lid = m_model->idForPath(e.path);
        if (lid > 0) return m_model->byId(lid);
        if (!QFileInfo::exists(e.path)) return nullptr;
        const QString abs = QFileInfo(e.path).absoluteFilePath();
        int id = m_extraIds.value(abs, -1);
        if (id < 0) {
            id = m_nextExtraId++;
            m_extraIds.insert(abs, id);
            Track t;
            t.id = id;
            t.path = abs;
            t.title = e.title.isEmpty() ? QFileInfo(abs).completeBaseName() : e.title;
            t.artist = e.artist.isEmpty() ? QStringLiteral("Unknown artist") : e.artist;
            t.album = e.album;
            t.durationMs = e.durationMs;
            m_known.emplace(id, std::move(t));
            m_loader->enqueue(id, abs);
        }
        return knownTrack(id);
    }

    const Track* entryTrack(const PlaylistEntry& e)
    {
        if (e.online) return ensureOnlineTrack(e.videoId, e.title, e.artist, e.album, e.durationMs);
        return localTrackForEntry(e);
    }

    static PlaylistEntry entryFromTrack(const Track& t)
    {
        PlaylistEntry e;
        e.online = t.online;
        if (t.online) e.videoId = t.videoId;
        else          e.path = t.path;
        e.title = t.title;
        e.artist = t.artist;
        e.album = t.album;
        e.durationMs = t.durationMs;
        return e;
    }

    // A track from the library, the registry, or the open playlist.
    const Track* trackById(int id) const
    {
        if (const Track* t = m_model->byId(id)) return t;
        const auto it = m_known.find(id);
        if (it != m_known.end()) return &it->second;
        if (const Track* t = m_pmodel->byId(id)) return t;
        return nullptr;
    }

    // Applies tags / artwork to every place that holds a copy of the track.
    void applyMetaEverywhere(const MetaResult& r)
    {
        m_model->applyMeta(r);
        if (Track* t = knownTrack(r.id)) TrackModel::mergeMeta(*t, r);
        m_smodel->applyMeta(r);
        m_pmodel->applyMeta(r);
        refreshCardsFor(r.id);
        if (!m_coverTimer.isActive()) m_coverTimer.start(150);
        if (r.id == m_currentId) refreshNowPlaying();
    }

    // =====================================================================
    //  Playlists
    // =====================================================================
    QString playlistSubtitle(const Playlist& pl) const
    {
        const int n = int(pl.entries.size());
        return QStringLiteral("Playlist \u00b7 %1 song%2").arg(n).arg(n == 1 ? "" : "s");
    }

    QVector<QPixmap> playlistPixmaps(const Playlist& pl, int maxCount = 4)
    {
        QVector<QPixmap> v;
        int looked = 0;
        for (const PlaylistEntry& e : pl.entries) {
            if (v.size() >= maxCount || ++looked > 14) break;
            const Track* t = entryTrack(e);
            if (!t) continue;
            if (t->online) ensureThumb(t->id);
            const QPixmap& pm = !t->cover.isNull() ? t->cover : t->thumb;
            if (!pm.isNull()) v.push_back(pm);
        }
        return v;
    }

    void rebuildPlaylistList()
    {
        for (PlaylistRow* r : m_plRows) { r->hide(); r->deleteLater(); }
        m_plRows.clear();
        for (const Playlist& pl : m_store.playlists()) {
            auto* row = new PlaylistRow(pl.id);
            row->setInfo(pl.name, playlistSubtitle(pl));
            row->setCovers(playlistPixmaps(pl));
            const QString id = pl.id;
            connect(row, &QAbstractButton::clicked, this, [this, id] { openPlaylist(id); });
            connect(row, &PlaylistRow::contextRequested, this, [this, id](const QPoint& gp) { showPlaylistMenu(id, gp); });
            connect(row, &PlaylistRow::trackDropped, this, [this, id](int trackId) { addTrackToPlaylist(id, trackId); });
            m_plRowsLayout->insertWidget(m_plRowsLayout->count() - 1, row);
            m_plRows.push_back(row);
        }
        m_plEmpty->setVisible(m_plRows.isEmpty());
        m_plScroll->setVisible(!m_plRows.isEmpty());
        updatePlaylistSelection();
    }

    void updatePlaylistSelection()
    {
        if (!m_stack) return;
        for (PlaylistRow* r : m_plRows) {
            r->setSelected(m_stack->currentIndex() == TabPlaylist && r->playlistId() == m_openPl);
            r->setPlayingFrom(m_currentId >= 0 && m_src == Src::Playlist && m_srcKey == r->playlistId());
        }
    }

    // Names, counts and artwork only (no widgets are recreated).
    void refreshPlaylistVisuals()
    {
        for (PlaylistRow* r : m_plRows) {
            if (const Playlist* pl = m_store.find(r->playlistId())) {
                r->setInfo(pl->name, playlistSubtitle(*pl));
                r->setCovers(playlistPixmaps(*pl));
            }
        }
        const QList<MediaCard*> cards = m_onlinePage->findChildren<MediaCard*>();
        for (MediaCard* c : cards) {
            if (c->kind() != MediaCard::PlaylistCard) continue;
            if (const Playlist* pl = m_store.find(c->playlistId())) {
                c->setTexts(pl->name, playlistSubtitle(*pl));
                c->setCovers(playlistPixmaps(*pl));
            }
        }
        if (!m_openPl.isEmpty()) updatePlaylistHeader();
        if (m_oStack && m_shPlRes && m_oStack->currentIndex() == 1) refreshSearchPlaylists(true);   // renamed / deleted / changed
    }

    // id >= 0: also put that song in the new playlist (and stay where we are)
    void createPlaylist(int trackId)
    {
        QString name;
        if (!NameDialog::ask(this, QStringLiteral("Create playlist"), m_store.nextDefaultName(),
                             QStringLiteral("Create"), name)) return;
        const QString id = m_store.create(name);
        rebuildPlaylistList();
        markHomeDirty();
        if (trackId >= 0) addTrackToPlaylist(id, trackId);
        else              openPlaylist(id);
    }

    void renamePlaylist(const QString& id)
    {
        const Playlist* pl = m_store.find(id);
        if (!pl) return;
        QString name;
        if (!NameDialog::ask(this, QStringLiteral("Rename playlist"), pl->name, QStringLiteral("Save"), name)) return;
        m_store.rename(id, name);
        refreshPlaylistVisuals();
        markHomeDirty();
    }

    void deletePlaylist(const QString& id)
    {
        const Playlist* pl = m_store.find(id);
        if (!pl) return;
        QMessageBox box(QMessageBox::Question, QStringLiteral("Delete playlist"),
                        QStringLiteral("Delete \"%1\"? This can't be undone.").arg(pl->name),
                        QMessageBox::NoButton, this);
        QPushButton* del = box.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
        box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != del) return;

        m_store.remove(id);
        if (m_openPl == id) {
            m_openPl.clear();
            m_pmodel->clear();
            m_plEntryIdx.clear();
            showTab(TabHome);
        }
        rebuildPlaylistList();
        markHomeDirty();
        showToast(QStringLiteral("Playlist deleted"));
    }

    void openPlaylist(const QString& id)
    {
        const Playlist* pl = m_store.find(id);
        if (!pl) return;
        m_openPl = id;
        loadPlaylistModel(*pl);
        updatePlaylistHeader();
        showTab(TabPlaylist);
    }

    void loadPlaylistModel(const Playlist& pl)
    {
        m_pmodel->clear();
        m_plEntryIdx.clear();
        QVector<Track> ts;
        for (int i = 0; i < pl.entries.size(); ++i) {
            const Track* t = entryTrack(pl.entries[i]);
            if (!t) continue;                       // file no longer exists: kept in the playlist, just not shown
            if (t->online) ensureThumb(t->id);
            ts.push_back(*t);
            m_plEntryIdx.push_back(i);
        }
        const QVector<int> ids = m_pmodel->appendTracks(ts);
        m_pview->appearIds(ids);
    }

    void updatePlaylistHeader()
    {
        const Playlist* pl = m_store.find(m_openPl);
        if (!pl) return;
        m_plName->setText(pl->name, false);
        const int n = int(pl->entries.size());
        QString meta = QStringLiteral("%1 song%2").arg(n).arg(n == 1 ? "" : "s");
        const qint64 ms = m_pmodel->totalDuration();
        if (ms > 0) {
            const qint64 mins = ms / 60000;
            meta += mins >= 60 ? QStringLiteral(" \u00b7 %1 hr %2 min").arg(mins / 60).arg(mins % 60)
                               : QStringLiteral(" \u00b7 %1 min").arg(qMax<qint64>(1, mins));
        }
        m_plMeta->setText(meta);
        m_plCover->setCovers(playlistPixmaps(*pl));
        m_pview->setEmptyText(m_pmodel->rowCount() == 0
            ? QStringLiteral("This playlist is empty.\nRight-click a song and choose \"Add to playlist\",\nor drag songs onto it in the sidebar.")
            : QString());
    }

    void addTrackToPlaylist(const QString& plId, int trackId)
    {
        const Track* t = trackById(trackId);
        const Playlist* pl = m_store.find(plId);
        if (!t || !pl) return;
        const QString name = pl->name;
        if (!m_store.add(plId, entryFromTrack(*t))) {
            showToast(QStringLiteral("Already in %1").arg(name));
            return;
        }
        showToast(QStringLiteral("Added to %1").arg(name));
        syncQueueToPlaylist(plId);
        if (plId == m_openPl) {
            const Playlist* p2 = m_store.find(plId);
            if (p2 && !p2->entries.isEmpty()) {
                if (const Track* nt = entryTrack(p2->entries.last())) {
                    if (nt->online) ensureThumb(nt->id);
                    m_pmodel->appendTracks(QVector<Track>{ *nt });
                    m_plEntryIdx.push_back(int(p2->entries.size()) - 1);
                    m_pview->appearIds({ nt->id });
                }
            }
        }
        refreshPlaylistVisuals();
        markHomeDirty();
    }

    void removeFromOpenPlaylist(int trackId)
    {
        const int row = m_pmodel->rowOf(trackId);
        if (row < 0 || row >= m_plEntryIdx.size()) return;
        const int entryIdx = m_plEntryIdx[row];
        m_store.removeAt(m_openPl, entryIdx);
        m_pmodel->removeAtRow(row);
        m_plEntryIdx.removeAt(row);
        for (int& k : m_plEntryIdx) if (k > entryIdx) --k;
        syncQueueToPlaylist(m_openPl);
        updatePlaylistHeader();
        refreshPlaylistVisuals();
        markHomeDirty();
        showToast(QStringLiteral("Removed from playlist"));
    }

    void rebuildEntryIdx(const Playlist& pl)
    {
        m_plEntryIdx.clear();
        for (int i = 0; i < pl.entries.size(); ++i)
            if (entryTrack(pl.entries[i])) m_plEntryIdx.push_back(i);
    }

    void moveInOpenPlaylist(int trackId, int delta)
    {
        const int row = m_pmodel->rowOf(trackId);
        const int to = row + delta;
        if (row < 0 || to < 0 || row >= m_plEntryIdx.size() || to >= m_plEntryIdx.size()) return;
        if (!m_store.moveEntry(m_openPl, m_plEntryIdx[row], m_plEntryIdx[to])) return;
        m_pmodel->moveRow(row, to);
        if (const Playlist* pl = m_store.find(m_openPl)) rebuildEntryIdx(*pl);
        syncQueueToPlaylist(m_openPl);
        refreshPlaylistVisuals();
        markHomeDirty();
    }

    // ---- importing from YouTube / YouTube Music
    void openImportDialog()
    {
        ImportDialog dlg(m_google, m_yt, this);
        dlg.setHandler([this](const QString& name, const QString& source, const QVector<OnlineItem>& items) {
            return importPlaylist(name, source, items);
        });
        dlg.exec();
    }

    // Creates the playlist, or (re-import of the same one) only adds the songs that are new.
    QString importPlaylist(const QString& name, const QString& source, const QVector<OnlineItem>& items)
    {
        QVector<PlaylistEntry> es;
        es.reserve(items.size());
        for (const OnlineItem& it : items) {
            PlaylistEntry e;
            e.online = true;
            e.videoId = it.id;
            e.title = it.title;
            e.artist = it.artist;
            e.album = it.album;
            e.durationMs = it.durationMs;
            es.push_back(e);
        }
        QString msg, id;
        if (const Playlist* ex = m_store.findBySource(source)) {
            id = ex->id;
            const QString exName = ex->name;
            const int added = m_store.addMany(id, es);
            msg = added > 0 ? QStringLiteral("Updated \"%1\": %2 new song%3").arg(exName).arg(added).arg(added == 1 ? "" : "s")
                            : QStringLiteral("\"%1\" is already up to date").arg(exName);
        } else {
            id = m_store.createFrom(name, source, es);
            const int n = int(es.size());
            msg = QStringLiteral("Imported \"%1\": %2 song%3").arg(name).arg(n).arg(n == 1 ? "" : "s");
        }
        rebuildPlaylistList();
        if (m_openPl == id) {
            if (const Playlist* pl = m_store.find(id)) { loadPlaylistModel(*pl); updatePlaylistHeader(); }
        }
        syncQueueToPlaylist(id);
        markHomeDirty();
        return msg;
    }

    QVector<int> idsForPlaylist(const Playlist& pl)
    {
        QVector<int> v;
        for (const PlaylistEntry& e : pl.entries)
            if (const Track* t = entryTrack(e)) v.push_back(t->id);
        return v;
    }

    void playPlaylist(const QString& id)
    {
        const Playlist* pl = m_store.find(id);
        if (!pl) return;
        const QVector<int> v = idsForPlaylist(*pl);
        if (v.isEmpty()) { showToast(QStringLiteral("This playlist is empty")); return; }
        playFrom(v, m_shuffle ? v[QRandomGenerator::global()->bounded(int(v.size()))] : v.first(),
                 Src::Playlist, id);
    }

    // ---- menus
    AddMenu fillAddMenu(QMenu* m)
    {
        AddMenu am;
        am.create = m->addAction(QStringLiteral("New playlist..."));
        if (!m_store.playlists().isEmpty()) m->addSeparator();
        for (const Playlist& pl : m_store.playlists()) am.lists.insert(m->addAction(pl.name), pl.id);
        return am;
    }

    void handleAddChoice(const AddMenu& am, QAction* chosen, int trackId)
    {
        if (chosen == am.create) createPlaylist(trackId);
        else if (am.lists.contains(chosen)) addTrackToPlaylist(am.lists.value(chosen), trackId);
    }

    void showTrackMenu(int id, const QPoint& gp, MenuFrom from, const QVector<int>& ctxQueue = {})
    {
        const Track* t = trackById(id);
        if (!t) return;
        const bool online = t->online;
        const QString link = t->path;

        QMenu menu(this);
        QAction* play = menu.addAction(QStringLiteral("Play"));
        QAction* playNextA = menu.addAction(QStringLiteral("Play next"));
        QAction* addQ = menu.addAction(QStringLiteral("Add to queue"));
        menu.addSeparator();
        QMenu* sub = menu.addMenu(QStringLiteral("Add to playlist"));
        const AddMenu am = fillAddMenu(sub);
        QAction *rmPl = nullptr, *rmLib = nullptr, *copy = nullptr, *open = nullptr, *up = nullptr, *down = nullptr;
        if (from == MenuFrom::Playlist || from == MenuFrom::Library || online) menu.addSeparator();
        if (from == MenuFrom::Playlist) {
            const int row = m_pmodel->rowOf(id);
            up = menu.addAction(QStringLiteral("Move up"));
            down = menu.addAction(QStringLiteral("Move down"));
            up->setEnabled(row > 0);
            down->setEnabled(row >= 0 && row < m_pmodel->rowCount() - 1);
            rmPl = menu.addAction(QStringLiteral("Remove from this playlist"));
        }
        if (from == MenuFrom::Library)  rmLib = menu.addAction(QStringLiteral("Remove from library"));
        if (online) {
            copy = menu.addAction(QStringLiteral("Copy YouTube link"));
            open = menu.addAction(QStringLiteral("Open in browser"));
        }

        QAction* a = menu.exec(gp);
        if (!a) return;
        if (a == play) {
            switch (from) {
            case MenuFrom::Library:  playFrom(idsOf(m_proxy), id, Src::Library); break;
            case MenuFrom::Search:   playFrom(m_smodel->ids(), id, Src::Other); break;
            case MenuFrom::Playlist: playFrom(m_pmodel->ids(), id, Src::Playlist, m_openPl); break;
            case MenuFrom::Other:
                if (ctxQueue.contains(id)) playFrom(ctxQueue, id, Src::Other);
                else                       playFrom(QVector<int>{ id }, id, Src::Other);
                break;
            }
        }
        else if (a == playNextA) queueTrack(id, true);
        else if (a == addQ)  queueTrack(id, false);
        else if (a == up)    moveInOpenPlaylist(id, -1);
        else if (a == down)  moveInOpenPlaylist(id, +1);
        else if (a == rmPl)  removeFromOpenPlaylist(id);
        else if (a == rmLib) removeId(id);
        else if (a == copy)  QGuiApplication::clipboard()->setText(link);
        else if (a == open)  QDesktopServices::openUrl(QUrl(link));
        else                 handleAddChoice(am, a, id);
    }

    void openThemeDialog()
    {
        ThemeDialog dlg(this);
        dlg.exec();
    }

    // Widgets that were given fixed colours when they were built follow the theme here.
    void applyThemeColors()
    {
        m_heroPlay->setCircle(Theme::accent());
        m_plHeroPlay->setCircle(Theme::accent());
        m_homeBtn->setCircle(Theme::greyAt(0.055), Theme::fg());
        m_playBtn->setCircle(Theme::fg());
        m_searchAct->setIcon(searchIcon());
        update();
    }

    void updateAvatar()
    {
        const bool in = m_google->signedIn();
        m_avatar->setProfile(m_google->avatar(), m_google->accountName(), in);
        m_avatar->setToolTip(in ? (m_google->accountName().isEmpty()
                                       ? QStringLiteral("Signed in to Google")
                                       : QStringLiteral("Signed in as %1").arg(m_google->accountName()))
                                : QStringLiteral("Not signed in. Click to sign in with Google"));
    }

    void showAccountMenu()
    {
        QMenu menu(this);
        QAction* imp = nullptr;
        QAction* out = nullptr;
        QAction* thm = nullptr;
        if (m_google->signedIn()) {
            QString who = m_google->accountName();
            who.replace(QLatin1Char('&'), QStringLiteral("&&"));
            QAction* h = menu.addAction(who.isEmpty() ? QStringLiteral("Signed in to Google")
                                                      : QStringLiteral("Signed in as %1").arg(who));
            h->setEnabled(false);
            menu.addSeparator();
            imp = menu.addAction(QStringLiteral("Import playlists..."));
            out = menu.addAction(QStringLiteral("Sign out"));
        } else {
            imp = menu.addAction(QStringLiteral("Sign in with Google..."));
        }
        menu.addSeparator();
        thm = menu.addAction(QStringLiteral("Themes..."));
        QAction* a = menu.exec(m_avatar->mapToGlobal(QPoint(m_avatar->width() - 180, m_avatar->height() + 4)));
        if (!a) return;
        if (a == imp) openImportDialog();
        else if (a == thm) openThemeDialog();
        else if (a == out) { m_google->signOut(); showToast(QStringLiteral("Signed out of Google")); }
    }

    void showPlaylistMenu(const QString& id, const QPoint& gp)
    {
        if (!m_store.find(id)) return;
        QMenu menu(this);
        QAction* play = menu.addAction(QStringLiteral("Play"));
        QAction* open = menu.addAction(QStringLiteral("Open"));
        menu.addSeparator();
        QAction* ren = menu.addAction(QStringLiteral("Rename"));
        QAction* del = menu.addAction(QStringLiteral("Delete"));
        QAction* a = menu.exec(gp);
        if (!a) return;
        if (a == play) playPlaylist(id);
        else if (a == open) openPlaylist(id);
        else if (a == ren) renamePlaylist(id);
        else if (a == del) deletePlaylist(id);
    }

    void showToast(const QString& text) { m_toast->showMessage(text, 94 + 16); }

    void openSettingsDialog()
    {
        if (!m_settingsDialog) {
            m_settingsDialog = new SettingsDialog(m_google, m_rpc, this);
        }
        m_settingsDialog->show();
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
    }

    // =====================================================================
    //  Library
    // =====================================================================
    void addFiles()
    {
        QSettings s;
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Add music"), s.value("lastDir").toString(),
            QStringLiteral("Audio files (*.mp3 *.flac *.wav *.ogg *.m4a *.aac *.opus *.wma);;All files (*)"));
        if (files.isEmpty()) return;
        s.setValue("lastDir", QFileInfo(files.first()).absolutePath());
        showTab(TabLibrary);
        addPaths(files);
    }

    void addFolder()
    {
        QSettings s;
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Add folder"),
                                                              s.value("lastDir").toString());
        if (dir.isEmpty()) return;
        s.setValue("lastDir", dir);
        showTab(TabLibrary);
        addPaths(collectAudio({ dir }));
    }

    void addPaths(const QStringList& paths)
    {
        const QVector<int> ids = m_model->append(paths);
        if (ids.isEmpty()) return;
        for (int id : ids)
            if (const Track* t = m_model->byId(id)) m_loader->enqueue(id, t->path);
        m_view->appearIds(ids);
        savePaths();
    }

    void removeId(int id)
    {
        const bool cur = (id == m_currentId);
        if (cur) {
            m_player->stop();
            m_player->setSource(QUrl());
            m_currentId = -1;
        }
        m_history.removeAll(id);
        m_queue.removeAll(id);
        m_userQ.removeAll(id);
        const int oi = m_order.indexOf(id);
        if (oi >= 0) { m_order.removeAt(oi); if (oi <= m_pos) --m_pos; }
        m_model->remove(id);
        savePaths();
        if (cur) refreshNowPlaying();
    }

    void savePaths()
    {
        QSettings s;
        s.setValue("paths", m_model->paths());
    }

    void updateCount()
    {
        const int total = m_model->rowCount();
        const int shown = m_proxy->rowCount();
        const qint64 ms = m_model->totalDuration();
        QString dur;
        if (ms > 0) {
            const qint64 mins = ms / 60000;
            dur = mins >= 60 ? QStringLiteral(" \u00b7 %1 hr %2 min").arg(mins / 60).arg(mins % 60)
                             : QStringLiteral(" \u00b7 %1 min").arg(qMax<qint64>(1, mins));
        }
        if (shown == total)
            m_count->setText(QStringLiteral("%1 song%2%3").arg(total).arg(total == 1 ? "" : "s").arg(dur));
        else
            m_count->setText(QStringLiteral("%1 of %2 songs").arg(shown).arg(total));

        if (total == 0)
            m_view->setEmptyText(QStringLiteral("Your library is empty.\nDrop MP3 files here or use \"Add files\"."));
        else if (shown == 0)
            m_view->setEmptyText(QStringLiteral("No songs match your search."));
        else
            m_view->setEmptyText(QString());
    }

    static QVector<int> idsOf(const QAbstractItemModel* m)
    {
        QVector<int> v;
        v.reserve(m->rowCount());
        for (int r = 0; r < m->rowCount(); ++r) v.push_back(m->index(r, 0).data(IdRole).toInt());
        return v;
    }

    // =====================================================================
    //  Playback
    // =====================================================================
    // Starts a song and remembers the list it came from as the queue.
    void playFrom(const QVector<int>& queue, int id, Src src, const QString& key = QString())
    {
        m_queue = queue;
        m_userQ.clear();
        m_src = src;
        m_srcKey = key;
        rebuildOrder(id);
        playId(id);
    }

    // m_queue is the list as it is shown; m_order is the order it is actually played in: the same,
    // or a shuffle that starts with `firstId` and plays every song once before anything repeats.
    void rebuildOrder(int firstId)
    {
        m_order = m_queue;
        if (m_shuffle) {
            std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
            const int i = m_order.indexOf(firstId);
            if (i > 0) std::swap(m_order[0], m_order[i]);
            m_pos = (i >= 0) ? 0 : -1;
        } else {
            m_pos = m_order.indexOf(firstId);
        }
    }

    // Keeps the running queue in step with a playlist that is being edited while it plays.
    void syncQueueToPlaylist(const QString& plId)
    {
        if (m_src != Src::Playlist || m_srcKey != plId) return;
        const Playlist* pl = m_store.find(plId);
        if (!pl) return;
        const QVector<int> fresh = idsForPlaylist(*pl);
        const QSet<int> have(fresh.begin(), fresh.end());
        const int anchor = (m_pos >= 0 && m_pos < m_order.size()) ? m_order[m_pos] : -1;
        int before = 0;                                   // songs before the anchor that survived
        for (int i = 0; i < m_pos && i < m_order.size(); ++i) if (have.contains(m_order[i])) ++before;
        const bool anchorKept = (anchor >= 0 && have.contains(anchor));
        m_queue = fresh;
        if (!m_shuffle) {
            m_order = fresh;
            m_pos = anchorKept ? fresh.indexOf(anchor) : before - 1;
            return;
        }
        QVector<int> keep;                                // shuffled: keep the existing order
        for (int id : m_order) if (have.contains(id)) keep.push_back(id);
        m_pos = anchorKept ? keep.indexOf(anchor) : before - 1;
        const QSet<int> kept(keep.begin(), keep.end());
        for (int id : fresh) {                            // ...and slot new songs in somewhere ahead
            if (kept.contains(id)) continue;
            const int lo = m_pos + 1;
            const int at = lo + int(QRandomGenerator::global()->bounded(int(keep.size()) - lo + 1));
            keep.insert(at, id);
        }
        m_order = keep;
    }

    // "Play next" (front) / "Add to queue" (back). These play before the rest of the list continues.
    void queueTrack(int id, bool atFront)
    {
        if (!trackById(id)) return;
        if (m_currentId < 0) { playFrom(QVector<int>{ id }, id, Src::Other); return; }
        if (atFront) m_userQ.prepend(id); else m_userQ.append(id);
        showToast(atFront ? QStringLiteral("Playing next") : QStringLiteral("Added to queue"));
        prefetchNext();
    }

    void playId(int id, bool pushHistory = true, bool fromUserQ = false)
    {
        const Track* t = trackById(id);
        if (!t) return;
        const bool online = t->online;
        const QString videoId = t->videoId;
        const QString path = t->path;
        noteRecent(*t);

        if (!fromUserQ) {
            const int ix = m_order.indexOf(id);
            if (ix >= 0) m_pos = ix;
        }
        if (pushHistory && m_currentId >= 0 && m_currentId != id) {
            m_history.push_back(m_currentId);
            if (m_history.size() > 300) m_history.removeFirst();
        }
        m_currentId = id;
        m_lastPos = 0;
        m_posClock.restart();
        m_seek->setValue(0);
        m_lastSec = -1;
        setTimeLabel(0);
        m_yt->cancelResolve();
        m_resolving = false;

        if (online) {
            // Online: stop whatever is playing, then use the stream we already prefetched, or resolve a fresh one.
            m_player->stop();
            m_player->setSource(QUrl());
            m_retried = false;
            refreshNowPlaying();
            // Upgrade to a high-res square cover for the player bar / now-playing sheet.
            m_thumbs->request(id, { Yt::thumbUrl(videoId, "maxresdefault"),
                                    Yt::thumbUrl(videoId, "hq720"),
                                    Yt::thumbUrl(videoId, "mqdefault") }, 400);
            const auto hit = m_prefetched.constFind(videoId);
            if (hit != m_prefetched.constEnd()) {
                const Prefetched pf = hit.value();
                m_prefetched.erase(m_prefetched.find(videoId));
                if (QDateTime::currentSecsSinceEpoch() - pf.when < kStreamTtlSec) {
                    startStream(pf.url, pf.meta);
                    return;
                }
            }
            resolveCurrent();
            return;
        }
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
        refreshNowPlaying();
    }

    // Starts an already-resolved stream URL for the current (online) song.
    void startStream(const QString& url, const OnlineItem& meta)
    {
        // richer metadata straight from the video page (artist / track / album when YouTube Music has them)
        MetaResult r;
        r.id = m_currentId;
        r.title = meta.title;
        r.artist = meta.artist;
        r.album = meta.album;
        r.durationMs = meta.durationMs;
        applyMetaEverywhere(r);
        setOnlineStatus(QString());

        // Store stream info on the track for InfoPanel
        if (Track* t = knownTrack(m_currentId)) {
            t->webUrl = meta.stream.webUrl;
        }
        updateInfoPanel(meta.stream);

        // Route through the local relay so URL expiry and server throttling are handled transparently.
        m_relaySession = m_relay->open(url, meta.stream.headers);
        // relay emits ready() -> player.setSource + play()
        // fallback: if relay fails, we still get the failed() signal above
    }

    // While one song plays, resolve the stream of the one after it so the gap between songs is short.
    void prefetchNext()
    {
        if (!m_yt->available()) return;
        const int id = peekNext();
        if (id < 0 || id == m_currentId) return;
        const Track* t = trackById(id);
        if (!t || !t->online) return;
        const auto it = m_prefetched.constFind(t->videoId);
        if (it != m_prefetched.constEnd() && QDateTime::currentSecsSinceEpoch() - it->when < kStreamTtlSec) return;
        m_yt->prefetch(t->videoId);
    }

    void noteRecent(const Track& t)
    {
        m_store.pushRecent(entryFromTrack(t));
        m_homeDirty = true;
    }

    void resolveCurrent()
    {
        const Track* t = trackById(m_currentId);
        if (!t || !t->online) return;
        if (!m_yt->available()) { refreshToolState(); showBanner(Banner::Setup); return; }
        m_resolving = true;
        m_resolveVideo = t->videoId;
        m_yt->resolve(t->videoId);
        m_barArtist->setText(QStringLiteral("Loading stream..."));
    }

    void togglePlay()
    {
        if (m_currentId < 0) { startFromCurrentTab(); return; }
        if (m_resolving) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
        else m_player->play();
    }

    // Space with nothing playing: start the list on screen.
    void startFromCurrentTab()
    {
        switch (m_stack->currentIndex()) {
        case TabLibrary: {
            const QVector<int> v = idsOf(m_proxy);
            if (!v.isEmpty()) playFrom(v, m_shuffle ? v[QRandomGenerator::global()->bounded(int(v.size()))] : v.first(), Src::Library);
            break;
        }
        case TabPlaylist:
            playPlaylist(m_openPl);
            break;
        default: {
            const QVector<int> v = m_smodel->ids();
            if (m_oStack->currentIndex() == 1 && !v.isEmpty()) playFrom(v, v.first(), Src::Other);
            break;
        }
        }
    }

    // The next song of the list (not the "play next" queue). `commit` allows reshuffling at the end of a
    // shuffled list; a peek must not change anything.
    int contextNext(bool fromEnd, bool commit)
    {
        const int n = int(m_order.size());
        if (n == 0) return -1;
        for (int p = m_pos + 1; p < n; ++p)                 // skip songs that no longer resolve
            if (trackById(m_order[p])) return m_order[p];
        // end of the list
        if (m_repeat != 1 && fromEnd) return -1;            // repeat off: stop
        if (m_shuffle && n > 1) {                           // wrap around with a fresh shuffle
            if (!commit) return -1;
            std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
            if (m_order[0] == m_currentId)
                std::swap(m_order[0], m_order[1 + int(QRandomGenerator::global()->bounded(n - 1))]);
        }
        for (int i = 0; i < n; ++i) if (trackById(m_order[i])) return m_order[i];
        return -1;
    }

    // Returns the id of the next track, or -1 when playback should stop. Consumes the "play next" queue.
    int pickNext(bool fromEnd)
    {
        m_pickedFromUserQ = false;
        while (!m_userQ.isEmpty()) {
            const int id = m_userQ.takeFirst();
            if (trackById(id)) { m_pickedFromUserQ = true; return id; }
        }
        return contextNext(fromEnd, true);
    }

    int peekNext()
    {
        for (int id : m_userQ) if (trackById(id)) return id;
        return contextNext(true, false);
    }

    void playNext(bool fromEnd)
    {
        const int id = pickNext(fromEnd);
        if (id >= 0) playId(id, true, m_pickedFromUserQ);
    }

    // The current song can't be played (deleted video, missing file...): move on instead of stopping dead.
    bool skipUnplayable(const QString& why)
    {
        const Track* t = trackById(m_currentId);
        if (!t) return false;
        if (m_skipStreak >= 6) {
            m_skipStreak = 0;
            showToast(QStringLiteral("Stopped: several songs in a row couldn't be played"));
            return false;
        }
        const QString msg = QStringLiteral("Skipped \"%1\": %2").arg(t->title, why);
        const int next = pickNext(true);
        if (next < 0) return false;
        ++m_skipStreak;
        showToast(msg);
        playId(next, true, m_pickedFromUserQ);
        return true;
    }

    void playPrev()
    {
        if (m_currentId < 0) return;
        if (m_player->position() > 3000) { seekTo(0); return; }
        while (!m_history.isEmpty()) {
            const int id = m_history.takeLast();
            if (trackById(id)) { playId(id, false); return; }
        }
        QVector<int> v;
        for (int id : m_order) if (trackById(id)) v.push_back(id);
        if (v.isEmpty()) return;
        const int pos = v.indexOf(m_currentId);
        playId(v[pos <= 0 ? v.size() - 1 : pos - 1], false);
    }

    void onEnded()
    {
        if (m_repeat == 2) {
            m_player->setPosition(0);
            m_player->play();
            m_lastPos = 0;
            m_posClock.restart();
            rpcSync();
            return;
        }
        playNext(true);
    }

    void seekTo(qint64 ms)
    {
        const qint64 dur = currentDuration();
        if (dur > 0) ms = qBound<qint64>(0, ms, dur);
        m_player->setPosition(ms);
        m_lastPos = ms;
        m_posClock.restart();
        m_lastSec = -1;
        setTimeLabel(ms);
        if (dur > 0 && !m_seek->dragging()) m_seek->setValue(qreal(ms) / dur);
        rpcSync();
    }

    qint64 currentDuration() const
    {
        qint64 d = m_player->duration();
        if (d <= 0)
            if (const Track* t = trackById(m_currentId)) d = t->durationMs;
        return d;
    }

    void setShuffle(bool on)
    {
        const bool changed = (on != m_shuffle);
        m_shuffle = on;
        m_shuffleBtn->setActive(on);
        m_heroShuffle->setActive(on);
        m_plHeroShuffle->setActive(on);
        if (changed && !m_queue.isEmpty()) {
            // keep playing the same song; the rest of the list is re-ordered around it
            const int anchor = (m_pos >= 0 && m_pos < m_order.size()) ? m_order[m_pos] : m_currentId;
            rebuildOrder(anchor);
            prefetchNext();
        }
    }

    void applyVolume(qreal v)
    {
        m_audio->setVolume(float(v * v));   // squared = more natural loudness curve
        m_volBtn->setLevel(v);
    }

    // ---------------------------------------------------- player callbacks
    void updateHeroStates()
    {
        const bool pl = (m_player->playbackState() == QMediaPlayer::PlayingState);
        m_heroPlay->setPlaying(pl && m_src == Src::Library);
        m_plHeroPlay->setPlaying(pl && m_src == Src::Playlist && m_srcKey == m_openPl);
    }

    void markNowPlaying(bool playing)
    {
        m_view->setNowPlaying(m_currentId, playing);
        m_slist->setNowPlaying(m_currentId, playing);
        m_pview->setNowPlaying(m_currentId, playing);
        updateHeroStates();
        updateCardStates();
        updatePlaylistSelection();
    }

    void onState(QMediaPlayer::PlaybackState st)
    {
        const bool pl = (st == QMediaPlayer::PlayingState);
        if (pl) { m_skipStreak = 0; prefetchNext(); }
        m_playBtn->setPlaying(pl);
        m_sheet->setPlaying(pl);
        markNowPlaying(pl);
        m_lastPos = m_player->position();
        m_posClock.restart();
        if (pl) m_ui.start(); else m_ui.stop();
        rpcSync();
    }

    void onPosition(qint64 pos)
    {
        const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        const qint64 predicted = m_lastPos + (playing ? m_posClock.elapsed() : 0);
        if (!playing || qAbs(predicted - pos) > 250) {
            m_lastPos = pos;
            m_posClock.restart();
        }
        if (!playing) {
            const qint64 dur = currentDuration();
            if (dur > 0 && !m_seek->dragging()) m_seek->setValue(qreal(pos) / dur);
            setTimeLabel(pos);
        }
    }

    void onDuration(qint64 d)
    {
        if (d <= 0) return;
        m_timeR->setText(fmtTime(d));
        m_model->setDuration(m_currentId, d);
        m_smodel->setDuration(m_currentId, d);
        m_pmodel->setDuration(m_currentId, d);
        if (Track* t = knownTrack(m_currentId)) t->durationMs = d;
        rpcSync();
    }

    // Current song -> Discord. Safe to call as often as you like (identical states are ignored).
    void rpcSync()
    {
        if (!m_rpc) return;
        const Track* t = (m_currentId >= 0) ? trackById(m_currentId) : nullptr;
        if (!t) { m_rpcIdle.invalidate(); m_rpc->setTrack(RpcTrack()); return; }

        const auto st = m_player->playbackState();
        const bool playing = (st == QMediaPlayer::PlayingState);
        const bool paused  = (st == QMediaPlayer::PausedState);
        if (playing || paused) {
            m_rpcIdle.invalidate();
        } else {
            // loading the next stream: keep showing the song, but give up if nothing starts for 30 s
            if (!m_rpcIdle.isValid()) m_rpcIdle.start();
            if (m_rpcIdle.elapsed() > 30000) { m_rpc->setTrack(RpcTrack()); return; }
        }

        const qint64 dur = currentDuration();
        qint64 pos = m_lastPos + (playing ? m_posClock.elapsed() : 0);
        pos = qMax<qint64>(0, pos);
        if (dur > 0) pos = qMin(pos, dur);

        RpcTrack r;
        r.valid = true;
        r.playing = playing;
        r.paused = paused;
        r.title = t->title;
        r.artist = t->artist;
        r.album = t->album;
        r.durationMs = dur;
        r.positionMs = pos;
        if (t->online) {
            r.fallbackImage = !t->thumbUrl.isEmpty() ? t->thumbUrl
                                                     : Yt::thumbUrl(t->videoId, "mqdefault");
        }
        m_rpc->setTrack(r);
    }

    // Smooth 60 fps progress: interpolate between the player's coarse updates.
    void tick()
    {
        const qint64 dur = currentDuration();
        if (dur <= 0) return;
        const qint64 pos = qMin<qint64>(dur, m_lastPos + m_posClock.elapsed());
        if (!m_seek->dragging()) {
            m_seek->setValue(qreal(pos) / dur);
            setTimeLabel(pos);
        }
        // Feed current position to the lyrics view for sync highlighting
        if (m_lyricsView) m_lyricsView->setPosition(pos);
    }

    void setTimeLabel(qint64 ms)
    {
        const int sec = int(ms / 1000);
        if (sec == m_lastSec) return;
        m_lastSec = sec;
        m_timeL->setText(fmtTime(ms));
    }

    void refreshNowPlaying()
    {
        const Track* t = trackById(m_currentId);
        const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        if (!t) m_currentId = -1;
        markNowPlaying(playing);
        rpcSync();
        if (!t) {
            m_barTitle->setText(QStringLiteral("Nothing playing"));
            m_barArtist->setText(QStringLiteral("Pick a song to start"));
            m_barCover->setPixmap(QPixmap());
            m_timeR->setText(QStringLiteral("0:00"));
            m_timeL->setText(QStringLiteral("0:00"));
            m_seek->setValue(0);
            m_page->setTint(Theme::defaultTint());
            m_onlinePage->setTint(Theme::defaultTint());
            m_plPage->setTint(Theme::defaultTint());
            m_sheet->closeSheet();
            setWindowTitle(QStringLiteral("Pulsify"));
            if (m_infoPanel) m_infoPanel->setInfo(SongInfo());
            m_lyricsKey.clear();
            if (m_lyricsView) m_lyricsView->setMessage(QString());
            return;
        }
        const QColor tint = t->tint.isValid() ? t->tint : Theme::defaultTint();
        m_barTitle->setText(t->title);
        m_barArtist->setText(t->artist);
        m_barCover->setPixmap(t->cover);
        m_page->setTint(tint);
        m_onlinePage->setTint(tint);
        m_plPage->setTint(tint);
        m_sheet->setTrack(t->title, t->artist, t->cover, tint);
        if (t->durationMs > 0) m_timeR->setText(fmtTime(t->durationMs));
        setWindowTitle(QStringLiteral("%1 - %2 - Pulsify").arg(t->title, t->artist));

        // Update InfoPanel (keeps the stream details that were already looked up for this song)
        updateInfoPanel(m_curStreamId == m_currentId ? m_curStream : StreamInfo());

        // Lyrics: only look them up again when the song or its title / artist changed
        if (m_lyricsService && m_lyricsView) {
            const QString key = (t->online ? t->videoId : t->path) + QLatin1Char('|') + t->artist + QLatin1Char('|') + t->title;
            if (key != m_lyricsKey) {
                m_lyricsKey = key;
                m_lyricsView->setMessage(QStringLiteral("Loading lyrics\u2026"));
                m_lyricsService->fetch(key, t->artist, t->title, t->album, t->durationMs);
            }
        }
    }

    void toggleLyrics()
    {
        if (m_currentId < 0) return;
        if (!m_sheet->isOpen()) m_sheet->openSheet(true);
        else m_sheet->setLyricsMode(!m_sheet->lyricsMode());
    }

    void updateInfoPanel(const StreamInfo& stream)
    {
        if (!m_infoPanel) return;
        const Track* t = trackById(m_currentId);
        if (!t) { m_infoPanel->setInfo(SongInfo()); return; }
        if (stream.known()) { m_curStream = stream; m_curStreamId = m_currentId; }

        SongInfo info;
        info.valid = true;
        info.online = t->online;
        info.title = t->title;
        info.artist = t->artist;
        info.album = t->album;
        info.videoId = t->videoId;
        info.path = t->path;
        info.durationMs = t->durationMs;
        info.cover = t->cover.isNull() ? t->thumb : t->cover;
        info.tint = t->tint;
        info.stream = stream;

        // next in queue
        const int nextId = peekNext();
        if (const Track* nt = (nextId >= 0 && nextId != m_currentId) ? trackById(nextId) : nullptr) {
            info.hasNext = true;
            info.nextTitle = nt->title;
            info.nextArtist = nt->artist;
            info.nextCover = nt->cover.isNull() ? nt->thumb : nt->cover;
        }

        m_infoPanel->setInfo(info);
    }

    // =====================================================================
    //  Members
    // =====================================================================
    QWidget*         m_upper = nullptr;
    QStackedWidget*  m_stack = nullptr;
    NavButton*       m_navHome = nullptr;
    NavButton*       m_navLib = nullptr;
    GoogleAccount*   m_google = nullptr;
    NowPlayingSheet* m_sheet = nullptr;
    Toast*           m_toast = nullptr;
    MetaLoader*      m_loader = nullptr;
    QMediaPlayer*    m_player = nullptr;
    QAudioOutput*    m_audio = nullptr;

    // models: offline library, search results, the open playlist
    TrackModel*      m_model = nullptr;
    TrackModel*      m_smodel = nullptr;
    TrackModel*      m_pmodel = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    HeadProxy*       m_sproxy = nullptr;

    // sidebar
    QFrame*          m_plEmpty = nullptr;
    SmoothScrollArea* m_plScroll = nullptr;
    QVBoxLayout*     m_plRowsLayout = nullptr;
    QList<PlaylistRow*> m_plRows;

    // library page
    TintWidget*  m_page = nullptr;
    TrackView*   m_view = nullptr;
    QLabel*      m_count = nullptr;
    QLineEdit*   m_search = nullptr;
    IconButton  *m_heroPlay = nullptr, *m_heroShuffle = nullptr;

    // playlist page
    TintWidget*   m_plPage = nullptr;
    TrackView*    m_pview = nullptr;
    PlaylistCover* m_plCover = nullptr;
    ElidedLabel*  m_plName = nullptr;
    QLabel*       m_plMeta = nullptr;
    IconButton   *m_plHeroPlay = nullptr, *m_plHeroShuffle = nullptr, *m_plMore = nullptr;
    QString       m_openPl;
    QVector<int>  m_plEntryIdx;          // model row -> index into the stored playlist's entries

    // Home page
    TintWidget*   m_onlinePage = nullptr;
    QStackedWidget* m_oStack = nullptr;
    IconButton*   m_homeBtn = nullptr;
    AvatarButton* m_avatar = nullptr;
    QPushButton*  m_themeBtn = nullptr;
    QAction*      m_searchAct = nullptr;
    QLineEdit*    m_osearch = nullptr;
    QPushButton*  m_refreshBtn = nullptr;
    QLabel*       m_oStatus = nullptr;
    SmoothScrollArea* m_homeScroll = nullptr;
    QButtonGroup* m_chipGroup = nullptr;
    int           m_chip = 0;
    QWidget*      m_tilesWrap = nullptr;
    TileGrid*     m_tiles = nullptr;
    Shelf        *m_shPlaylists = nullptr, *m_shRecent = nullptr, *m_shMore = nullptr;
    QList<Shelf*> m_disc;
    QLabel*       m_homeEmpty = nullptr;
    bool          m_homeDirty = true;

    // search results page
    SmoothScrollArea* m_searchScroll = nullptr;
    QWidget*      m_heroWrap = nullptr;
    MediaCard*    m_topCard = nullptr;
    TrackView*    m_slist = nullptr;
    Shelf*        m_shMoreRes = nullptr;
    Shelf*        m_shPlRes = nullptr;
    QStringList   m_plResIds;
    QVector<int>  m_moreIds;
    QTimer        m_searchDebounce, m_resultsTimer, m_coverTimer;
    QString       m_lastQuery;
    quint64       m_searchTok = 0;
    bool          m_searchFirst = false;

    // online plumbing
    YtDlp*        m_yt = nullptr;
    ThumbLoader*  m_thumbs = nullptr;
    QHash<quint64, int> m_fetchMap;                 // fetch token -> discover index (-1 = "more like")
    QVector<QVector<OnlineItem>> m_discItems;
    QVector<OnlineItem> m_moreItems;
    QString       m_moreArtist;
    int           m_fetchFails = 0, m_fetchTotal = 0;
    bool          m_cacheDirty = false;
    bool          m_resolving = false;
    bool          m_retried = false;
    QString       m_resolveVideo;

    // banner (install / update yt-dlp)
    QFrame*       m_banner = nullptr;
    QLabel*       m_bannerText = nullptr;
    QPushButton  *m_bannerA = nullptr, *m_bannerB = nullptr;
    Banner        m_bannerMode = Banner::None;

    // track registry: every online track / playlist-only file gets one stable id
    std::unordered_map<int, Track> m_known;
    QHash<QString, int> m_vidIds;
    QHash<QString, int> m_extraIds;
    QSet<int>     m_thumbAsked;
    int           m_nextOnlineId = OnlineIdBase;
    int           m_nextExtraId = ExtraIdBase;
    PlaylistStore m_store;

    // player bar
    ElidedLabel* m_barTitle = nullptr;
    ElidedLabel* m_barArtist = nullptr;
    CoverView*   m_barCover = nullptr;
    QLabel      *m_timeL = nullptr, *m_timeR = nullptr;
    IconButton  *m_playBtn = nullptr, *m_prevBtn = nullptr, *m_nextBtn = nullptr;
    IconButton  *m_shuffleBtn = nullptr, *m_repeatBtn = nullptr, *m_volBtn = nullptr, *m_addBtn = nullptr;
    SmoothSlider *m_seek = nullptr, *m_volume = nullptr;

    // playback state
    QTimer        m_ui;
    QElapsedTimer m_posClock;
    qint64        m_lastPos = 0;
    int           m_lastSec = -1;
    int           m_currentId = -1;
    int           m_repeat = 0;           // 0 off, 1 all, 2 one
    bool          m_shuffle = false;
    qreal         m_preMute = 0.7;
    QVector<int>  m_history;
    QVector<int>  m_queue;                // the list the current song was started from, as shown
    QVector<int>  m_order;                // ...in the order it plays (shuffled or not)
    int           m_pos = -1;             // where we are in m_order
    QVector<int>  m_userQ;                // "Play next" / "Add to queue": plays before the list continues
    bool          m_pickedFromUserQ = false;
    int           m_skipStreak = 0;       // unplayable songs skipped in a row
    Src           m_src = Src::None;
    QString       m_srcKey;

    // streams resolved ahead of time (YouTube stream URLs stay valid for hours)
    struct Prefetched { QString url; OnlineItem meta; qint64 when = 0; };
    static constexpr qint64 kStreamTtlSec = 20 * 60;
    QHash<QString, Prefetched> m_prefetched;

    // ---- new feature members ----
    // Stream relay: local HTTP proxy that handles YouTube URL expiry / throttling
    StreamRelay*  m_relay = nullptr;
    quint64       m_relaySession = 0;

    // Now Playing info panel (right sidebar)
    InfoPanel*    m_infoPanel = nullptr;
    SlideHost*    m_infoHost = nullptr;
    IconButton*   m_infoBtn = nullptr;
    IconButton*   m_lyricsBtn = nullptr;
    StreamInfo    m_curStream;
    int           m_curStreamId = -1;
    QString       m_lyricsKey;

    // Synced lyrics
    LyricsService* m_lyricsService = nullptr;
    LyricsView*    m_lyricsView = nullptr;
    QWidget*       m_lyricsContainer = nullptr;

    // Deezer chart service (trending songs, popular artists/albums)
    ChartService*     m_charts = nullptr;
    ImageCache*       m_imgCache = nullptr;
    QVector<Track>    m_chartSongs;
    QVector<ChartItem> m_chartArtists, m_chartAlbums;

    // Settings dialog
    SettingsDialog* m_settingsDialog = nullptr;

    // Discord Rich Presence
    DiscordRpc*    m_rpc = nullptr;
    QTimer         m_rpcTimer;
    QElapsedTimer  m_rpcIdle;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Storage identity stays "Pulse"/"PulsePlayer" so existing playlists, settings and Google sign-in carry over.
    app.setApplicationName(QStringLiteral("Pulse"));
    app.setOrganizationName(QStringLiteral("PulsePlayer"));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    app.setFont(Theme::font(14));

    Theme::load();

    Theme::setAppSheet(
        "QScrollBar:vertical{background:transparent;width:14px;margin:0;}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,0.22);border-radius:4px;min-height:48px;margin:2px 3px;}"
        "QScrollBar::handle:vertical:hover{background:rgba(255,255,255,0.45);}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QMenu{background:#282828;color:white;border:1px solid #3a3a3a;padding:6px;}"
        "QMenu::item{padding:8px 24px;border-radius:4px;}"
        "QMenu::item:selected{background:#3d3d3d;}"
        "QMenu::separator{height:1px;background:#3a3a3a;margin:6px 8px;}"
        "QToolTip{background:#282828;color:white;border:1px solid #3a3a3a;padding:4px 8px;}");
    Theme::apply();

    MainWindow w;
    w.show();
    w.openPaths(QCoreApplication::arguments().mid(1));   // "Open with..." support
    return app.exec();
}

#include "main.moc"
