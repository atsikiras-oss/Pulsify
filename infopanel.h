#pragma once
// ============================================================================
//  InfoPanel: right-hand sidebar with everything known about the current song
//  (cover, title, artist, album, format / bitrate, source, file or YouTube
//  details) plus what plays next.  MainWindow fills it through setInfo().
// ============================================================================
#include <QtWidgets>
#include "widgets.h"
#include "online.h"
#include "ui.h"

struct SongInfo
{
    bool    valid = false, online = false;
    QString title, artist, album, videoId, path;
    qint64  durationMs = 0;
    QPixmap cover;
    QColor  tint;

    StreamInfo stream;                                   // YouTube songs (from yt-dlp)

    // local files (from the media backend's tags)
    QString codec, format, genre, year, trackNo;
    int     bitrateKbps = 0;

    // what plays next
    bool    hasNext = false;
    QString nextTitle, nextArtist;
    QPixmap nextCover;
};

class InfoPanel : public TintWidget
{
    Q_OBJECT
public:
    static constexpr int kWidth = 320;

    explicit InfoPanel(QWidget* parent = nullptr) : TintWidget(0.55, 380, 12, parent)
    {
        setFixedWidth(kWidth);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // ---- header
        auto* hdr = new QHBoxLayout;
        hdr->setContentsMargins(18, 14, 10, 4);
        auto* ht = new QLabel(QStringLiteral("Now playing"));
        sty(ht, QStringLiteral("font-size:15px;font-weight:700;color:white;background:transparent;"));
        auto* close = new QPushButton(QStringLiteral("\u2715"));
        close->setCursor(Qt::PointingHandCursor);
        close->setFocusPolicy(Qt::NoFocus);
        close->setFixedSize(30, 30);
        close->setToolTip(QStringLiteral("Hide (Ctrl+Shift+I)"));
        sty(close, QStringLiteral(
            "QPushButton{background:transparent;color:#b3b3b3;border:none;border-radius:15px;font-size:13px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.14);color:white;}"));
        hdr->addWidget(ht, 1);
        hdr->addWidget(close);
        v->addLayout(hdr);
        connect(close, &QAbstractButton::clicked, this, &InfoPanel::closeRequested);

        // ---- scrolling body
        m_scroll = new SmoothScrollArea;
        auto* body = new QWidget;
        auto* bv = new QVBoxLayout(body);
        bv->setContentsMargins(18, 8, 18, 18);
        bv->setSpacing(0);

        m_empty = new QLabel(QStringLiteral("Nothing playing yet.\nPlay a song to see its details here."));
        m_empty->setAlignment(Qt::AlignCenter);
        m_empty->setWordWrap(true);
        sty(m_empty, QStringLiteral("color:#8a8a8a;font-size:13px;background:transparent;padding:60px 10px;"));
        bv->addWidget(m_empty);

        m_content = new QWidget;
        m_content->setAttribute(Qt::WA_TranslucentBackground);
        auto* cv = new QVBoxLayout(m_content);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(0);

        const int cw = kWidth - 36;
        m_cover = new CoverView(12, 0);
        m_cover->setFixedSize(cw, cw);
        cv->addWidget(m_cover);
        cv->addSpacing(16);

        m_title = new QLabel;
        m_title->setWordWrap(true);
        m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sty(m_title, QStringLiteral("font-size:22px;font-weight:800;color:white;background:transparent;"));
        m_artist = new QLabel;
        m_artist->setWordWrap(true);
        m_artist->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sty(m_artist, QStringLiteral("font-size:14px;font-weight:600;color:#c4c4c4;background:transparent;"));
        cv->addWidget(m_title);
        cv->addSpacing(2);
        cv->addWidget(m_artist);
        cv->addSpacing(16);

        m_aboutCard  = makeCard(QStringLiteral("About"), &m_aboutRows);
        m_sourceCard = makeCard(QStringLiteral("Source"), &m_sourceRows);
        cv->addWidget(m_aboutCard);
        cv->addSpacing(10);
        cv->addWidget(m_sourceCard);
        cv->addSpacing(10);

        // buttons live inside the source card
        m_btnRow = new QHBoxLayout;
        m_btnRow->setSpacing(8);
        m_btnA = pill(QStringLiteral("Open"));
        m_btnB = pill(QStringLiteral("Copy link"));
        m_btnRow->addWidget(m_btnA);
        m_btnRow->addWidget(m_btnB);
        m_btnRow->addStretch(1);
        static_cast<QVBoxLayout*>(m_sourceCard->layout())->addLayout(m_btnRow);

        // next in queue
        m_nextCard = new QFrame;
        m_nextCard->setObjectName(QStringLiteral("card"));
        sty(m_nextCard, cardStyle());
        auto* nv = new QVBoxLayout(m_nextCard);
        nv->setContentsMargins(14, 12, 14, 12);
        nv->setSpacing(8);
        nv->addWidget(heading(QStringLiteral("Next in queue")));
        auto* nr = new QHBoxLayout;
        nr->setSpacing(10);
        m_nextCover = new CoverView(6, 0);
        m_nextCover->setFixedSize(46, 46);
        auto* nt = new QVBoxLayout;
        nt->setSpacing(0);
        m_nextTitle  = new ElidedLabel(13, QFont::DemiBold, Qt::white);
        m_nextArtist = new ElidedLabel(12, QFont::Normal, Theme::subtext());
        nt->addStretch(1);
        nt->addWidget(m_nextTitle);
        nt->addWidget(m_nextArtist);
        nt->addStretch(1);
        nr->addWidget(m_nextCover);
        nr->addLayout(nt, 1);
        nv->addLayout(nr);
        cv->addWidget(m_nextCard);
        cv->addStretch(1);

        bv->addWidget(m_content);
        bv->addStretch(1);
        m_scroll->setBody(body);
        v->addWidget(m_scroll, 1);

        connect(m_btnA, &QAbstractButton::clicked, this, [this] { m_actionA ? m_actionA() : void(); });
        connect(m_btnB, &QAbstractButton::clicked, this, [this] { m_actionB ? m_actionB() : void(); });

        setInfo(SongInfo());
    }

    void setInfo(const SongInfo& s)
    {
        m_empty->setVisible(!s.valid);
        m_content->setVisible(s.valid);
        if (!s.valid) { setTint(Theme::defaultTint()); return; }

        // a different song starts at the top again
        const QString key = s.online ? s.videoId : s.path;
        if (key != m_key) { m_key = key; m_scroll->scrollToTop(); }

        setTint(s.tint.isValid() ? s.tint : Theme::defaultTint());
        m_cover->setPixmap(s.cover);
        m_title->setText(s.title);
        m_artist->setText(s.artist);

        // ---- About
        clearRows(m_aboutRows);
        addRow(m_aboutRows, QStringLiteral("Album"), s.album);
        addRow(m_aboutRows, QStringLiteral("Duration"), s.durationMs > 0 ? fmtTime(s.durationMs) : QString());
        addRow(m_aboutRows, QStringLiteral("Genre"), s.genre);
        addRow(m_aboutRows, QStringLiteral("Year"), s.year);
        addRow(m_aboutRows, QStringLiteral("Track"), s.trackNo);
        if (s.online) {
            addRow(m_aboutRows, QStringLiteral("Channel"), s.stream.channel);
            if (s.stream.views > 0)
                addRow(m_aboutRows, QStringLiteral("Views"), QLocale().toString(s.stream.views));
            const QDate d = QDate::fromString(s.stream.uploadDate, QStringLiteral("yyyyMMdd"));
            if (d.isValid())
                addRow(m_aboutRows, QStringLiteral("Uploaded"), QLocale().toString(d, QLocale::LongFormat));
        }
        m_aboutCard->setVisible(m_aboutRows->count() > 0);

        // ---- Source (stream details or file details)
        clearRows(m_sourceRows);
        m_actionA = m_actionB = nullptr;
        if (s.online) {
            m_sourceCard->findChild<QLabel*>(QStringLiteral("heading"))->setText(QStringLiteral("Stream"));
            addRow(m_sourceRows, QStringLiteral("Source"), QStringLiteral("YouTube"));
            if (s.stream.known()) {
                QStringList fmt;
                const QString codec = prettyCodec(s.stream.codec);
                if (!codec.isEmpty()) fmt << codec;
                if (!s.stream.container.isEmpty()) fmt << s.stream.container.toUpper();
                addRow(m_sourceRows, QStringLiteral("Format"), fmt.join(QStringLiteral(" \u00b7 ")));
                if (s.stream.bitrateKbps > 0)
                    addRow(m_sourceRows, QStringLiteral("Bitrate"), QStringLiteral("%1 kbps").arg(s.stream.bitrateKbps));
                if (s.stream.sampleRate > 0)
                    addRow(m_sourceRows, QStringLiteral("Sample rate"),
                           QStringLiteral("%1 kHz").arg(s.stream.sampleRate / 1000.0, 0, 'f', s.stream.sampleRate % 1000 ? 1 : 0));
            } else {
                addRow(m_sourceRows, QStringLiteral("Format"), QStringLiteral("Loading..."));
            }
            const QString link = s.stream.webUrl.isEmpty()
                ? QStringLiteral("https://www.youtube.com/watch?v=") + s.videoId : s.stream.webUrl;
            m_btnA->setText(QStringLiteral("Open on YouTube"));
            m_btnB->setText(QStringLiteral("Copy link"));
            m_btnA->show();
            m_btnB->show();
            m_actionA = [link] { QDesktopServices::openUrl(QUrl(link)); };
            m_actionB = [link] { QGuiApplication::clipboard()->setText(link); };
        } else {
            m_sourceCard->findChild<QLabel*>(QStringLiteral("heading"))->setText(QStringLiteral("File"));
            const QFileInfo fi(s.path);
            addRow(m_sourceRows, QStringLiteral("Name"), fi.fileName());
            addRow(m_sourceRows, QStringLiteral("Folder"), QDir::toNativeSeparators(fi.absolutePath()));
            QStringList fmt;
            const QString codec = prettyCodec(s.codec);
            if (!codec.isEmpty()) fmt << codec;
            const QString container = !s.format.isEmpty() ? s.format : fi.suffix().toUpper();
            if (!container.isEmpty() && container.compare(codec, Qt::CaseInsensitive) != 0) fmt << container;
            addRow(m_sourceRows, QStringLiteral("Format"), fmt.join(QStringLiteral(" \u00b7 ")));
            if (s.bitrateKbps > 0)
                addRow(m_sourceRows, QStringLiteral("Bitrate"), QStringLiteral("%1 kbps").arg(s.bitrateKbps));
            if (fi.exists()) addRow(m_sourceRows, QStringLiteral("Size"), prettySize(fi.size()));
            m_btnA->setText(QStringLiteral("Show in folder"));
            m_btnA->show();
            m_btnB->hide();
            const QString path = s.path;
            m_actionA = [path] { revealFile(path); };
        }

        // ---- next in queue
        m_nextCard->setVisible(s.hasNext);
        if (s.hasNext) {
            m_nextCover->setPixmap(s.nextCover);
            m_nextTitle->setText(s.nextTitle, false);
            m_nextArtist->setText(s.nextArtist, false);
        }
    }

signals:
    void closeRequested();

private:
    static QString cardStyle()
    {
        return QStringLiteral("QFrame#card{background:rgba(255,255,255,0.07);border-radius:10px;}");
    }

    static QLabel* heading(const QString& t)
    {
        auto* l = new QLabel(t);
        l->setObjectName(QStringLiteral("heading"));
        sty(l, QStringLiteral("font-size:13px;font-weight:700;color:#b3b3b3;background:transparent;"));
        return l;
    }

    static QPushButton* pill(const QString& text)
    {
        auto* b = new QPushButton(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(30);
        sty(b, QStringLiteral(
            "QPushButton{background:rgba(255,255,255,0.12);color:white;border:none;border-radius:15px;"
            "padding:0 14px;font-size:12px;font-weight:700;}"
            "QPushButton:hover{background:rgba(255,255,255,0.22);}"));
        return b;
    }

    // card with a heading and a column of rows (returned through `rows`)
    static QFrame* makeCard(const QString& title, QVBoxLayout** rows)
    {
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        sty(card, cardStyle());
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(14, 12, 14, 12);
        v->setSpacing(8);
        v->addWidget(heading(title));
        auto* holder = new QWidget;
        holder->setAttribute(Qt::WA_TranslucentBackground);
        *rows = new QVBoxLayout(holder);
        (*rows)->setContentsMargins(0, 0, 0, 0);
        (*rows)->setSpacing(7);
        v->addWidget(holder);
        return card;
    }

    static void clearRows(QVBoxLayout* rows)
    {
        while (QLayoutItem* it = rows->takeAt(0)) {
            if (QWidget* w = it->widget()) { w->hide(); w->deleteLater(); }
            delete it;
        }
    }

    static void addRow(QVBoxLayout* rows, const QString& key, const QString& value)
    {
        if (value.trimmed().isEmpty()) return;
        auto* row = new QWidget;
        row->setAttribute(Qt::WA_TranslucentBackground);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        auto* k = new QLabel(key);
        k->setFixedWidth(76);
        k->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        sty(k, QStringLiteral("font-size:12px;color:#9a9a9a;background:transparent;"));
        auto* v = new QLabel(value);
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sty(v, QStringLiteral("font-size:13px;color:white;background:transparent;"));
        h->addWidget(k, 0, Qt::AlignTop);
        h->addWidget(v, 1);
        rows->addWidget(row);
    }

    static QString prettySize(qint64 b)
    {
        const double mb = b / (1024.0 * 1024.0);
        if (mb >= 1.0) return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
        return QStringLiteral("%1 KB").arg(qMax<qint64>(1, b / 1024));
    }

    static QString prettyCodec(const QString& raw)
    {
        const QString c = raw.trimmed().toLower();
        if (c.isEmpty() || c == QLatin1String("none")) return QString();
        if (c.startsWith(QLatin1String("mp4a.40.5")) || c.startsWith(QLatin1String("mp4a.40.29"))) return QStringLiteral("HE-AAC");
        if (c.startsWith(QLatin1String("mp4a")) || c == QLatin1String("aac")) return QStringLiteral("AAC");
        if (c.contains(QLatin1String("opus"))) return QStringLiteral("Opus");
        if (c.contains(QLatin1String("vorbis"))) return QStringLiteral("Vorbis");
        if (c.contains(QLatin1String("flac"))) return QStringLiteral("FLAC");
        if (c.contains(QLatin1String("mp3")) || c.contains(QLatin1String("mpeg"))) return QStringLiteral("MP3");
        if (c.contains(QLatin1String("alac"))) return QStringLiteral("ALAC");
        return raw.trimmed();
    }

    static void revealFile(const QString& path)
    {
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                { QStringLiteral("/select,"), QDir::toNativeSeparators(path) });
#elif defined(Q_OS_MACOS)
        QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), path });
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    }

    SmoothScrollArea* m_scroll = nullptr;
    QLabel*      m_empty = nullptr;
    QWidget*     m_content = nullptr;
    CoverView*   m_cover = nullptr;
    QLabel      *m_title = nullptr, *m_artist = nullptr;
    QFrame      *m_aboutCard = nullptr, *m_sourceCard = nullptr, *m_nextCard = nullptr;
    QVBoxLayout *m_aboutRows = nullptr, *m_sourceRows = nullptr;
    QHBoxLayout* m_btnRow = nullptr;
    QPushButton *m_btnA = nullptr, *m_btnB = nullptr;
    std::function<void()> m_actionA, m_actionB;
    CoverView*   m_nextCover = nullptr;
    ElidedLabel *m_nextTitle = nullptr, *m_nextArtist = nullptr;
    QString      m_key;
};


// ============================================================================
//  SlideHost: holds the InfoPanel and slides it in from the right edge.
//  The host's width is animated (so the pages next to it glide aside) while the
//  panel keeps its full width and is simply revealed, so its content never reflows.
// ============================================================================
class SlideHost : public QWidget
{
    Q_OBJECT
public:
    SlideHost(QWidget* inner, int fullWidth, QWidget* parent = nullptr)
        : QWidget(parent), m_inner(inner), m_full(fullWidth)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        m_inner->setParent(this);
        m_inner->setFixedWidth(m_full);
        m_inner->show();
        setFixedWidth(0);
        hide();
        connect(&m_t, &Tween::changed, this, [this] { apply(); });
    }

    bool isOpen() const { return m_t.target() > 0.5; }

    void setOpen(bool on)
    {
        if (on == isOpen() && (on ? isVisible() : !isVisible())) return;
        if (on) {
            show();
            m_t.to(1.0, 420, QEasingCurve::OutCubic);
        } else {
            m_t.to(0.0, 320, QEasingCurve::InOutCubic);
        }
        emit openChanged(on);
    }
    void toggle() { setOpen(!isOpen()); }

signals:
    void openChanged(bool open);

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        apply();
    }

private:
    void apply()
    {
        const int w = int(std::round(m_full * qBound(0.0, m_t.value(), 1.0)));
        if (w != width()) setFixedWidth(w);
        m_inner->setGeometry(w - m_full, 0, m_full, height());      // slides in from the right edge
        if (w <= 0 && m_t.target() == 0.0 && isVisible()) hide();
    }

    QWidget* m_inner;
    int      m_full;
    Tween    m_t{this, 0.0};
};
