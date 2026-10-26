#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <memory>
#include "online.h"
#include "google.h"
#include "importdialog.h"
#include "lyrics.h"
#include "discord.h"

// ============================================================================
//  SettingsDialog: yt-dlp install/update, JS runtime status, cookies, Google account,
//  lyrics, playback mode
// ============================================================================
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(GoogleAccount* auth, DiscordRpc* rpc, QWidget* parent = nullptr)
        : QDialog(parent), m_auth(auth), m_rpc(rpc)
    {
        setWindowTitle(QStringLiteral("Settings"));
        setMinimumWidth(600);
        auto* v = new QVBoxLayout(this);
        v->setSpacing(14);

        auto* intro = new QLabel(QStringLiteral(
            "Online playback uses <b>yt-dlp</b> to find and stream audio. "
            "YouTube changes often, so keep it updated if songs stop playing."));
        intro->setWordWrap(true);
        intro->setTextFormat(Qt::RichText);
        v->addWidget(intro);

        auto* form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(14);

        // --- yt-dlp
        auto* ytBox = new QVBoxLayout;
        m_ytStatus = new QLabel;
        m_ytStatus->setWordWrap(true);
        m_ytStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_ytStatus->setTextFormat(Qt::RichText);
        auto* ytRow = new QHBoxLayout;
        m_install = new QPushButton;
        auto* browse = new QPushButton(QStringLiteral("Choose file\u2026"));
        ytRow->addWidget(m_install);
        ytRow->addWidget(browse);
        ytRow->addStretch(1);
        m_progress = new QProgressBar;
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(6);
        m_progress->hide();
        m_note = new QLabel;
        m_note->setWordWrap(true);
        ytBox->addWidget(m_ytStatus);
        ytBox->addLayout(ytRow);
        ytBox->addWidget(m_progress);
        ytBox->addWidget(m_note);
        form->addRow(QStringLiteral("yt-dlp"), ytBox);

        // --- JS runtime
        auto* jsBox = new QVBoxLayout;
        m_jsStatus = new QLabel;
        m_jsStatus->setWordWrap(true);
        m_jsStatus->setTextFormat(Qt::RichText);
        m_jsStatus->setOpenExternalLinks(true);
        auto* recheck = new QPushButton(QStringLiteral("Check again"));
        auto* jsRow = new QHBoxLayout;
        jsRow->addWidget(recheck);
        jsRow->addStretch(1);
        jsBox->addWidget(m_jsStatus);
        jsBox->addLayout(jsRow);
        form->addRow(QStringLiteral("JavaScript runtime"), jsBox);

        // --- cookies
        m_cookies = new QComboBox;
        m_cookies->addItem(QStringLiteral("None"), QString());
        const char* browsers[] = { "chrome", "firefox", "edge", "brave", "opera", "vivaldi", "safari" };
        for (const char* b : browsers) {
            QString label = QString::fromLatin1(b);
            label[0] = label[0].toUpper();
            m_cookies->addItem(label, QString::fromLatin1(b));
        }
        auto* cookieHelp = new QLabel(QStringLiteral(
            "Only needed if YouTube says \u201cSign in to confirm you\u2019re not a bot\u201d or a song is age-restricted."));
        cookieHelp->setWordWrap(true);
        cookieHelp->setStyleSheet(QStringLiteral("color:#9a9a9a;font-size:12px;"));
        auto* cookieBox = new QVBoxLayout;
        cookieBox->addWidget(m_cookies);
        cookieBox->addWidget(cookieHelp);
        form->addRow(QStringLiteral("Use cookies from"), cookieBox);

        // --- Google account
        auto* gBox = new QVBoxLayout;
        m_gStatus = new QLabel;
        m_gStatus->setWordWrap(true);
        m_gStatus->setTextFormat(Qt::RichText);
        m_gId = new QLineEdit;
        m_gId->setPlaceholderText(QStringLiteral("OAuth client ID  (\u2026apps.googleusercontent.com)"));
        m_gSecret = new QLineEdit;
        m_gSecret->setEchoMode(QLineEdit::Password);
        m_gSecret->setPlaceholderText(QStringLiteral("OAuth client secret"));
        m_gBtn = new QPushButton;
        auto* gRow = new QHBoxLayout;
        gRow->addWidget(m_gBtn);
        gRow->addStretch(1);
        auto* gHelp = new QLabel(QStringLiteral(
            "Logging in lets Pulse list your YouTube playlists (private ones and Liked videos too) so you can import "
            "them in one click. Google needs an \u201cOAuth client\u201d that belongs to you \u2013 it takes about two minutes:<br>"
            "1. <a href='https://console.cloud.google.com/projectcreate' style='color:#1DB954'>Create a Google Cloud project</a> (any name).<br>"
            "2. <a href='https://console.cloud.google.com/apis/library/youtube.googleapis.com' style='color:#1DB954'>Enable the YouTube Data API v3</a>.<br>"
            "3. <a href='https://console.cloud.google.com/apis/credentials/consent' style='color:#1DB954'>OAuth consent screen</a>: "
            "choose External, fill in the name and your email, and add your own Google account under <i>Test users</i>.<br>"
            "4. <a href='https://console.cloud.google.com/apis/credentials' style='color:#1DB954'>Credentials</a> \u2192 Create credentials "
            "\u2192 OAuth client ID \u2192 type <b>Desktop app</b>. Paste the client ID and secret above.<br>"
            "Google will warn that the app is unverified \u2013 that is expected for your own client (Advanced \u2192 continue). "
            "While the consent screen is in <i>Testing</i> mode Google signs you out every 7 days."));
        gHelp->setWordWrap(true);
        gHelp->setTextFormat(Qt::RichText);
        gHelp->setOpenExternalLinks(true);
        gHelp->setStyleSheet(QStringLiteral("color:#9a9a9a;font-size:12px;"));
        gBox->addWidget(m_gStatus);
        gBox->addWidget(m_gId);
        gBox->addWidget(m_gSecret);
        gBox->addLayout(gRow);
        gBox->addWidget(gHelp);
        form->addRow(QStringLiteral("Google account"), gBox);

        // --- Lyrics
        auto* lBox = new QVBoxLayout;
        m_genius = new QLineEdit;
        m_genius->setEchoMode(QLineEdit::Password);
        m_genius->setPlaceholderText(QStringLiteral("Genius API token (optional)"));
        auto* lHelp = new QLabel(QStringLiteral(
            "Lyrics text comes from Genius, timing from LRCLIB. Genius needs no login for this, but if its search ever "
            "stops answering, create a free client at <a href='https://genius.com/api-clients' style='color:#1DB954'>genius.com/api-clients</a> "
            "and paste its <i>Client Access Token</i> here."));
        lHelp->setWordWrap(true);
        lHelp->setTextFormat(Qt::RichText);
        lHelp->setOpenExternalLinks(true);
        lHelp->setStyleSheet(QStringLiteral("color:#9a9a9a;font-size:12px;"));
        auto* clearLyrics = new QPushButton(QStringLiteral("Clear lyrics cache"));
        auto* lRow = new QHBoxLayout;
        lRow->addWidget(clearLyrics);
        lRow->addStretch(1);
        lBox->addWidget(m_genius);
        lBox->addWidget(lHelp);
        lBox->addLayout(lRow);
        form->addRow(QStringLiteral("Lyrics"), lBox);

        // --- Discord Rich Presence
        auto* dBox = new QVBoxLayout;
        m_dEnable = new QCheckBox(QStringLiteral("Show what I'm listening to on Discord (title, artist, progress, cover)"));
        m_dId = new QLineEdit;
        m_dId->setPlaceholderText(QStringLiteral("Discord Application ID"));
        m_dStatus = new QLabel;
        m_dStatus->setWordWrap(true);
        m_dStatus->setStyleSheet(QStringLiteral("color:#9a9a9a;font-size:12px;"));
        auto* dHelp = new QLabel(QStringLiteral(
            "Create a free application at <a href='https://discord.com/developers/applications' style='color:#1DB954'>"
            "discord.com/developers/applications</a>, name it <b>Pulsify</b> (Discord shows \"Listening to Pulsify\"), "
            "and paste its <i>Application ID</i> here. Discord must be running on this PC, with "
            "<i>Settings \u2192 Activity Privacy \u2192 Share my activity</i> turned on. "
            "Covers are looked up online by artist and title, so they also show for local files."));
        dHelp->setWordWrap(true);
        dHelp->setTextFormat(Qt::RichText);
        dHelp->setOpenExternalLinks(true);
        dHelp->setStyleSheet(QStringLiteral("color:#9a9a9a;font-size:12px;"));
        dBox->addWidget(m_dEnable);
        dBox->addWidget(m_dId);
        dBox->addWidget(m_dStatus);
        dBox->addWidget(dHelp);
        form->addRow(QStringLiteral("Discord"), dBox);

        // --- playback
        m_preDl = new QCheckBox(QStringLiteral("Download each song before playing (slower start, most reliable)"));
        form->addRow(QStringLiteral("Playback"), m_preDl);

        auto* clear = new QPushButton(QStringLiteral("Clear audio cache"));
        auto* clearRow = new QHBoxLayout;
        clearRow->addWidget(clear);
        clearRow->addStretch(1);
        form->addRow(QStringLiteral("Cache"), clearRow);

        auto* inner = new QWidget;
        auto* iv = new QVBoxLayout(inner);
        iv->setContentsMargins(0, 0, 10, 0);
        iv->addLayout(form);
        iv->addStretch(1);
        m_scroll = new QScrollArea;
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidget(inner);
        v->addWidget(m_scroll, 1);
        resize(680, 700);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        v->addWidget(buttons);

        QSettings s;
        const int ci = m_cookies->findData(s.value(QStringLiteral("yt/cookies")).toString());
        m_cookies->setCurrentIndex(ci < 0 ? 0 : ci);
        m_preDl->setChecked(s.value(QStringLiteral("yt/predownload"), false).toBool());
        m_gId->setText(s.value(QStringLiteral("google/clientId")).toString());
        m_gSecret->setText(s.value(QStringLiteral("google/clientSecret")).toString());
        m_genius->setText(s.value(QStringLiteral("lyrics/geniusToken")).toString());
        m_dEnable->setChecked(s.value(QStringLiteral("discord/enabled"), true).toBool());
        m_dId->setText(s.value(QStringLiteral("discord/clientId"), DiscordRpc::defaultClientId()).toString());
        if (m_rpc) {
            m_dStatus->setText(m_rpc->status());
            connect(m_rpc, &DiscordRpc::statusChanged, this, [this](const QString& text, bool ok) {
                m_dStatus->setText(QStringLiteral("<span style='color:%1'>%2</span>")
                                       .arg(ok ? QStringLiteral("#1DB954") : QStringLiteral("#9a9a9a"), text.toHtmlEscaped()));
            });
        }

        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
        connect(m_install, &QPushButton::clicked, this, &SettingsDialog::installOrUpdate);
        connect(browse, &QPushButton::clicked, this, &SettingsDialog::chooseFile);
        connect(recheck, &QPushButton::clicked, this, [this] { refresh(); });
        connect(clear, &QPushButton::clicked, this, [this] {
            Yt::clearAudioCache();
            m_note->setText(QStringLiteral("Audio cache cleared."));
        });
        connect(m_cookies, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            QSettings().setValue(QStringLiteral("yt/cookies"), m_cookies->currentData().toString());
        });
        connect(m_preDl, &QCheckBox::toggled, this, [](bool on) {
            QSettings().setValue(QStringLiteral("yt/predownload"), on);
        });
        connect(m_dEnable, &QCheckBox::toggled, this, [this](bool) { saveDiscord(); });
        connect(m_dId, &QLineEdit::editingFinished, this, [this] { saveDiscord(); });
        connect(m_gId, &QLineEdit::editingFinished, this, [this] { saveGoogleFields(); });
        connect(m_gSecret, &QLineEdit::editingFinished, this, [this] { saveGoogleFields(); });
        connect(m_gBtn, &QPushButton::clicked, this, [this] {
            if (m_auth->signedIn()) { m_auth->signOut(); return; }
            if (m_auth->signingIn()) { m_auth->cancelSignIn(); return; }
            saveGoogleFields();
            m_auth->signIn();
        });
        connect(m_auth, &GoogleAccount::stateChanged, this, [this] { refreshGoogle(); });
        connect(m_auth, &GoogleAccount::signInFailed, this, [this](const QString& msg) {
            m_gStatus->setText(QStringLiteral("<span style='color:#ff8a65'>%1</span>").arg(msg.toHtmlEscaped()));
        });
        connect(m_genius, &QLineEdit::editingFinished, this, [this] {
            QSettings().setValue(QStringLiteral("lyrics/geniusToken"), m_genius->text().trimmed());
        });
        connect(clearLyrics, &QPushButton::clicked, this, [this] {
            LyricsService::clearCache();
            m_note->setText(QStringLiteral("Lyrics cache cleared."));
        });

        refresh();
        refreshGoogle();
    }

    void focusGoogle()
    {
        QTimer::singleShot(0, this, [this] {          // after the dialog has been laid out
            m_scroll->ensureWidgetVisible(m_gId, 0, 80);
            m_gId->setFocus();
        });
    }

private:
    void saveDiscord()
    {
        QSettings s;
        s.setValue(QStringLiteral("discord/enabled"), m_dEnable->isChecked());
        s.setValue(QStringLiteral("discord/clientId"), m_dId->text().trimmed());
        if (m_rpc) m_rpc->reloadSettings();
    }

    void saveGoogleFields()
    {
        m_auth->setCredentials(m_gId->text().trimmed(), m_gSecret->text().trimmed());
        QSettings s;
        s.setValue(QStringLiteral("google/clientId"), m_gId->text().trimmed());
        s.setValue(QStringLiteral("google/clientSecret"), m_gSecret->text().trimmed());
    }

    void refreshGoogle()
    {
        if (m_auth->signedIn()) {
            const QString who = m_auth->accountName().isEmpty() ? QStringLiteral("your Google account") : m_auth->accountName();
            QString t = QStringLiteral("<span style='color:#1DB954'>\u2713</span> Logged in as <b>%1</b>").arg(who.toHtmlEscaped());
            m_gStatus->setText(t);
            m_gBtn->setText(QStringLiteral("Log out"));
            m_gBtn->setEnabled(true);
        } else if (m_auth->signingIn()) {
            m_gStatus->setText(QStringLiteral("Finish signing in in your browser\u2026"));
            m_gBtn->setText(QStringLiteral("Cancel"));
            m_gBtn->setEnabled(true);
        } else {
            m_gStatus->setText(QStringLiteral("Not logged in."));
            m_gBtn->setText(QStringLiteral("Log in with Google"));
            m_gBtn->setEnabled(true);
        }
    }

    void refresh()
    {
        const QString exe = Yt::findYtDlp();
        if (exe.isEmpty()) {
            m_ytStatus->setText(QStringLiteral("<span style='color:#ff8a65'>Not installed.</span> One click installs it for this app."));
            m_install->setText(QStringLiteral("Download yt-dlp"));
        } else {
            m_ytStatus->setText(QStringLiteral("Found: %1").arg(exe.toHtmlEscaped()));
            m_install->setText(QStringLiteral("Update yt-dlp"));
            auto* job = new YtJob(this);
            connect(job, &YtJob::line, this, [this, exe](const QByteArray& l) {
                m_ytStatus->setText(QStringLiteral("Found: %1<br>Version: %2")
                                        .arg(exe.toHtmlEscaped(), QString::fromUtf8(l).toHtmlEscaped()));
            });
            job->start(QStringList() << QStringLiteral("--version"), exe);
        }

        const QPair<QString, QString> js = Yt::jsRuntime();
        if (js.first.isEmpty()) {
            m_jsStatus->setText(QStringLiteral(
                "<span style='color:#ff8a65'>None found.</span> YouTube playback needs one. Install "
                "<a href='https://deno.com' style='color:#1DB954'>Deno</a> "
                "(Windows: <i>winget install DenoLand.Deno</i> \u00b7 macOS: <i>brew install deno</i> \u00b7 "
                "Linux: <i>curl -fsSL https://deno.land/install.sh | sh</i>), then press \u201cCheck again\u201d. "
                "Node.js or Bun also work."));
        } else {
            m_jsStatus->setText(QStringLiteral("<span style='color:#1DB954'>\u2713</span> Using %1 (%2)")
                                    .arg(js.first, js.second.toHtmlEscaped()));
        }
    }

    void setBusy(bool b)
    {
        m_install->setEnabled(!b);
        m_progress->setVisible(b);
        if (b) m_progress->setRange(0, 0);
    }

    void chooseFile()
    {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Locate yt-dlp"));
        if (f.isEmpty()) return;
        QSettings().setValue(QStringLiteral("yt/path"), f);
        refresh();
    }

    void installOrUpdate()
    {
        const QString cur = Yt::findYtDlp();
        if (cur.isEmpty()) { download(); return; }

        setBusy(true);
        m_note->setText(QStringLiteral("Updating\u2026"));
        auto* job = new YtJob(this);
        auto last = std::make_shared<QString>();
        connect(job, &YtJob::line, this, [last](const QByteArray& l) { *last = QString::fromUtf8(l); });
        connect(job, &YtJob::done, this, [this, last](bool ok, const QString& err) {
            setBusy(false);
            m_note->setText(ok ? (last->isEmpty() ? QStringLiteral("Done.") : *last) : err);
            refresh();
        });
        job->start(QStringList() << QStringLiteral("-U"), cur);
    }

    void download()
    {
        QString asset;
#if defined(Q_OS_WIN)
        asset = QStringLiteral("yt-dlp.exe");
#elif defined(Q_OS_MACOS)
        asset = QStringLiteral("yt-dlp_macos");
#else
        asset = QSysInfo::currentCpuArchitecture().contains(QLatin1String("arm"))
                    ? QStringLiteral("yt-dlp_linux_aarch64") : QStringLiteral("yt-dlp_linux");
#endif
        QNetworkRequest req{ QUrl(QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/") + asset) };
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        setBusy(true);
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        m_note->setText(QStringLiteral("Downloading yt-dlp\u2026"));
        QNetworkReply* rep = m_nam.get(req);
        connect(rep, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            if (total > 0) m_progress->setValue(int(got * 100 / total));
        });
        connect(rep, &QNetworkReply::finished, this, [this, rep] {
            rep->deleteLater();
            setBusy(false);
            if (rep->error() != QNetworkReply::NoError) {
                m_note->setText(QStringLiteral("Download failed: %1").arg(rep->errorString()));
                return;
            }
            const QByteArray data = rep->readAll();
            if (data.size() < 1024 * 1024) {
                m_note->setText(QStringLiteral("The download looked incomplete \u2013 please try again."));
                return;
            }
            const QString path = Yt::managedPath();
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
                m_note->setText(QStringLiteral("Couldn\u2019t save yt-dlp to %1").arg(path));
                return;
            }
            QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                            | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
            m_note->setText(QStringLiteral("Installed."));
            refresh();
        });
    }

    GoogleAccount*   m_auth = nullptr;
    DiscordRpc*      m_rpc = nullptr;
    QCheckBox*       m_dEnable = nullptr;
    QLineEdit*       m_dId = nullptr;
    QLabel*          m_dStatus = nullptr;
    QScrollArea*  m_scroll = nullptr;
    QLabel*       m_gStatus = nullptr;
    QLineEdit     *m_gId = nullptr, *m_gSecret = nullptr, *m_genius = nullptr;
    QPushButton*  m_gBtn = nullptr;
    QLabel*       m_ytStatus = nullptr;
    QLabel*       m_jsStatus = nullptr;
    QLabel*       m_note = nullptr;
    QPushButton*  m_install = nullptr;
    QProgressBar* m_progress = nullptr;
    QComboBox*    m_cookies = nullptr;
    QCheckBox*    m_preDl = nullptr;
    QNetworkAccessManager m_nam;
};

// ============================================================================
//  ImportDialog: paste a link / list of songs, or load a CSV or text file
// ============================================================================
class MultiImportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MultiImportDialog(QWidget* parent = nullptr) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Import playlist"));
        setMinimumWidth(600);
        auto* v = new QVBoxLayout(this);
        v->setSpacing(12);

        auto* info = new QLabel(QStringLiteral(
            "<b>Paste a playlist link</b> \u2013 YouTube, YouTube Music, Spotify (public playlists and albums), "
            "SoundCloud, Bandcamp and other sites yt-dlp supports.<br>"
            "<b>Or paste a list of songs</b>, one per line, like <i>Artist - Title</i>.<br>"
            "<b>Or load a CSV / text export</b> from Exportify, TuneMyMusic, Soundiiz and similar tools "
            "(this covers Apple Music, Deezer, Tidal, Amazon Music and more)."));
        info->setWordWrap(true);
        info->setTextFormat(Qt::RichText);
        v->addWidget(info);

        m_input = new QPlainTextEdit;
        m_input->setPlaceholderText(QStringLiteral("https://open.spotify.com/playlist/\u2026   or   https://www.youtube.com/playlist?list=\u2026"));
        m_input->setFixedHeight(120);
        v->addWidget(m_input);

        auto* fileRow = new QHBoxLayout;
        auto* file = new QPushButton(QStringLiteral("Choose CSV / text file\u2026"));
        fileRow->addWidget(file);
        fileRow->addStretch(1);
        v->addLayout(fileRow);

        m_name = new QLineEdit;
        m_name->setPlaceholderText(QStringLiteral("Playlist name (optional \u2013 defaults to the original name)"));
        v->addWidget(m_name);

        m_status = new QLabel;
        m_status->setWordWrap(true);
        v->addWidget(m_status);

        auto* row = new QHBoxLayout;
        row->addStretch(1);
        m_cancel = new QPushButton(QStringLiteral("Cancel"));
        m_go = new QPushButton(QStringLiteral("Import"));
        m_go->setDefault(true);
        row->addWidget(m_cancel);
        row->addWidget(m_go);
        v->addLayout(row);

        connect(file, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a playlist file"), QString(),
                                                           QStringLiteral("Playlist files (*.csv *.tsv *.txt);;All files (*)"));
            if (!f.isEmpty()) m_input->setPlainText(f);
        });
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_go, &QPushButton::clicked, this, &MultiImportDialog::begin);
    }

    QString playlistName() const { return m_resultName; }
    QVector<Track> tracks() const { return m_tracks; }
    QString note() const { return m_resultNote; }

    void reject() override
    {
        if (m_imp) m_imp->cancel();
        QDialog::reject();
    }

private:
    void begin()
    {
        const QString text = m_input->toPlainText().trimmed();
        if (text.isEmpty()) {
            m_status->setText(QStringLiteral("Paste a link or a list of songs first."));
            return;
        }
        m_go->setEnabled(false);
        m_status->setText(QStringLiteral("Working\u2026"));
        m_imp = new Importer(this);
        connect(m_imp, &Importer::progress, this, [this](const QString& s) { m_status->setText(s); });
        connect(m_imp, &Importer::finished, this,
                [this](bool ok, const QString& name, const QVector<Track>& tracks, const QString& msg) {
            if (!ok) {
                m_go->setEnabled(true);
                m_status->setText(msg);
                return;
            }
            const QString typed = m_name->text().trimmed();
            m_resultName = typed.isEmpty() ? name : typed;
            m_tracks = tracks;
            m_resultNote = msg;
            accept();
        });
        m_imp->start(text);
    }

    QPlainTextEdit* m_input = nullptr;
    QLineEdit*      m_name = nullptr;
    QLabel*         m_status = nullptr;
    QPushButton*    m_go = nullptr;
    QPushButton*    m_cancel = nullptr;
    QPointer<Importer> m_imp;
    QString         m_resultName, m_resultNote;
    QVector<Track>  m_tracks;
};
