#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <memory>
#include "widgets.h"
#include "library.h"
#include "online.h"
#include "ui.h"
#include "pages.h"

// ============================================================================
//  ImageCache: downloads cover / artist / avatar pictures (disk cached)
// ============================================================================
class ImageCache : public QObject
{
    Q_OBJECT
public:
    explicit ImageCache(QObject* parent = nullptr) : QObject(parent)
    {
        m_nam = new QNetworkAccessManager(this);
        auto* cache = new QNetworkDiskCache(this);
        cache->setCacheDirectory(Yt::cacheDir(QStringLiteral("HomeShelf")));
        cache->setMaximumCacheSize(150LL * 1024 * 1024);
        m_nam->setCache(cache);
    }

    void request(const QString& url)
    {
        if (url.isEmpty()) return;
        const auto it = m_done.constFind(url);
        if (it != m_done.constEnd()) {
            const QPixmap pm = it.value();
            QTimer::singleShot(0, this, [this, url, pm] { emit ready(url, pm); });
            return;
        }
        if (m_pending.contains(url)) return;
        m_pending.insert(url);
        QNetworkRequest req{ QUrl(url) };
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(15000);
        QNetworkReply* rep = m_nam->get(req);
        connect(rep, &QNetworkReply::finished, this, [this, rep, url] {
            rep->deleteLater();
            m_pending.remove(url);
            QImage img;
            if (rep->error() == QNetworkReply::NoError) img.loadFromData(rep->readAll());
            if (img.isNull()) return;
            if (img.width() > 360 || img.height() > 360)
                img = img.scaled(360, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPixmap pm = QPixmap::fromImage(img);
            m_done.insert(url, pm);
            emit ready(url, pm);
        });
    }

signals:
    void ready(const QString& url, const QPixmap& pm);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QSet<QString>          m_pending;
    QHash<QString, QPixmap> m_done;
};

// ============================================================================
//  ChartService: trending songs / popular artists / albums (Deezer's public chart API,
//  no key needed). Songs are returned as "pending" tracks: Pulse finds the matching
//  YouTube audio when you press play, exactly like imported Spotify playlists.
// ============================================================================
struct ChartItem {
    QString id, title, sub, image;
};

inline Track trackFromDeezer(const QJsonObject& o, const QString& albumFallback = QString(),
                             const QString& coverFallback = QString())
{
    Track t;
    QString title = jstr(o, "title_short");
    if (title.trimmed().isEmpty()) title = jstr(o, "title");
    t.title = title.trimmed();
    t.artist = jstr(jobj(o, "artist"), "name").trimmed();
    const QJsonObject al = jobj(o, "album");
    t.album = jstr(al, "title");
    if (t.album.isEmpty()) t.album = albumFallback;
    QString cover = jstr(al, "cover_big");
    if (cover.isEmpty()) cover = jstr(al, "cover_medium");
    if (cover.isEmpty()) cover = coverFallback;
    t.thumbUrl = cover;
    t.durationMs = qint64(jnum(o, "duration") * 1000.0);
    t.query = (t.artist + QLatin1Char(' ') + t.title).trimmed();
    t.locked = true;
    return t;
}

class ChartService : public QObject
{
    Q_OBJECT
public:
    explicit ChartService(QObject* parent = nullptr) : QObject(parent) {}

    void loadHome()
    {
        getJson(QStringLiteral("https://api.deezer.com/chart/0/tracks?limit=50"),
                [this](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { emit failed(err); return; }
            QVector<Track> v;
            const QJsonArray a = jarr(o, "data");
            for (const QJsonValue& x : a) {
                const Track t = trackFromDeezer(x.toObject());
                if (!t.title.isEmpty()) v.push_back(t);
            }
            emit tracksReady(v);
        });
        getJson(QStringLiteral("https://api.deezer.com/chart/0/artists?limit=30"),
                [this](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { emit failed(err); return; }
            QVector<ChartItem> v;
            const QJsonArray a = jarr(o, "data");
            for (const QJsonValue& x : a) {
                const QJsonObject it = x.toObject();
                ChartItem c;
                c.id = QString::number(qint64(jnum(it, "id")));
                c.title = jstr(it, "name");
                c.sub = QStringLiteral("Artist");
                c.image = jstr(it, "picture_big");
                if (c.image.isEmpty()) c.image = jstr(it, "picture_medium");
                if (!c.title.isEmpty() && c.id != QLatin1String("0")) v.push_back(c);
            }
            emit artistsReady(v);
        });
        getJson(QStringLiteral("https://api.deezer.com/chart/0/albums?limit=30"),
                [this](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { emit failed(err); return; }
            QVector<ChartItem> v;
            const QJsonArray a = jarr(o, "data");
            for (const QJsonValue& x : a) {
                const QJsonObject it = x.toObject();
                ChartItem c;
                c.id = QString::number(qint64(jnum(it, "id")));
                c.title = jstr(it, "title");
                c.sub = jstr(jobj(it, "artist"), "name");
                c.image = jstr(it, "cover_big");
                if (c.image.isEmpty()) c.image = jstr(it, "cover_medium");
                if (!c.title.isEmpty() && c.id != QLatin1String("0")) v.push_back(c);
            }
            emit albumsReady(v);
        });
    }

    void loadArtistTop(int token, const QString& id)
    {
        getJson(QStringLiteral("https://api.deezer.com/artist/%1/top?limit=50").arg(id),
                [this, token](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { emit collectionFailed(token, err); return; }
            QVector<Track> v;
            const QJsonArray a = jarr(o, "data");
            for (const QJsonValue& x : a) {
                const Track t = trackFromDeezer(x.toObject());
                if (!t.title.isEmpty()) v.push_back(t);
            }
            emit collectionReady(token, v);
        });
    }

    void loadAlbum(int token, const QString& id, const QString& albumTitle, const QString& cover)
    {
        getJson(QStringLiteral("https://api.deezer.com/album/%1/tracks?limit=100").arg(id),
                [this, token, albumTitle, cover](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { emit collectionFailed(token, err); return; }
            QVector<Track> v;
            const QJsonArray a = jarr(o, "data");
            for (const QJsonValue& x : a) {
                const Track t = trackFromDeezer(x.toObject(), albumTitle, cover);   // album tracks carry no album info
                if (!t.title.isEmpty()) v.push_back(t);
            }
            emit collectionReady(token, v);
        });
    }

signals:
    void tracksReady(const QVector<Track>& tracks);
    void artistsReady(const QVector<ChartItem>& items);
    void albumsReady(const QVector<ChartItem>& items);
    void failed(const QString& message);
    void collectionReady(int token, const QVector<Track>& tracks);
    void collectionFailed(int token, const QString& message);

private:
    using JsonCb = std::function<void(const QJsonObject& o, const QString& err)>;

    void getJson(const QString& url, const JsonCb& cb)
    {
        QNetworkRequest req{ QUrl(url) };
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Pulse/1.2"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(15000);
        QNetworkReply* rep = m_nam.get(req);
        connect(rep, &QNetworkReply::finished, this, [rep, cb] {
            rep->deleteLater();
            if (rep->error() != QNetworkReply::NoError) { cb(QJsonObject(), rep->errorString()); return; }
            const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
            if (o.contains(QLatin1String("error"))) {
                QString m = jstr(jobj(o, "error"), "message");
                if (m.isEmpty()) m = QStringLiteral("The chart service returned an error");
                cb(o, m);
                return;
            }
            cb(o, QString());
        });
    }

    QNetworkAccessManager m_nam;
};

// ============================================================================
//  Small helpers
// ============================================================================
// Greedy word wrap into at most `maxLines` lines; the last line is elided.
inline QStringList wrapLines(const QFontMetrics& fm, const QString& text, int width, int maxLines)
{
    QStringList out;
    QString rest = text.simplified();
    while (!rest.isEmpty() && out.size() < maxLines) {
        if (out.size() == maxLines - 1) { out << fm.elidedText(rest, Qt::ElideRight, width); break; }
        if (fm.horizontalAdvance(rest) <= width) { out << rest; break; }
        int cut = int(rest.size());
        while (cut > 1 && fm.horizontalAdvance(rest.left(cut)) > width) --cut;
        int sp = int(rest.lastIndexOf(QLatin1Char(' '), cut));
        if (sp <= 0) sp = qMax(1, cut);
        out << rest.left(sp).trimmed();
        rest = rest.mid(sp).trimmed();
    }
    return out;
}

// Wheel scrolling with the same easing as the track lists.
class HomeSmoothScrollArea : public QScrollArea
{
public:
    explicit HomeSmoothScrollArea(QWidget* parent = nullptr) : QScrollArea(parent)
    {
        m_anim = new QPropertyAnimation(verticalScrollBar(), "value", this);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        QScrollBar* sb = verticalScrollBar();
        const int delta = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->angleDelta().y();
        if (delta == 0) { e->ignore(); return; }
        const int base = (m_anim->state() == QAbstractAnimation::Running) ? m_anim->endValue().toInt() : sb->value();
        const int target = qBound(sb->minimum(), base - delta, sb->maximum());
        m_anim->stop();
        m_anim->setStartValue(sb->value());
        m_anim->setEndValue(target);
        m_anim->setDuration(420);
        m_anim->start();
        e->accept();
    }

private:
    QPropertyAnimation* m_anim = nullptr;
};

// ============================================================================
//  Card: cover (square or round), title, subtitle; green play button on hover
// ============================================================================
class Card : public QWidget
{
    Q_OBJECT
public:
    static int heightFor(int width) { return 12 + (width - 24) + 14 + 22 + 40 + 12; }

    Card(const QString& title, const QString& sub, bool round, bool playButton, QWidget* parent = nullptr)
        : QWidget(parent), m_title(title), m_sub(sub), m_round(round), m_play(playButton)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        bindUpdate(this, { &m_hover, &m_hot });
    }

    void setImageUrl(const QString& u) { m_url = u; }
    QString imageUrl() const           { return m_url; }
    void setPixmap(const QPixmap& pm)  { m_pm = pm; update(); }

signals:
    void clicked();
    void playClicked();
    void contextRequested(const QPoint& globalPos);

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 160); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 240); m_hot.to(0.0, 120); }
    void mouseMoveEvent(QMouseEvent* e) override { m_hot.to(overPlay(e->position()) ? 1.0 : 0.0, 120); }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_down = true;
        m_downPlay = overPlay(e->position());
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !m_down) return;
        m_down = false;
        if (!rect().contains(e->position().toPoint())) return;
        if (m_downPlay && overPlay(e->position())) emit playClicked();
        else emit clicked();
    }
    void contextMenuEvent(QContextMenuEvent* e) override { emit contextRequested(e->globalPos()); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const qreal hv = m_hover.value();
        if (hv > 0.002) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, int(24 * hv)));
            p.drawRoundedRect(QRectF(rect()), 8, 8);
        }
        const QRectF img = imageRect();
        drawCover(&p, img, m_pm, m_round ? img.width() / 2 : 6);

        if (m_play && hv > 0.01) {
            p.save();
            p.setOpacity(hv);
            QPointF c = playCenter();
            c.setY(c.y() + 8.0 * (1.0 - hv));
            const qreal r = 24.0 * (1.0 + 0.06 * m_hot.value());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 90));
            p.drawEllipse(c + QPointF(0, 3), r, r);
            p.setBrush(Theme::accent());
            p.drawEllipse(c, r, r);
            p.setBrush(QColor(10, 10, 10));
            QPolygonF tri;
            tri << c + QPointF(-6, -9) << c + QPointF(9, 0) << c + QPointF(-6, 9);
            p.drawPolygon(tri);
            p.restore();
        }

        const int tw = width() - 24;
        const QFont tf = Theme::font(16, QFont::Bold);
        const QFont sf = Theme::font(14);
        const QFontMetrics tfm(tf), sfm(sf);
        qreal y = img.bottom() + 14;
        p.setFont(tf);
        p.setPen(Qt::white);
        p.drawText(QRectF(12, y, tw, 22), int(Qt::AlignLeft | Qt::AlignVCenter),
                   tfm.elidedText(m_title, Qt::ElideRight, tw));
        y += 22;
        p.setFont(sf);
        p.setPen(Theme::subtext());
        const QStringList lines = wrapLines(sfm, m_sub, tw, 2);
        for (const QString& l : lines) {
            p.drawText(QRectF(12, y, tw, 20), int(Qt::AlignLeft | Qt::AlignVCenter), l);
            y += 20;
        }
    }

private:
    QRectF imageRect() const  { const qreal side = width() - 24; return QRectF(12, 12, side, side); }
    QPointF playCenter() const
    {
        const QRectF r = imageRect();
        return QPointF(r.right() - 8 - 24, r.bottom() - 8 - 24);
    }
    bool overPlay(const QPointF& pt) const
    {
        return m_play && QLineF(pt, playCenter()).length() <= 26.0;
    }

    QString m_title, m_sub, m_url;
    bool    m_round, m_play;
    bool    m_down = false, m_downPlay = false;
    QPixmap m_pm;
    Tween   m_hover{this};
    Tween   m_hot{this};
};

// ============================================================================
//  CardGrid: one row of cards that stretch to fill the width; "Show all" expands it
// ============================================================================
class CardGrid : public QWidget
{
    Q_OBJECT
public:
    explicit CardGrid(QWidget* parent = nullptr) : QWidget(parent) {}

    void setCards(const QVector<Card*>& cards)
    {
        qDeleteAll(m_cards);
        m_cards.clear();
        for (Card* c : cards) {
            c->setParent(this);
            c->show();
            m_cards.push_back(c);
        }
        relayout();
    }

    const QVector<Card*>& cards() const { return m_cards; }
    bool expanded() const               { return m_expanded; }
    bool canExpand() const              { return int(m_cards.size()) > columns(); }
    void setExpanded(bool e)            { m_expanded = e; relayout(); }
    int  columns() const                { return qMax(1, width() / 190); }

signals:
    void layoutChanged();

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        relayout();
    }

private:
    void relayout()
    {
        const int w = width();
        if (w <= 0) return;
        const int cols = columns();
        const int cw = w / cols;
        const int ch = Card::heightFor(cw);
        const int n = int(m_cards.size());
        const int rows = m_expanded ? (n + cols - 1) / cols : (n > 0 ? 1 : 0);
        for (int i = 0; i < n; ++i) {
            Card* c = m_cards[i];
            const int r = i / cols, col = i % cols;
            if (r >= rows) { c->hide(); continue; }
            c->setGeometry(col * cw, r * ch, cw, ch);
            c->show();
        }
        if (height() != rows * ch) setFixedHeight(rows * ch);
        emit layoutChanged();
    }

    QVector<Card*> m_cards;
    bool           m_expanded = false;
};

// ============================================================================
//  HomeShelf: "Trending songs  ......  Show all" + a grid of cards
// ============================================================================
struct ShelfItem {
    QString id, title, sub, image;
};

class HomeShelf : public QWidget
{
    Q_OBJECT
public:
    HomeShelf(const QString& title, bool round, bool playButton, ImageCache* img, QWidget* parent = nullptr)
        : QWidget(parent), m_round(round), m_playButton(playButton), m_img(img)
    {
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);

        auto* head = new QHBoxLayout;
        head->setContentsMargins(12, 0, 12, 0);
        auto* t = new QLabel(title);
        t->setStyleSheet(QStringLiteral("font-size:24px;font-weight:800;color:white;background:transparent;"));
        m_more = new QPushButton(QStringLiteral("Show all"));
        m_more->setCursor(Qt::PointingHandCursor);
        m_more->setFocusPolicy(Qt::NoFocus);
        m_more->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;color:#b3b3b3;font-size:14px;font-weight:700;}"
            "QPushButton:hover{color:white;text-decoration:underline;}"));
        head->addWidget(t);
        head->addStretch(1);
        head->addWidget(m_more);
        v->addLayout(head);

        m_grid = new CardGrid;
        v->addWidget(m_grid);

        connect(m_more, &QAbstractButton::clicked, this, [this] {
            m_grid->setExpanded(!m_grid->expanded());
            m_more->setText(m_grid->expanded() ? QStringLiteral("Show less") : QStringLiteral("Show all"));
        });
        connect(m_grid, &CardGrid::layoutChanged, this, [this] { m_more->setVisible(m_grid->canExpand()); });
        connect(img, &ImageCache::ready, this, [this](const QString& url, const QPixmap& pm) {
            for (Card* c : m_grid->cards())
                if (c->imageUrl() == url) c->setPixmap(pm);
        });
        m_more->hide();
        hide();
    }

    void setItems(const QVector<ShelfItem>& items)
    {
        QVector<Card*> cards;
        for (int i = 0; i < items.size(); ++i) {
            auto* c = new Card(items[i].title, items[i].sub, m_round, m_playButton);
            c->setImageUrl(items[i].image);
            connect(c, &Card::clicked, this, [this, i] { emit itemClicked(i); });
            connect(c, &Card::playClicked, this, [this, i] { emit itemPlay(i); });
            connect(c, &Card::contextRequested, this, [this, i](const QPoint& gp) { emit itemContext(i, gp); });
            cards.push_back(c);
        }
        m_grid->setCards(cards);
        for (const ShelfItem& it : items) m_img->request(it.image);
        m_more->setVisible(m_grid->canExpand());
        setVisible(!items.isEmpty());
    }

signals:
    void itemClicked(int index);
    void itemPlay(int index);
    void itemContext(int index, const QPoint& globalPos);

private:
    bool         m_round, m_playButton;
    ImageCache*  m_img;
    CardGrid*    m_grid = nullptr;
    QPushButton* m_more = nullptr;
};

// ============================================================================
//  HomePage: the Online landing page (shelves of trending music)
// ============================================================================
class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(ImageCache* img, QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        m_scroll = new HomeSmoothScrollArea;
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"));
        m_scroll->viewport()->setAutoFillBackground(false);
        outer->addWidget(m_scroll);

        auto* content = new QWidget;
        content->setAutoFillBackground(false);
        m_scroll->setWidget(content);
        auto* v = new QVBoxLayout(content);
        v->setContentsMargins(16, 20, 16, 28);
        v->setSpacing(18);

        // ---- "log in" banner (hidden once signed in)
        m_banner = new QFrame;
        m_banner->setObjectName(QStringLiteral("loginBanner"));
        m_banner->setStyleSheet(QStringLiteral("QFrame#loginBanner{background:#1f1f1f;border-radius:10px;}"));
        auto* bl = new QHBoxLayout(m_banner);
        bl->setContentsMargins(24, 16, 14, 16);
        bl->setSpacing(14);
        auto* bt = new QVBoxLayout;
        bt->setSpacing(2);
        auto* bh = new QLabel(QStringLiteral("Bring your YouTube playlists"));
        bh->setStyleSheet(QStringLiteral("font-size:17px;font-weight:700;color:white;background:transparent;"));
        auto* bs = new QLabel(QStringLiteral("Log in with Google and pick any of your playlists \u2013 private ones and Liked videos included."));
        bs->setWordWrap(true);
        bs->setStyleSheet(QStringLiteral("font-size:14px;color:#b3b3b3;background:transparent;"));
        bt->addWidget(bh);
        bt->addWidget(bs);
        auto* bLogin = new QPushButton(QStringLiteral("Log in with Google"));
        bLogin->setCursor(Qt::PointingHandCursor);
        bLogin->setFocusPolicy(Qt::NoFocus);
        bLogin->setFixedHeight(38);
        bLogin->setStyleSheet(QStringLiteral(
            "QPushButton{background:white;color:black;border:none;border-radius:19px;padding:0 22px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#f2f2f2;}QPushButton:pressed{background:#dcdcdc;}"));
        auto* bClose = new QPushButton(QStringLiteral("\u2715"));
        bClose->setCursor(Qt::PointingHandCursor);
        bClose->setFocusPolicy(Qt::NoFocus);
        bClose->setFixedSize(32, 32);
        bClose->setToolTip(QStringLiteral("Hide"));
        bClose->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;color:#b3b3b3;border:none;border-radius:16px;font-size:14px;}"
            "QPushButton:hover{color:white;background:rgba(255,255,255,0.1);}"));
        bl->addLayout(bt, 1);
        bl->addWidget(bLogin);
        bl->addWidget(bClose);
        v->addWidget(m_banner);

        // ---- shelves
        m_yt      = new HomeShelf(QStringLiteral("Your YouTube playlists"), false, false, img);
        m_songs   = new HomeShelf(QStringLiteral("Trending songs"), false, true, img);
        m_artists = new HomeShelf(QStringLiteral("Popular artists"), true, true, img);
        m_albums  = new HomeShelf(QStringLiteral("Popular albums"), false, true, img);
        v->addWidget(m_yt);
        v->addWidget(m_songs);
        v->addWidget(m_artists);
        v->addWidget(m_albums);

        m_status = new QLabel;
        m_status->setAlignment(Qt::AlignCenter);
        m_status->setWordWrap(true);
        m_status->setStyleSheet(QStringLiteral("font-size:15px;font-weight:500;color:rgba(255,255,255,0.6);background:transparent;padding:40px 20px 6px 20px;"));
        m_retry = makePill(QStringLiteral("Try again"));
        m_retry->hide();
        auto* retryRow = new QHBoxLayout;
        retryRow->addStretch(1);
        retryRow->addWidget(m_retry);
        retryRow->addStretch(1);
        v->addWidget(m_status);
        v->addLayout(retryRow);

        auto* credit = new QLabel(QStringLiteral("Charts by Deezer \u00b7 audio from YouTube \u00b7 lyrics from Genius, timing from LRCLIB"));
        credit->setStyleSheet(QStringLiteral("font-size:12px;color:rgba(255,255,255,0.35);background:transparent;padding:18px 12px 0 12px;"));
        v->addWidget(credit);
        v->addStretch(1);

        connect(bLogin, &QAbstractButton::clicked, this, &HomePage::loginRequested);
        connect(bClose, &QAbstractButton::clicked, this, [this] {
            QSettings().setValue(QStringLiteral("ui/hideLoginBanner"), true);
            updateBanner();
        });
        connect(m_retry, &QAbstractButton::clicked, this, &HomePage::retryRequested);
        connect(m_songs, &HomeShelf::itemClicked, this, &HomePage::songClicked);
        connect(m_songs, &HomeShelf::itemPlay, this, &HomePage::songClicked);
        connect(m_songs, &HomeShelf::itemContext, this, &HomePage::songContext);
        connect(m_artists, &HomeShelf::itemClicked, this, &HomePage::artistClicked);
        connect(m_artists, &HomeShelf::itemPlay, this, &HomePage::artistPlay);
        connect(m_albums, &HomeShelf::itemClicked, this, &HomePage::albumClicked);
        connect(m_albums, &HomeShelf::itemPlay, this, &HomePage::albumPlay);
        connect(m_yt, &HomeShelf::itemClicked, this, &HomePage::playlistClicked);

        setStatus(QStringLiteral("Loading\u2026"), false);
        updateBanner();
    }

    void setSongs(const QVector<Track>& tracks)
    {
        QVector<ShelfItem> items;
        for (const Track& t : tracks) {
            ShelfItem it;
            it.title = t.title;
            it.sub = t.artist;
            it.image = t.thumbUrl;
            items.push_back(it);
        }
        m_songs->setItems(items);
        clearStatusIfAny();
    }

    void setArtists(const QVector<ChartItem>& list) { m_artists->setItems(toShelf(list)); clearStatusIfAny(); }
    void setAlbums(const QVector<ChartItem>& list)  { m_albums->setItems(toShelf(list));  clearStatusIfAny(); }

    void setYouTube(const QVector<ShelfItem>& items) { m_yt->setItems(items); }

    void setSignedIn(bool in)
    {
        m_signedIn = in;
        if (!in) m_yt->setItems(QVector<ShelfItem>());
        updateBanner();
    }

    void setStatus(const QString& text, bool retry)
    {
        m_status->setText(text);
        m_status->setVisible(!text.isEmpty());
        m_retry->setVisible(retry);
    }

signals:
    void songClicked(int index);
    void songContext(int index, const QPoint& globalPos);
    void artistClicked(int index);
    void artistPlay(int index);
    void albumClicked(int index);
    void albumPlay(int index);
    void playlistClicked(int index);
    void loginRequested();
    void retryRequested();

private:
    static QVector<ShelfItem> toShelf(const QVector<ChartItem>& list)
    {
        QVector<ShelfItem> v;
        for (const ChartItem& c : list) {
            ShelfItem it;
            it.id = c.id;
            it.title = c.title;
            it.sub = c.sub;
            it.image = c.image;
            v.push_back(it);
        }
        return v;
    }

    void clearStatusIfAny() { setStatus(QString(), false); }

    void updateBanner()
    {
        const bool hidden = QSettings().value(QStringLiteral("ui/hideLoginBanner"), false).toBool();
        m_banner->setVisible(!m_signedIn && !hidden);
    }

    HomeSmoothScrollArea* m_scroll = nullptr;
    QFrame*           m_banner = nullptr;
    HomeShelf             *m_yt = nullptr, *m_songs = nullptr, *m_artists = nullptr, *m_albums = nullptr;
    QLabel*           m_status = nullptr;
    QPushButton*      m_retry = nullptr;
    bool              m_signedIn = false;
};

// ============================================================================
//  TopBar: logo, Home, search pill, links, Google account
// ============================================================================
class SearchField : public QLineEdit
{
    Q_OBJECT
public:
    explicit SearchField(QWidget* parent = nullptr) : QLineEdit(parent)
    {
        setFixedHeight(48);
        setPlaceholderText(QStringLiteral("What do you want to play?"));
        setStyleSheet(QStringLiteral(
            "QLineEdit{background:#1f1f1f;border:2px solid transparent;border-radius:24px;"
            "padding-left:48px;padding-right:62px;color:white;font-size:15px;selection-background-color:#1DB954;}"
            "QLineEdit:hover{background:#2a2a2a;border:2px solid #3d3d3d;}"
            "QLineEdit:focus{background:#1f1f1f;border:2px solid white;}"));
        m_browse = new QPushButton(QStringLiteral("\U0001F4C2"), this);
        m_browse->setFixedSize(40, 40);
        m_browse->setCursor(Qt::PointingHandCursor);
        m_browse->setFocusPolicy(Qt::NoFocus);
        m_browse->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;color:#b3b3b3;font-size:16px;border-radius:20px;}"
            "QPushButton:hover{color:white;background:rgba(255,255,255,0.10);}"));
        m_browse->setToolTip(QStringLiteral("Offline library"));
        connect(m_browse, &QAbstractButton::clicked, this, &SearchField::browseClicked);
    }

signals:
    void browseClicked();

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QLineEdit::resizeEvent(e);
        m_browse->move(width() - 50, (height() - 40) / 2);
    }

    void paintEvent(QPaintEvent* e) override
    {
        QLineEdit::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal cy = height() / 2.0;
        p.setPen(QPen(hasFocus() ? QColor(Qt::white) : Theme::subtext(), 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(19, cy - 9, 13, 13));
        p.drawLine(QPointF(30, cy + 3), QPointF(35.5, cy + 8.5));
        p.setPen(QPen(QColor(255, 255, 255, 55), 1.0));
        p.drawLine(QPointF(width() - 57, 14), QPointF(width() - 57, height() - 14));
    }

private:
    QPushButton* m_browse = nullptr;
};

class HomeAvatarButton : public QAbstractButton
{
public:
    explicit HomeAvatarButton(QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setFixedSize(46, 46);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        bindUpdate(this, { &m_hover });
    }

    void setAccount(const QString& name, const QPixmap& pm)
    {
        m_name = name;
        m_pm = pm;
        update();
    }

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 150); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 250); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(40 * m_hover.value() + 18)));
        p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -6);
        if (!m_pm.isNull()) {
            drawCover(&p, r, m_pm, r.width() / 2);
        } else {
            p.setBrush(Theme::accent());
            p.drawEllipse(r);
            p.setPen(QColor(10, 10, 10));
            p.setFont(Theme::font(17, QFont::Bold));
            const QString initial = m_name.trimmed().left(1).toUpper();
            p.drawText(r, Qt::AlignCenter, initial.isEmpty() ? QStringLiteral("G") : initial);
        }
    }

private:
    QString m_name;
    QPixmap m_pm;
    Tween   m_hover{this};
};

class TopBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(68);
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(18, 10, 18, 6);
        h->setSpacing(10);

        auto* logo = new QLabel(QStringLiteral("<span style='color:#1DB954'>&#9679;</span>&nbsp;Pulse"));
        logo->setTextFormat(Qt::RichText);
        logo->setStyleSheet(QStringLiteral("font-size:26px;font-weight:800;color:white;background:transparent;"));
        logo->setFixedWidth(124);

        m_home = new IconButton(IconButton::Home, 48);
        m_home->setCircle(QColor(0x1f, 0x1f, 0x1f), Qt::white);
        m_home->setToolTip(QStringLiteral("Home"));

        m_search = new SearchField;
        m_search->setMinimumWidth(300);
        m_search->setMaximumWidth(520);
        m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* importLink = makeLink(QStringLiteral("Import"));
        auto* settingsLink = makeLink(QStringLiteral("Settings"));

        auto* divider = new QFrame;
        divider->setFixedSize(1, 26);
        divider->setStyleSheet(QStringLiteral("background:#5a5a5a;border:none;"));

        m_login = new QPushButton(QStringLiteral("Log in with Google"));
        m_login->setCursor(Qt::PointingHandCursor);
        m_login->setFocusPolicy(Qt::NoFocus);
        m_login->setFixedHeight(48);
        m_login->setToolTip(QStringLiteral("Sign in with Google to get your YouTube playlists"));
        m_login->setStyleSheet(QStringLiteral(
            "QPushButton{background:white;color:black;border:none;border-radius:24px;padding:0 28px;font-size:15px;font-weight:700;}"
            "QPushButton:hover{background:#f6f6f6;}QPushButton:pressed{background:#dcdcdc;}"
            "QPushButton:disabled{background:#b8b8b8;color:#333;}"));

        m_avatar = new HomeAvatarButton;
        m_avatar->hide();

        h->addWidget(logo);
        h->addWidget(m_home);
        h->addWidget(m_search, 1);
        h->addStretch(1);
        h->addWidget(importLink);
        h->addWidget(settingsLink);
        h->addSpacing(6);
        h->addWidget(divider);
        h->addSpacing(6);
        h->addWidget(m_login);
        h->addWidget(m_avatar);

        connect(m_home, &QAbstractButton::clicked, this, &TopBar::homeClicked);
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            emit searchSubmitted(m_search->text().trimmed());
        });
        connect(m_search, &SearchField::browseClicked, this, &TopBar::offlineClicked);
        connect(importLink, &QAbstractButton::clicked, this, &TopBar::importClicked);
        connect(settingsLink, &QAbstractButton::clicked, this, &TopBar::settingsClicked);
        connect(m_login, &QAbstractButton::clicked, this, &TopBar::loginClicked);
        connect(m_avatar, &QAbstractButton::clicked, this, [this] {
            emit accountClicked(m_avatar->mapToGlobal(QPoint(m_avatar->width(), m_avatar->height() + 6)));
        });
    }

    QLineEdit* searchBox() const { return m_search; }

    void setAccount(bool signedIn, const QString& name, const QPixmap& avatar)
    {
        m_login->setVisible(!signedIn);
        m_avatar->setVisible(signedIn);
        m_avatar->setAccount(name, avatar);
        m_avatar->setToolTip(name);
    }

    void setLoginBusy(bool busy)
    {
        m_login->setText(busy ? QStringLiteral("Cancel sign-in") : QStringLiteral("Log in with Google"));
    }

signals:
    void homeClicked();
    void searchSubmitted(const QString& text);
    void offlineClicked();
    void importClicked();
    void settingsClicked();
    void loginClicked();
    void accountClicked(const QPoint& globalPos);

private:
    static QPushButton* makeLink(const QString& text)
    {
        auto* b = new QPushButton(text);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFlat(true);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;color:#b3b3b3;font-size:14px;font-weight:700;padding:8px 8px;}"
            "QPushButton:hover{color:white;}"));
        return b;
    }

    IconButton*   m_home = nullptr;
    SearchField*  m_search = nullptr;
    QPushButton*  m_login = nullptr;
    HomeAvatarButton* m_avatar = nullptr;
};

// ============================================================================
//  LibraryPanel: "Your Library" with the playlists (or the two starter cards)
// ============================================================================
class LibraryDelegate : public QStyledItemDelegate
{
public:
    explicit LibraryDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setSelectedRow(int r) { m_sel = r; }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(100, 64); }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& ix) const override
    {
        p->save();
        p->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(opt.rect).adjusted(2, 2, -2, -2);
        const bool sel = (ix.row() == m_sel);
        if (sel || (opt.state & QStyle::State_MouseOver)) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 255, sel ? 30 : 18));
            p->drawRoundedRect(r, 8, 8);
        }
        const QRectF cr(r.left() + 8, r.center().y() - 24, 48, 48);
        drawCover(p, cr, ix.data(PlaylistListModel::CoverRole2).value<QPixmap>(), 5);

        const qreal tx = cr.right() + 12;
        const int tw = int(qMax<qreal>(10, r.right() - 8 - tx));
        const QFont tf = Theme::font(15, QFont::Medium);
        const QFont sf = Theme::font(13);
        p->setFont(tf);
        p->setPen(sel ? Theme::accent() : QColor(Qt::white));
        p->drawText(QRectF(tx, r.top() + 10, tw, 22), int(Qt::AlignLeft | Qt::AlignVCenter),
                    QFontMetrics(tf).elidedText(ix.data().toString(), Qt::ElideRight, tw));
        const int n = ix.data(PlaylistListModel::CountRole).toInt();
        const QString sub = QStringLiteral("Playlist \u00b7 %1 song%2").arg(n).arg(n == 1 ? "" : "s");
        p->setFont(sf);
        p->setPen(Theme::subtext());
        p->drawText(QRectF(tx, r.top() + 32, tw, 20), int(Qt::AlignLeft | Qt::AlignVCenter),
                    QFontMetrics(sf).elidedText(sub, Qt::ElideRight, tw));
        p->restore();
    }

private:
    int m_sel = -1;
};

class LibraryPanel : public TintWidget
{
    Q_OBJECT
public:
    explicit LibraryPanel(PlaylistStore* store, QWidget* parent = nullptr)
        : TintWidget(0.0, 0, 12, parent), m_store(store)
    {
        setFixedWidth(320);
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(12, 18, 12, 12);
        v->setSpacing(10);

        auto* head = new QHBoxLayout;
        head->setContentsMargins(12, 0, 0, 0);
        auto* title = new QLabel(QStringLiteral("Your Library"));
        title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:white;background:transparent;"));
        m_create = new QPushButton(QStringLiteral("+   Create"));
        m_create->setCursor(Qt::PointingHandCursor);
        m_create->setFocusPolicy(Qt::NoFocus);
        m_create->setFixedHeight(40);
        m_create->setStyleSheet(QStringLiteral(
            "QPushButton{background:#1f1f1f;color:white;border:none;border-radius:20px;padding:0 18px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#2c2c2c;}QPushButton:pressed{background:#161616;}"));
        head->addWidget(title);
        head->addStretch(1);
        head->addWidget(m_create);
        v->addLayout(head);

        m_model = new PlaylistListModel(store, this);
        m_list = new QListView;
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setStyleSheet(QStringLiteral("QListView { background: transparent; border: none; outline: none; }"));
        m_list->viewport()->setAutoFillBackground(false);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_list->setMouseTracking(true);
        m_list->setCursor(Qt::PointingHandCursor);
        m_list->setModel(m_model);
        m_delegate = new LibraryDelegate(m_list);
        m_list->setItemDelegate(m_delegate);
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        v->addWidget(m_list, 1);

        m_empty = new QWidget;
        auto* el = new QVBoxLayout(m_empty);
        el->setContentsMargins(0, 6, 0, 0);
        el->setSpacing(12);
        QPushButton *createBtn = nullptr, *importBtn = nullptr;
        el->addWidget(infoCard(QStringLiteral("Create your first playlist"),
                               QStringLiteral("It\u2019s easy, we\u2019ll help you"),
                               QStringLiteral("Create playlist"), &createBtn));
        el->addWidget(infoCard(QStringLiteral("Import a playlist"),
                               QStringLiteral("Bring your playlists over from YouTube, Spotify and more"),
                               QStringLiteral("Import playlist"), &importBtn));
        el->addStretch(1);
        v->addWidget(m_empty, 1);

        m_note = new QLabel;
        m_note->setWordWrap(true);
        m_note->setStyleSheet(QStringLiteral("color:#1DB954;font-size:12px;padding:0 12px 2px 12px;background:transparent;"));
        v->addWidget(m_note);

        m_offline = new NavButton(NavButton::Library, QStringLiteral("Offline library"));
        v->addWidget(m_offline);

        connect(createBtn, &QAbstractButton::clicked, this, &LibraryPanel::newRequested);
        connect(importBtn, &QAbstractButton::clicked, this, &LibraryPanel::importRequested);
        connect(m_create, &QAbstractButton::clicked, this, [this] {
            QMenu menu(this);
            QAction* np = menu.addAction(QStringLiteral("New playlist"));
            QAction* ip = menu.addAction(QStringLiteral("Import playlist\u2026"));
            QAction* a = menu.exec(m_create->mapToGlobal(QPoint(0, m_create->height() + 4)));
            if (a == np) emit newRequested();
            else if (a == ip) emit importRequested();
        });
        connect(m_list, &QAbstractItemView::clicked, this, [this](const QModelIndex& ix) { emit openRequested(ix.row()); });
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
            const QModelIndex ix = m_list->indexAt(pt);
            if (ix.isValid()) emit contextRequested(ix.row(), m_list->viewport()->mapToGlobal(pt));
        });
        connect(m_offline, &QAbstractButton::clicked, this, &LibraryPanel::offlineRequested);
        // PlaylistStore doesn't emit signals — refresh is driven from MainWindow
        refresh();
    }

    QLabel*    note() const   { return m_note; }
    NavButton* offline() const { return m_offline; }

    void setSelectedRow(int row)
    {
        m_delegate->setSelectedRow(row);
        m_list->viewport()->update();
    }

signals:
    void newRequested();
    void importRequested();
    void openRequested(int row);
    void contextRequested(int row, const QPoint& globalPos);
    void offlineRequested();

private:
    static QFrame* infoCard(const QString& title, const QString& text, const QString& button, QPushButton** out)
    {
        auto* f = new QFrame;
        f->setObjectName(QStringLiteral("infoCard"));
        f->setStyleSheet(QStringLiteral("QFrame#infoCard{background:#1f1f1f;border-radius:8px;}"));
        auto* l = new QVBoxLayout(f);
        l->setContentsMargins(20, 18, 20, 20);
        l->setSpacing(6);
        auto* t = new QLabel(title);
        t->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:white;background:transparent;"));
        auto* d = new QLabel(text);
        d->setWordWrap(true);
        d->setStyleSheet(QStringLiteral("font-size:14px;font-weight:500;color:white;background:transparent;"));
        auto* b = new QPushButton(button);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(34);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:white;color:black;border:none;border-radius:17px;padding:0 18px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#f2f2f2;}QPushButton:pressed{background:#dcdcdc;}"));
        auto* row = new QHBoxLayout;
        row->addWidget(b);
        row->addStretch(1);
        l->addWidget(t);
        l->addWidget(d);
        l->addSpacing(12);
        l->addLayout(row);
        *out = b;
        return f;
    }

    void refresh()
    {
        const bool any = m_store->playlists().size() > 0;
        m_list->setVisible(any);
        m_empty->setVisible(!any);
    }

    PlaylistStore*     m_store;
    PlaylistListModel* m_model = nullptr;
    QListView*         m_list = nullptr;
    LibraryDelegate*   m_delegate = nullptr;
    QWidget*           m_empty = nullptr;
    QPushButton*       m_create = nullptr;
    QLabel*            m_note = nullptr;
    NavButton*         m_offline = nullptr;
};
