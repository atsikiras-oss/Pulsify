#pragma once
#include <QtWidgets>
#include <functional>
#include "library.h"
#include "google.h"
#include "online.h"

// ============================================================================
//  Import dialogs: bring playlists in from YouTube / YouTube Music.
//
//   * "Google account" tab: sign in, tick the playlists you want, import.
//     Includes private playlists and your Liked videos.
//   * "From link" tab: paste any public/unlisted playlist link (YouTube or
//     YouTube Music). No account needed; uses yt-dlp.
// ============================================================================

inline QString pulseDialogStyle()
{
    return QStringLiteral(
        "QDialog{background:#282828;}"
        "QLabel{color:white;background:transparent;font-size:13px;}"
        "QPushButton#pill{background:rgba(255,255,255,0.09);color:white;border-radius:16px;padding:0 18px;"
        "min-height:32px;font-size:13px;font-weight:600;}"
        "QPushButton#pill:hover{background:rgba(255,255,255,0.16);}"
        "QPushButton#pill:checked{background:white;color:black;}"
        "QCheckBox{color:white;font-size:13px;spacing:10px;background:transparent;}"
        "QCheckBox::indicator{width:18px;height:18px;border:1px solid #8a8a8a;border-radius:5px;}"
        "QCheckBox::indicator:checked{background:#1DB954;border-color:#1DB954;}"
        "QSlider::groove:horizontal{height:4px;background:rgba(255,255,255,0.22);border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#1DB954;border-radius:2px;}"
        "QSlider::handle:horizontal{background:white;width:14px;height:14px;margin:-5px 0;border-radius:7px;}"
        "QScrollArea{background:transparent;border:none;}"
        "QListWidget{background:rgba(255,255,255,0.05);border:none;border-radius:10px;color:white;"
        "font-size:14px;outline:none;padding:4px;}"
        "QListWidget::item{padding:9px 8px;border-radius:6px;}"
        "QListWidget::item:hover{background:rgba(255,255,255,0.08);}"
        "QListWidget::indicator{width:16px;height:16px;border:1px solid #8a8a8a;border-radius:4px;}"
        "QListWidget::indicator:checked{background:#1DB954;border-color:#1DB954;}"
        "QLineEdit{background:rgba(255,255,255,0.10);border:1px solid transparent;border-radius:8px;"
        "padding:0 14px;min-height:42px;color:white;font-size:14px;selection-background-color:#1DB954;}"
        "QLineEdit:focus{background:rgba(255,255,255,0.14);border:1px solid rgba(255,255,255,0.7);}"
        "QPushButton{background:rgba(255,255,255,0.12);color:white;border:none;border-radius:18px;"
        "padding:0 18px;min-height:36px;font-size:13px;font-weight:700;}"
        "QPushButton:hover{background:rgba(255,255,255,0.20);}"
        "QPushButton:disabled{color:#777;background:rgba(255,255,255,0.06);}"
        "QPushButton#primary{background:#1DB954;color:black;}"
        "QPushButton#primary:hover{background:#1ed760;}"
        "QPushButton#primary:disabled{background:rgba(255,255,255,0.10);color:#777;}");
}

// ---------------------------------------------------------------------------
//  One-time Google Cloud setup: client ID + secret
// ---------------------------------------------------------------------------
class GoogleSetupDialog : public QDialog
{
public:
    GoogleSetupDialog(GoogleAccount* g, QWidget* parent) : QDialog(parent), m_g(g)
    {
        setWindowTitle(QStringLiteral("Google setup"));
        setMinimumWidth(580);
        sty(this, pulseDialogStyle());

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(26, 24, 26, 20);
        v->setSpacing(12);

        auto* h = new QLabel(QStringLiteral("Connect your own Google project"));
        sty(h, QStringLiteral("font-size:22px;font-weight:800;color:white;background:transparent;"));
        v->addWidget(h);

        auto* info = new QLabel(QStringLiteral(
            "Google only lets an app read your YouTube playlists through an OAuth client that you create. "
            "It is free and takes a few minutes, once:<br><br>"
            "1. <a href=\"https://console.cloud.google.com/projectcreate\">Create a project</a> (any name).<br>"
            "2. <a href=\"https://console.cloud.google.com/apis/library/youtube.googleapis.com\">Enable "
            "\"YouTube Data API v3\"</a> for it.<br>"
            "3. <a href=\"https://console.cloud.google.com/apis/credentials/consent\">Set up the consent screen</a> "
            "(User type: External) and add your own Google account as a <i>Test user</i>.<br>"
            "4. <a href=\"https://console.cloud.google.com/apis/credentials\">Create credentials &rarr; OAuth client ID</a>, "
            "application type <b>Desktop app</b>.<br>"
            "5. Paste the client ID and secret below, or load the JSON file Google lets you download.<br><br>"
            "<span style=\"color:#a7a7a7\">Pulsify only asks for read-only access to your YouTube data. While your project "
            "is in \"Testing\" mode Google ends the sign-in after 7 days; set it to \"In production\" to avoid that "
            "(Google will show an \"unverified app\" warning, which is normal for your own project).</span>"));
        info->setTextFormat(Qt::RichText);
        info->setWordWrap(true);
        info->setOpenExternalLinks(true);
        QPalette lp = info->palette();
        lp.setColor(QPalette::Link, QColor(0x1e, 0xd7, 0x60));
        info->setPalette(lp);
        v->addWidget(info);

        m_id = new QLineEdit(g->clientId());
        m_id->setPlaceholderText(QStringLiteral("Client ID  (xxxx.apps.googleusercontent.com)"));
        m_secret = new QLineEdit(g->clientSecret());
        m_secret->setPlaceholderText(QStringLiteral("Client secret"));
        m_secret->setEchoMode(QLineEdit::Password);
        v->addWidget(m_id);
        v->addWidget(m_secret);

        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* load = new QPushButton(QStringLiteral("Load JSON file..."));
        auto* cancel = new QPushButton(QStringLiteral("Cancel"));
        auto* save = new QPushButton(QStringLiteral("Save"));
        save->setObjectName(QStringLiteral("primary"));
        for (auto* b : { load, cancel, save }) { b->setAutoDefault(false); b->setCursor(Qt::PointingHandCursor); }
        row->addWidget(load);
        row->addStretch(1);
        row->addWidget(cancel);
        row->addWidget(save);
        v->addLayout(row);

        connect(load, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("OAuth client JSON"), QString(),
                                                           QStringLiteral("JSON (*.json);;All files (*)"));
            if (f.isEmpty()) return;
            QString id, secret;
            if (!GoogleAccount::parseCredentialsFile(f, id, secret)) {
                QMessageBox::warning(this, QStringLiteral("Google setup"),
                                     QStringLiteral("That file doesn't contain a client ID and secret. Download the "
                                                    "JSON of a \"Desktop app\" OAuth client."));
                return;
            }
            m_id->setText(id);
            m_secret->setText(secret);
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(save, &QPushButton::clicked, this, [this] {
            if (m_id->text().trimmed().isEmpty() || m_secret->text().trimmed().isEmpty()) {
                QMessageBox::information(this, QStringLiteral("Google setup"),
                                         QStringLiteral("Enter both the client ID and the client secret."));
                return;
            }
            m_g->setCredentials(m_id->text(), m_secret->text());
            accept();
        });
    }

private:
    GoogleAccount* m_g;
    QLineEdit *m_id = nullptr, *m_secret = nullptr;
};

// ---------------------------------------------------------------------------
//  The import dialog
// ---------------------------------------------------------------------------
class ImportDialog : public QDialog
{
public:
    // Called once per playlist; returns a one-line result ("Imported X (12 songs)") shown in the dialog.
    using Handler = std::function<QString(const QString& name, const QString& source,
                                          const QVector<OnlineItem>& items)>;

    ImportDialog(GoogleAccount* g, YtDlp* yt, QWidget* parent) : QDialog(parent), m_g(g), m_yt(yt)
    {
        setWindowTitle(QStringLiteral("Import playlists"));
        setMinimumSize(560, 580);
        resize(620, 640);
        sty(this, pulseDialogStyle());

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(26, 24, 26, 20);
        v->setSpacing(10);
        auto* h = new QLabel(QStringLiteral("Import from YouTube"));
        sty(h, QStringLiteral("font-size:24px;font-weight:800;color:white;background:transparent;"));
        v->addWidget(h);

        auto* pills = new QHBoxLayout;
        pills->setSpacing(8);
        auto* pages = new QStackedWidget;
        auto* group = new QButtonGroup(this);
        const QStringList names = { QStringLiteral("Google account"), QStringLiteral("From link") };
        const QList<QWidget*> tabPages = { buildAccountTab(), buildLinkTab() };
        for (int i = 0; i < names.size(); ++i) {
            auto* b = new QPushButton(names[i]);
            b->setObjectName(QStringLiteral("pill"));
            b->setCheckable(true);
            b->setChecked(i == 0);
            b->setAutoDefault(false);
            b->setFocusPolicy(Qt::NoFocus);
            b->setCursor(Qt::PointingHandCursor);
            group->addButton(b, i);
            pills->addWidget(b);
            pages->addWidget(tabPages[i]);
        }
        pills->addStretch(1);
        v->addSpacing(2);
        v->addLayout(pills);
        v->addSpacing(6);
        v->addWidget(pages, 1);
        connect(group, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);

        m_result = new QLabel;
        m_result->setWordWrap(true);
        m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_result->hide();
        v->addWidget(m_result);

        auto* close = new QPushButton(QStringLiteral("Close"));
        close->setAutoDefault(false);
        close->setCursor(Qt::PointingHandCursor);
        auto* row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(close);
        v->addLayout(row);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);

        connect(m_g, &GoogleAccount::stateChanged, this, [this] { updateState(); });
        connect(m_g, &GoogleAccount::signInFailed, this, [this](const QString& e) { showAccountMsg(e, true); updateState(); });
        connect(m_g, &GoogleAccount::playlistsReady, this, &ImportDialog::onPlaylists);
        connect(m_g, &GoogleAccount::itemsProgress, this, [this](const QString& id, int got, int total) {
            if (id != m_cur.id) return;
            showAccountMsg(QStringLiteral("Reading \"%1\"...  %2%3").arg(m_cur.title).arg(got)
                               .arg(total > 0 ? QStringLiteral(" of %1").arg(total) : QString()), false);
        });
        connect(m_g, &GoogleAccount::itemsReady, this, &ImportDialog::onItems);
        connect(m_yt, &YtDlp::playlistImported, this, &ImportDialog::onLinkImported);

        updateState();
        if (m_g->signedIn()) loadPlaylists();
    }

    void setHandler(Handler h) { m_handler = std::move(h); }

protected:
    void done(int r) override
    {
        m_yt->cancelImport();
        m_g->cancelAll();
        QDialog::done(r);
    }

private:
    // ------------------------------------------------------------ Google tab
    QWidget* buildAccountTab()
    {
        auto* w = new QWidget;
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 6, 0, 0);
        v->setSpacing(10);

        m_status = new QLabel;
        m_status->setWordWrap(true);
        m_status->setTextFormat(Qt::RichText);
        v->addWidget(m_status);

        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        m_signIn = new QPushButton(QStringLiteral("Sign in with Google"));
        m_signIn->setObjectName(QStringLiteral("primary"));
        m_signOut = new QPushButton(QStringLiteral("Sign out"));
        m_setup = new QPushButton(QStringLiteral("Google setup..."));
        for (auto* b : { m_signIn, m_signOut, m_setup }) { b->setAutoDefault(false); b->setCursor(Qt::PointingHandCursor); }
        row->addWidget(m_signIn);
        row->addWidget(m_signOut);
        row->addStretch(1);
        row->addWidget(m_setup);
        v->addLayout(row);

        m_accMsg = new QLabel;
        m_accMsg->setWordWrap(true);
        m_accMsg->hide();
        v->addWidget(m_accMsg);

        m_list = new QListWidget;
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        v->addWidget(m_list, 1);

        auto* row2 = new QHBoxLayout;
        row2->setSpacing(8);
        m_refresh = new QPushButton(QStringLiteral("Refresh"));
        m_toggleAll = new QPushButton(QStringLiteral("Select all"));
        m_importSel = new QPushButton(QStringLiteral("Import selected"));
        m_importSel->setObjectName(QStringLiteral("primary"));
        for (auto* b : { m_refresh, m_toggleAll, m_importSel }) { b->setAutoDefault(false); b->setCursor(Qt::PointingHandCursor); }
        row2->addWidget(m_refresh);
        row2->addWidget(m_toggleAll);
        row2->addStretch(1);
        row2->addWidget(m_importSel);
        v->addLayout(row2);

        connect(m_setup, &QPushButton::clicked, this, [this] {
            GoogleSetupDialog d(m_g, this);
            if (d.exec() == QDialog::Accepted) { showAccountMsg(QString(), false); updateState(); }
        });
        connect(m_signIn, &QPushButton::clicked, this, [this] {
            if (m_g->signingIn()) { m_g->cancelSignIn(); showAccountMsg(QString(), false); }
            else { showAccountMsg(QString(), false); m_g->signIn(); }
            updateState();
        });
        connect(m_signOut, &QPushButton::clicked, this, [this] {
            m_list->clear();
            m_tried = false;
            showAccountMsg(QString(), false);
            m_g->signOut();
        });
        connect(m_refresh, &QPushButton::clicked, this, [this] { loadPlaylists(); });
        connect(m_toggleAll, &QPushButton::clicked, this, [this] {
            const bool on = (m_toggleAll->text() == QLatin1String("Select all"));
            for (int i = 0; i < m_list->count(); ++i) m_list->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            m_toggleAll->setText(on ? QStringLiteral("Select none") : QStringLiteral("Select all"));
            updateState();
        });
        connect(m_list, &QListWidget::itemChanged, this, [this] { if (!m_busy) updateState(); });
        connect(m_importSel, &QPushButton::clicked, this, [this] { startImport(); });
        return w;
    }

    void showAccountMsg(const QString& text, bool error)
    {
        sty(m_accMsg, QStringLiteral("color:%1;font-size:13px;background:transparent;")
                                    .arg(error ? "#ff8a8a" : "#c8c8c8"));
        m_accMsg->setText(text);
        m_accMsg->setVisible(!text.isEmpty());
    }

    void updateState()
    {
        const bool cred = m_g->hasCredentials();
        const bool in = m_g->signedIn();
        const bool signing = m_g->signingIn();
        if (!cred)
            m_status->setText(QStringLiteral("Google isn't set up yet. Click <b>Google setup</b> and add your client ID and "
                                             "secret (one-time)."));
        else if (signing)
            m_status->setText(QStringLiteral("Finish signing in in your browser, then come back here..."));
        else if (!in)
            m_status->setText(QStringLiteral("Not signed in. Sign in to import your own playlists, including private ones."));
        else
            m_status->setText(m_g->accountName().isEmpty()
                ? QStringLiteral("Signed in to Google.")
                : QStringLiteral("Signed in as <b>%1</b>").arg(m_g->accountName().toHtmlEscaped()));

        m_signIn->setVisible(!in);
        m_signIn->setText(signing ? QStringLiteral("Cancel sign-in") : QStringLiteral("Sign in with Google"));
        m_signIn->setEnabled(cred && !m_busy);
        m_signOut->setVisible(in);
        m_signOut->setEnabled(!m_busy);
        m_setup->setEnabled(!m_busy && !signing);
        m_list->setEnabled(in && !m_busy);
        m_refresh->setEnabled(in && !m_busy);
        m_toggleAll->setEnabled(in && !m_busy && m_list->count() > 0);

        int checked = 0;
        for (int i = 0; i < m_list->count(); ++i) if (m_list->item(i)->checkState() == Qt::Checked) ++checked;
        m_importSel->setEnabled(in && !m_busy && checked > 0);
        m_importSel->setText(checked > 0 ? QStringLiteral("Import %1 playlist%2").arg(checked).arg(checked == 1 ? "" : "s")
                                         : QStringLiteral("Import selected"));

        if (!in) m_tried = false;
        else if (!m_tried && !m_busy) loadPlaylists();                    // just signed in: show the playlists
    }

    void loadPlaylists()
    {
        if (!m_g->signedIn()) return;
        m_tried = true;
        m_list->clear();
        showAccountMsg(QStringLiteral("Loading your playlists..."), false);
        updateState();
        m_g->fetchPlaylists();
    }

    void onPlaylists(const QVector<RemotePlaylist>& lists, const QString& err)
    {
        m_list->clear();
        if (!err.isEmpty()) { showAccountMsg(err, true); updateState(); return; }
        QSignalBlocker b(m_list);
        for (const RemotePlaylist& p : lists) {
            auto* it = new QListWidgetItem(p.count >= 0
                ? QStringLiteral("%1   \u00b7   %2 video%3").arg(p.title).arg(p.count).arg(p.count == 1 ? "" : "s")
                : p.title);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            it->setCheckState(Qt::Unchecked);
            it->setData(Qt::UserRole, p.id);
            it->setData(Qt::UserRole + 1, p.title);
            m_list->addItem(it);
        }
        m_toggleAll->setText(QStringLiteral("Select all"));
        showAccountMsg(lists.isEmpty() ? QStringLiteral("No playlists found on this account.") : QString(), false);
        updateState();
    }

    void startImport()
    {
        m_pending.clear();
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem* it = m_list->item(i);
            if (it->checkState() != Qt::Checked) continue;
            RemotePlaylist p;
            p.id = it->data(Qt::UserRole).toString();
            p.title = it->data(Qt::UserRole + 1).toString();
            m_pending.push_back(p);
        }
        if (m_pending.isEmpty()) return;
        m_log.clear();
        m_total = int(m_pending.size());
        m_busy = true;
        m_result->hide();
        updateState();
        nextPlaylist();
    }

    void nextPlaylist()
    {
        if (m_pending.isEmpty()) {
            m_busy = false;
            m_cur = RemotePlaylist();
            showAccountMsg(QString(), false);
            showResult(m_log.join(QLatin1Char('\n')));
            updateState();
            return;
        }
        m_cur = m_pending.takeFirst();
        showAccountMsg(QStringLiteral("Reading \"%1\"  (%2 of %3)...")
                           .arg(m_cur.title).arg(m_total - int(m_pending.size())).arg(m_total), false);
        m_g->fetchPlaylistItems(m_cur.id);
    }

    void onItems(const QString& id, const QVector<OnlineItem>& items, const QString& err)
    {
        if (!m_busy || id != m_cur.id) return;
        const QString title = m_cur.title;
        if (!err.isEmpty()) {
            m_log << QStringLiteral("\u2022 %1: %2").arg(title, err);
        } else if (items.isEmpty()) {
            m_log << QStringLiteral("\u2022 %1: nothing to import (empty, or only private/deleted videos)").arg(title);
        } else if (m_handler) {
            const QString name = (title == QLatin1String("Liked videos")) ? QStringLiteral("YouTube liked videos") : title;
            m_log << QStringLiteral("\u2022 ") + m_handler(name, QStringLiteral("yt:") + id, items);
        }
        nextPlaylist();
    }

    // ------------------------------------------------------------ link tab
    QWidget* buildLinkTab()
    {
        auto* w = new QWidget;
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(0, 6, 0, 0);
        v->setSpacing(12);

        auto* info = new QLabel(QStringLiteral(
            "Paste the link of a YouTube or YouTube Music playlist. This works for public and unlisted playlists and "
            "needs no sign-in. Importing the same link again only adds songs that are new."));
        info->setWordWrap(true);
        v->addWidget(info);

        m_link = new QLineEdit;
        m_link->setPlaceholderText(QStringLiteral("https://music.youtube.com/playlist?list=..."));
        m_link->setClearButtonEnabled(true);
        v->addWidget(m_link);

        auto* row = new QHBoxLayout;
        m_linkBtn = new QPushButton(QStringLiteral("Import playlist"));
        m_linkBtn->setObjectName(QStringLiteral("primary"));
        m_linkBtn->setAutoDefault(false);
        m_linkBtn->setCursor(Qt::PointingHandCursor);
        row->addWidget(m_linkBtn);
        row->addStretch(1);
        v->addLayout(row);

        m_linkMsg = new QLabel;
        m_linkMsg->setWordWrap(true);
        m_linkMsg->hide();
        v->addWidget(m_linkMsg);
        v->addStretch(1);

        connect(m_linkBtn, &QPushButton::clicked, this, [this] { importLink(); });
        connect(m_link, &QLineEdit::returnPressed, this, [this] { importLink(); });
        return w;
    }

    void showLinkMsg(const QString& text, bool error)
    {
        sty(m_linkMsg, QStringLiteral("color:%1;font-size:13px;background:transparent;")
                                     .arg(error ? "#ff8a8a" : "#c8c8c8"));
        m_linkMsg->setText(text);
        m_linkMsg->setVisible(!text.isEmpty());
    }

    void importLink()
    {
        if (m_linkTok) return;
        m_linkId = YtDlp::playlistIdFromLink(m_link->text());
        if (m_linkId.isEmpty()) {
            showLinkMsg(QStringLiteral("That doesn't look like a playlist link. It should contain \"list=\"."), true);
            return;
        }
        if (!m_yt->available()) {
            showLinkMsg(QStringLiteral("yt-dlp isn't installed yet. Close this window and use \"Install automatically\" "
                                       "in the banner on Home, then try again."), true);
            return;
        }
        m_linkBtn->setEnabled(false);
        showLinkMsg(QStringLiteral("Reading the playlist... big ones can take a minute."), false);
        m_linkTok = m_yt->importPlaylist(m_linkId);
    }

    void onLinkImported(quint64 tok, const QString& title, const QVector<OnlineItem>& items, const QString& err)
    {
        if (tok != m_linkTok) return;
        m_linkTok = 0;
        m_linkBtn->setEnabled(true);
        if (!err.isEmpty()) { showLinkMsg(err, true); return; }
        const QString name = title.isEmpty() ? QStringLiteral("Imported playlist") : title;
        const QString msg = m_handler ? m_handler(name, QStringLiteral("yt:") + m_linkId, items) : QString();
        showLinkMsg(msg, false);
        m_link->clear();
    }

    void showResult(const QString& text)
    {
        sty(m_result, QStringLiteral("color:#1ed760;font-size:13px;background:transparent;"));
        m_result->setText(text);
        m_result->setVisible(!text.isEmpty());
    }

    GoogleAccount* m_g;
    YtDlp*         m_yt;
    Handler        m_handler;

    QLabel*       m_status = nullptr;
    QLabel*       m_accMsg = nullptr;
    QLabel*       m_result = nullptr;
    QPushButton  *m_signIn = nullptr, *m_signOut = nullptr, *m_setup = nullptr;
    QPushButton  *m_refresh = nullptr, *m_toggleAll = nullptr, *m_importSel = nullptr;
    QListWidget*  m_list = nullptr;
    QVector<RemotePlaylist> m_pending;
    RemotePlaylist m_cur;
    QStringList   m_log;
    int           m_total = 0;
    bool          m_busy = false, m_tried = false;

    QLineEdit*    m_link = nullptr;
    QPushButton*  m_linkBtn = nullptr;
    QLabel*       m_linkMsg = nullptr;
    QString       m_linkId;
    quint64       m_linkTok = 0;
};


// ============================================================================
//  Importer: handles multiple input formats for the new ImportDialog (dialogs.h).
//  Accepts YouTube playlist links, plain-text "Artist - Title" lists, and CSV/text
//  exports from Exportify, TuneMyMusic, Soundiiz (covers Spotify, Apple Music, etc.)
// ============================================================================

inline QVector<QStringList> parseCsvRows(const QString& text, QChar delim)
{
    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool inQ = false;
    const QChar quote(QLatin1Char('"'));
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQ) {
            if (c == quote) {
                if (i + 1 < text.size() && text.at(i + 1) == quote) { cell += quote; ++i; }
                else inQ = false;
            } else { cell += c; }
        } else if (c == quote) { inQ = true; }
        else if (c == delim) { row << cell; cell.clear(); }
        else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            row << cell; cell.clear();
            if (!(row.size() == 1 && row.first().trimmed().isEmpty())) rows << row;
            row.clear();
        } else { cell += c; }
    }
    if (!cell.isEmpty() || !row.isEmpty()) { row << cell; rows << row; }
    return rows;
}

inline qint64 parseDurCell(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) return 0;
    if (s.contains(QLatin1Char(':'))) {
        const QStringList p = s.split(QLatin1Char(':'));
        qint64 sec = 0;
        for (const QString& part : p) sec = sec * 60 + part.toLongLong();
        return sec * 1000;
    }
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || v <= 0) return 0;
    return v > 10000 ? qint64(v) : qint64(v * 1000.0);
}

class Importer : public QObject
{
    Q_OBJECT
public:
    explicit Importer(QObject* parent = nullptr) : QObject(parent) {}

    void start(const QString& input)
    {
        const QString text = input.trimmed();
        if (QFileInfo::exists(text)) { loadFile(text); return; }
        const QString pid = YtDlp::playlistIdFromLink(text);
        if (!pid.isEmpty()) { importYt(pid); return; }
        importLines(text);
    }

    void cancel() { m_cancelled = true; if (m_yt) m_yt->cancelImport(); }

signals:
    void progress(const QString& status);
    void finished(bool ok, const QString& name, const QVector<Track>& tracks, const QString& msg);

private:
    static int nextId() { static int g = 20000000; return ++g; }

    void loadFile(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit finished(false, QString(), {}, QStringLiteral("Couldn't open file: ") + path);
            return;
        }
        const QString text = QString::fromUtf8(f.readAll());
        QVector<Track> tracks = parseCsvTracks(text);
        if (!tracks.isEmpty()) { emit finished(true, QFileInfo(path).baseName(), tracks, QString()); return; }
        importLines(text);
    }

    QVector<Track> parseCsvTracks(const QString& text)
    {
        const QChar delim = text.count(QLatin1Char('\t')) > text.count(QLatin1Char(','))
                            ? QLatin1Char('\t') : QLatin1Char(',');
        const QVector<QStringList> rows = parseCsvRows(text, delim);
        if (rows.size() < 2) return {};
        const QStringList& hdr = rows[0];
        int colTitle = -1, colArtist = -1, colAlbum = -1, colDur = -1;
        for (int i = 0; i < hdr.size(); ++i) {
            const QString h = hdr[i].toLower().trimmed();
            if (h.contains(QLatin1String("title")) || h.contains(QLatin1String("track name")))
                if (colTitle < 0) colTitle = i;
            if (h.contains(QLatin1String("artist")) && colArtist < 0) colArtist = i;
            if (h.contains(QLatin1String("album")) && colAlbum < 0) colAlbum = i;
            if (h.contains(QLatin1String("duration")) && colDur < 0) colDur = i;
        }
        if (colTitle < 0 || colArtist < 0) return {};
        QVector<Track> out;
        for (int r = 1; r < rows.size(); ++r) {
            const QStringList& row = rows[r];
            auto col = [&](int i) -> QString { return (i >= 0 && i < row.size()) ? row[i].trimmed() : QString(); };
            if (col(colTitle).isEmpty()) continue;
            Track t;
            t.id = nextId();
            t.title = col(colTitle);
            t.artist = col(colArtist);
            if (colAlbum >= 0) t.album = col(colAlbum);
            if (colDur >= 0) t.durationMs = parseDurCell(col(colDur));
            t.query = (t.artist + QLatin1Char(' ') + t.title).trimmed();
            t.locked = true;
            out.push_back(t);
        }
        return out;
    }

    void importLines(const QString& text)
    {
        static const QRegularExpression sep(QStringLiteral(R"(^(.{1,80}?)\s*[-\u2013\u2014]\s*(.+)$)"));
        QVector<Track> tracks;
        for (const QString& raw : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            Track t;
            t.id = nextId();
            const QRegularExpressionMatch m = sep.match(line);
            if (m.hasMatch()) { t.artist = m.captured(1).trimmed(); t.title = m.captured(2).trimmed(); }
            else t.title = line;
            t.query = (t.artist + QLatin1Char(' ') + t.title).trimmed();
            tracks.push_back(t);
        }
        if (tracks.isEmpty()) {
            emit finished(false, QString(), {}, QStringLiteral("No songs found. Paste one per line (Artist - Title) or choose a CSV."));
            return;
        }
        emit finished(true, QStringLiteral("Imported songs"), tracks,
            QStringLiteral("%1 song%2 imported.").arg(tracks.size()).arg(tracks.size() == 1 ? "" : "s"));
    }

    void importYt(const QString& playlistId)
    {
        emit progress(QStringLiteral("Importing from YouTube\u2026"));
        m_yt = new YtDlp(this);
        const quint64 tok = m_yt->importPlaylist(playlistId);
        connect(m_yt, &YtDlp::playlistImported, this,
            [this, tok](quint64 t, const QString& name, const QVector<OnlineItem>& items, const QString& err) {
            if (t != tok || m_cancelled) return;
            if (!err.isEmpty()) { emit finished(false, name, {}, err); return; }
            QVector<Track> tracks;
            for (const OnlineItem& it : items) {
                Track tr;
                tr.id = nextId();
                tr.online = true;
                tr.videoId = it.id;
                tr.title = it.title;
                tr.artist = it.artist;
                tr.album = it.album;
                tr.durationMs = it.durationMs;
                tr.thumbUrl = it.thumbUrl;
                tracks.push_back(tr);
            }
            emit finished(true, name, tracks, QString());
        });
    }

    bool    m_cancelled = false;
    YtDlp*  m_yt = nullptr;
};
