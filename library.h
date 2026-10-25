#pragma once
#include <QtWidgets>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QQueue>
#include "widgets.h"
#include "online.h"

// Id ranges: library files < 1,000,000 <= online (YouTube) tracks < 2,000,000 <= files that are only in playlists.
static constexpr int OnlineIdBase = 1000000;
static constexpr int ExtraIdBase  = 2000000;

// Mime type used when dragging a song onto a playlist in the sidebar.
inline constexpr const char* kTrackMime = "application/x-pulse-track";

enum TrackRole {
    IdRole = Qt::UserRole + 1,
    ArtistRole,
    AlbumRole,
    DurationRole,
    CoverRole,
    SearchRole
};

struct MetaResult {
    int     id = -1;
    QString title, artist, album;
    qint64  durationMs = 0;
    QImage  cover;
};

struct Track {
    int     id = 0;
    QString path, title, artist, album;
    qint64  durationMs = 0;
    QPixmap cover, thumb;
    QColor  tint;               // invalid until cover art is known
    bool    online = false;     // true = YouTube stream (no local file)
    QString videoId, thumbUrl;
};

// ============================================================================
//  MetaLoader: reads tags + cover art one file at a time via a hidden QMediaPlayer
// ============================================================================
class MetaLoader : public QObject
{
    Q_OBJECT
public:
    explicit MetaLoader(QObject* parent = nullptr) : QObject(parent)
    {
        m_player = new QMediaPlayer(this);
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(4000);
        connect(&m_timeout, &QTimer::timeout, this, [this] { finish(); });

        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (!m_busy) return;
            if (s == QMediaPlayer::LoadedMedia) {
                const int id = m_cur.first;
                QTimer::singleShot(40, this, [this, id] { if (m_busy && m_cur.first == id) finish(); });
            } else if (s == QMediaPlayer::InvalidMedia) {
                finish();
            }
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this] { if (m_busy) finish(); });
    }

    void enqueue(int id, const QString& path)
    {
        m_queue.enqueue(qMakePair(id, path));
        if (!m_busy) QTimer::singleShot(0, this, &MetaLoader::next);
    }

signals:
    void loaded(const MetaResult& r);

private:
    void next()
    {
        if (m_busy || m_queue.isEmpty()) return;
        m_cur = m_queue.dequeue();
        m_busy = true;
        m_timeout.start();
        m_player->setSource(QUrl::fromLocalFile(m_cur.second));
    }

    void finish()
    {
        if (!m_busy) return;
        m_timeout.stop();

        MetaResult r;
        r.id = m_cur.first;
        const QMediaMetaData md = m_player->metaData();
        r.title  = md.stringValue(QMediaMetaData::Title);
        r.artist = md.stringValue(QMediaMetaData::ContributingArtist);
        if (r.artist.isEmpty()) r.artist = md.stringValue(QMediaMetaData::AlbumArtist);
        if (r.artist.isEmpty()) r.artist = md.stringValue(QMediaMetaData::Author);
        r.album = md.stringValue(QMediaMetaData::AlbumTitle);
        r.durationMs = m_player->duration();
        r.cover = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
        if (r.cover.isNull())
            r.cover = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();

        m_busy = false;
        m_player->setSource(QUrl());
        emit loaded(r);
        QTimer::singleShot(0, this, &MetaLoader::next);
    }

    QMediaPlayer* m_player = nullptr;
    QTimer        m_timeout;
    QQueue<QPair<int, QString>> m_queue;
    QPair<int, QString> m_cur;
    bool          m_busy = false;
};

// ============================================================================
//  TrackModel
// ============================================================================
class TrackModel : public QAbstractListModel
{
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_tracks.size());
    }

    QVariant data(const QModelIndex& ix, int role) const override
    {
        if (!ix.isValid() || ix.row() < 0 || ix.row() >= m_tracks.size()) return {};
        const Track& t = m_tracks[ix.row()];
        switch (role) {
        case Qt::DisplayRole: return t.title;
        case IdRole:          return t.id;
        case ArtistRole:      return t.artist;
        case AlbumRole:       return t.album;
        case DurationRole:    return t.durationMs;
        case CoverRole:       return t.thumb;
        case SearchRole:      return t.title + QLatin1Char(' ') + t.artist + QLatin1Char(' ') + t.album;
        }
        return {};
    }

    // Returns ids of newly added tracks (duplicates are skipped).
    QVector<int> append(const QStringList& paths)
    {
        QVector<int> ids;
        QVector<Track> add;
        for (const QString& p : paths) {
            const QString abs = QFileInfo(p).absoluteFilePath();
            if (m_paths.contains(abs)) continue;
            m_paths.insert(abs);
            Track t;
            t.id = m_nextId++;
            m_pathToId.insert(abs, t.id);
            t.path = abs;
            t.title = QFileInfo(abs).completeBaseName();
            t.artist = QStringLiteral("Unknown artist");
            add.push_back(t);
            ids.push_back(t.id);
        }
        if (add.isEmpty()) return ids;
        beginInsertRows(QModelIndex(), int(m_tracks.size()), int(m_tracks.size() + add.size() - 1));
        m_tracks += add;
        endInsertRows();
        emit libraryChanged();
        return ids;
    }

    void remove(int id)
    {
        const int row = rowOf(id);
        if (row < 0) return;
        beginRemoveRows(QModelIndex(), row, row);
        m_paths.remove(m_tracks[row].path);
        m_pathToId.remove(m_tracks[row].path);
        m_tracks.removeAt(row);
        endRemoveRows();
        emit libraryChanged();
    }

    // Library id of a file path, or -1.
    int idForPath(const QString& path) const
    {
        return m_pathToId.value(QFileInfo(path).absoluteFilePath(), -1);
    }

    // Merges non-empty fields of a MetaResult into a track.
    static void mergeMeta(Track& t, const MetaResult& r)
    {
        if (!r.title.trimmed().isEmpty())  t.title  = r.title.trimmed();
        if (!r.artist.trimmed().isEmpty()) t.artist = r.artist.trimmed();
        if (!r.album.trimmed().isEmpty())  t.album  = r.album.trimmed();
        if (r.durationMs > 0) t.durationMs = r.durationMs;
        if (!r.cover.isNull()) {
            const QImage big = (r.cover.width() > 640 || r.cover.height() > 640)
                ? r.cover.scaled(640, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                : r.cover;
            t.cover = QPixmap::fromImage(big);
            t.thumb = QPixmap::fromImage(r.cover.scaled(96, 96, Qt::KeepAspectRatioByExpanding,
                                                        Qt::SmoothTransformation));
            t.tint = Theme::tintFrom(r.cover);
        }
    }

    void applyMeta(const MetaResult& r)
    {
        const int row = rowOf(r.id);
        if (row < 0) return;
        mergeMeta(m_tracks[row], r);
        emit dataChanged(index(row), index(row));
        emit libraryChanged();
    }

    // ---- online results (separate model instance from the offline library)
    void setIdBase(int base) { m_nextId = base; }

    void clear()
    {
        if (m_tracks.isEmpty()) return;
        beginResetModel();
        m_tracks.clear();
        m_paths.clear();
        m_pathToId.clear();
        m_ids.clear();
        endResetModel();
        emit libraryChanged();
    }

    // Appends search results (duplicates by video id are skipped); returns new ids.
    QVector<int> appendOnline(const QVector<OnlineItem>& items)
    {
        QVector<int> ids;
        QVector<Track> add;
        for (const OnlineItem& it : items) {
            const QString key = QStringLiteral("yt:") + it.id;
            if (m_paths.contains(key)) continue;
            m_paths.insert(key);
            Track t;
            t.id = m_nextId++;
            t.online = true;
            t.videoId = it.id;
            t.path = QStringLiteral("https://www.youtube.com/watch?v=") + it.id;
            t.title = it.title;
            t.artist = it.artist;
            t.album = it.album;
            t.durationMs = it.durationMs;
            t.thumbUrl = it.thumbUrl;
            add.push_back(t);
            ids.push_back(t.id);
        }
        if (add.isEmpty()) return ids;
        beginInsertRows(QModelIndex(), int(m_tracks.size()), int(m_tracks.size() + add.size() - 1));
        m_tracks += add;
        endInsertRows();
        emit libraryChanged();
        return ids;
    }

    // Appends tracks that already carry their own (shared) ids. Duplicate ids are skipped.
    QVector<int> appendTracks(const QVector<Track>& ts)
    {
        QVector<int> ids;
        QVector<Track> add;
        for (const Track& t : ts) {
            if (t.id <= 0 || m_ids.contains(t.id)) continue;
            m_ids.insert(t.id);
            add.push_back(t);
            ids.push_back(t.id);
        }
        if (add.isEmpty()) return ids;
        beginInsertRows(QModelIndex(), int(m_tracks.size()), int(m_tracks.size() + add.size() - 1));
        m_tracks += add;
        endInsertRows();
        emit libraryChanged();
        return ids;
    }

    // Moves a row so that it ends up at index `to` (used to reorder a playlist in place).
    bool moveRow(int from, int to)
    {
        if (from == to || from < 0 || to < 0 || from >= m_tracks.size() || to >= m_tracks.size()) return false;
        if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to)) return false;
        m_tracks.move(from, to);
        endMoveRows();
        emit libraryChanged();
        return true;
    }

    void removeAtRow(int row)
    {
        if (row < 0 || row >= m_tracks.size()) return;
        beginRemoveRows(QModelIndex(), row, row);
        m_ids.remove(m_tracks[row].id);
        m_tracks.removeAt(row);
        endRemoveRows();
        emit libraryChanged();
    }

    const Track& at(int row) const { return m_tracks[row]; }

    QVector<int> ids() const
    {
        QVector<int> v;
        v.reserve(m_tracks.size());
        for (const Track& t : m_tracks) v.push_back(t.id);
        return v;
    }

    void setDuration(int id, qint64 ms)
    {
        const int row = rowOf(id);
        if (row < 0 || ms <= 0 || m_tracks[row].durationMs == ms) return;
        m_tracks[row].durationMs = ms;
        emit dataChanged(index(row), index(row));
        emit libraryChanged();
    }

    const Track* byId(int id) const
    {
        for (const Track& t : m_tracks) if (t.id == id) return &t;
        return nullptr;
    }

    int rowOf(int id) const
    {
        for (int i = 0; i < m_tracks.size(); ++i) if (m_tracks[i].id == id) return i;
        return -1;
    }

    QStringList paths() const
    {
        QStringList l;
        for (const Track& t : m_tracks) l << t.path;
        return l;
    }

    qint64 totalDuration() const
    {
        qint64 s = 0;
        for (const Track& t : m_tracks) s += t.durationMs;
        return s;
    }

signals:
    void libraryChanged();

private:
    QVector<Track> m_tracks;
    QSet<QString>  m_paths;
    QHash<QString, int> m_pathToId;
    QSet<int>      m_ids;
    int            m_nextId = 1;
};

// Shows only the first N rows of its source (used for the compact "Songs" list in search results).
class HeadProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setLimit(int n)
    {
        m_limit = n;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex&) const override { return row < m_limit; }

private:
    int m_limit = 4;
};

// ============================================================================
//  Row state interface (hover / appear animation values) for the delegate
// ============================================================================
struct RowStateProvider {
    virtual ~RowStateProvider() = default;
    virtual qreal hoverOf(int id) const = 0;
    virtual qreal appearOf(int id) const = 0;
    virtual int   currentId() const = 0;
    virtual bool  nowPlaying() const = 0;
    virtual qreal clockSec() const = 0;
};

// ============================================================================
//  TrackDelegate
// ============================================================================
class TrackDelegate : public QStyledItemDelegate
{
public:
    TrackDelegate(RowStateProvider* st, QObject* parent) : QStyledItemDelegate(parent), m_st(st) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(100, 58); }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& ix) const override
    {
        const int id = ix.data(IdRole).toInt();
        const qreal hov = m_st->hoverOf(id);
        const qreal app = m_st->appearOf(id);
        const bool current = (id == m_st->currentId());

        p->save();
        p->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p->setOpacity(qBound<qreal>(0.0, app, 1.0));

        QRectF r = QRectF(opt.rect).adjusted(10, 3, -10, -3);
        r.translate(0, (1.0 - app) * 18.0);

        if (hov > 0.002) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 255, int(28 * hov)));
            p->drawRoundedRect(r, 8, 8);
        }

        // --- leading column: number / play arrow / equalizer
        const QPointF c(r.left() + 30, r.center().y());
        if (current) {
            drawEq(p, c);
        } else {
            QColor nc = Theme::subtext();
            nc.setAlphaF(float(1.0 - hov));
            p->setPen(nc);
            p->setFont(Theme::font(14));
            p->drawText(QRectF(r.left(), r.top(), 60, r.height()), Qt::AlignCenter,
                        QString::number(ix.row() + 1));
            if (hov > 0.02) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(255, 255, 255, int(255 * hov)));
                QPolygonF t;
                t << QPointF(c.x() - 5.5, c.y() - 7) << QPointF(c.x() + 7, c.y()) << QPointF(c.x() - 5.5, c.y() + 7);
                p->drawPolygon(t);
            }
        }

        // --- cover
        const qreal thumb = 40;
        const QRectF tr(r.left() + 62, r.center().y() - thumb / 2, thumb, thumb);
        drawCover(p, tr, ix.data(CoverRole).value<QPixmap>(), 5);

        // --- text
        const qreal textX = tr.right() + 14;
        const qreal durW = 60;
        const QRectF durR(r.right() - 18 - durW, r.top(), durW, r.height());
        const bool showAlbum = r.width() > 760;
        const qreal albumX = r.left() + r.width() * 0.56;
        const qreal titleRight = (showAlbum ? albumX : durR.left()) - 16;

        const QFont tf = Theme::font(15, QFont::Medium);
        const QFont af = Theme::font(13);
        const QFontMetrics tfm(tf), afm(af);
        const int tw = int(qMax<qreal>(10, titleRight - textX));

        p->setFont(tf);
        p->setPen(current ? Theme::accent() : QColor(Qt::white));
        p->drawText(QRectF(textX, r.top() + 6, tw, 22), int(Qt::AlignLeft | Qt::AlignVCenter),
                    tfm.elidedText(ix.data().toString(), Qt::ElideRight, tw));

        p->setFont(af);
        p->setPen(Theme::mix(Theme::subtext(), Qt::white, hov * 0.6));
        p->drawText(QRectF(textX, r.top() + 28, tw, 20), int(Qt::AlignLeft | Qt::AlignVCenter),
                    afm.elidedText(ix.data(ArtistRole).toString(), Qt::ElideRight, tw));

        if (showAlbum) {
            const int aw = int(durR.left() - 16 - albumX);
            p->setPen(Theme::mix(Theme::subtext(), Qt::white, hov * 0.6));
            p->drawText(QRectF(albumX, r.top(), aw, r.height()), int(Qt::AlignLeft | Qt::AlignVCenter),
                        afm.elidedText(ix.data(AlbumRole).toString(), Qt::ElideRight, aw));
        }

        const qint64 dur = ix.data(DurationRole).toLongLong();
        p->setPen(Theme::subtext());
        p->drawText(durR, int(Qt::AlignRight | Qt::AlignVCenter),
                    dur > 0 ? fmtTime(dur) : QStringLiteral("--:--"));

        p->restore();
    }

private:
    void drawEq(QPainter* p, const QPointF& c) const
    {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::accent());
        const qreal t = m_st->clockSec();
        const bool playing = m_st->nowPlaying();
        static const qreal still[3] = { 6.0, 12.0, 8.0 };
        for (int i = 0; i < 3; ++i) {
            const qreal h = playing ? 5.0 + 10.0 * (0.5 + 0.5 * std::sin(t * (6.0 + i * 2.3) + i * 1.7))
                                    : still[i];
            p->drawRoundedRect(QRectF(c.x() - 8 + i * 6, c.y() + 7 - h, 3.5, h), 1.5, 1.5);
        }
    }

    RowStateProvider* m_st;
};

// ============================================================================
//  TrackView: smooth wheel scrolling, animated hover, staggered row appear
// ============================================================================
class TrackView : public QListView, public RowStateProvider
{
public:
    explicit TrackView(QWidget* parent = nullptr) : QListView(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setStyleSheet(QStringLiteral("QListView { background: transparent; border: none; outline: none; }"));
        viewport()->setAutoFillBackground(false);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSelectionMode(QAbstractItemView::NoSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setUniformItemSizes(true);
        setMouseTracking(true);
        setItemDelegate(new TrackDelegate(this, this));

        m_scroll = new QPropertyAnimation(verticalScrollBar(), "value", this);
        m_scroll->setEasingCurve(QEasingCurve::OutCubic);

        m_eq = new QTimer(this);
        m_eq->setInterval(33);
        connect(m_eq, &QTimer::timeout, viewport(), QOverload<>::of(&QWidget::update));
        m_clock.start();
    }

    void setEmptyText(const QString& t) { m_empty = t; viewport()->update(); }

    // false = the list never scrolls by itself (it sits inside a page that scrolls instead)
    void setScrollable(bool s)
    {
        m_scrollable = s;
        setVerticalScrollBarPolicy(s ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    }
    // true = rows can be dragged onto playlists in the sidebar
    void setTrackDrag(bool on) { m_trackDrag = on; }

    void setNowPlaying(int id, bool playing)
    {
        m_cur = id;
        m_playing = playing;
        if (playing && id >= 0) { if (!m_eq->isActive()) m_eq->start(); }
        else m_eq->stop();
        viewport()->update();
    }

    // Stagger-fade the given rows in (first 16 only).
    void appearIds(const QVector<int>& ids)
    {
        int n = 0;
        for (int id : ids) {
            if (n >= 16) break;
            Tween* t = tweenFor(m_appear, id);
            t->set(0.0);
            QTimer::singleShot(n * 40, t, [t] { t->to(1.0, 560, QEasingCurve::OutCubic); });
            ++n;
        }
    }

    // RowStateProvider
    qreal hoverOf(int id) const override  { Tween* t = m_hover.value(id, nullptr);  return t ? t->value() : 0.0; }
    qreal appearOf(int id) const override { Tween* t = m_appear.value(id, nullptr); return t ? t->value() : 1.0; }
    int   currentId() const override      { return m_cur; }
    bool  nowPlaying() const override     { return m_playing; }
    qreal clockSec() const override       { return m_clock.elapsed() / 1000.0; }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        m_pressId = -1;
        if (e->button() == Qt::LeftButton) {
            const QModelIndex ix = indexAt(e->position().toPoint());
            if (ix.isValid()) {
                m_pressId = ix.data(IdRole).toInt();
                m_pressPos = e->position().toPoint();
                m_pressTitle = ix.data().toString();
            }
        }
        QListView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_trackDrag && (e->buttons() & Qt::LeftButton) && m_pressId >= 0
            && (e->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance() * 2) {
            startTrackDrag();
            return;
        }
        QListView::mouseMoveEvent(e);
        updateHover(e->position().toPoint());
    }
    void leaveEvent(QEvent* e) override
    {
        QListView::leaveEvent(e);
        setHovered(-1);
    }
    void scrollContentsBy(int dx, int dy) override
    {
        QListView::scrollContentsBy(dx, dy);
        if (underMouse()) updateHover(viewport()->mapFromGlobal(QCursor::pos()));
    }

    void wheelEvent(QWheelEvent* e) override
    {
        QScrollBar* sb = verticalScrollBar();
        int delta = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->angleDelta().y();
        if (delta == 0 || !m_scrollable || sb->maximum() == 0) { e->ignore(); return; }   // let the parent page scroll
        const int base = (m_scroll->state() == QAbstractAnimation::Running) ? m_scroll->endValue().toInt()
                                                                            : sb->value();
        const int target = qBound(sb->minimum(), base - delta, sb->maximum());
        m_scroll->stop();
        m_scroll->setStartValue(sb->value());
        m_scroll->setEndValue(target);
        m_scroll->setDuration(420);
        m_scroll->start();
        e->accept();
    }

    void paintEvent(QPaintEvent* e) override
    {
        QListView::paintEvent(e);
        if (model() && model()->rowCount() == 0 && !m_empty.isEmpty()) {
            QPainter p(viewport());
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setPen(QColor(255, 255, 255, 120));
            p.setFont(Theme::font(15, QFont::Medium));
            p.drawText(viewport()->rect().adjusted(30, 0, -30, 80),
                       int(Qt::AlignCenter | Qt::TextWordWrap), m_empty);
        }
    }

private:
    void startTrackDrag()
    {
        const int id = m_pressId;
        const QString title = m_pressTitle;
        m_pressId = -1;
        auto* drag = new QDrag(this);
        auto* md = new QMimeData;
        md->setData(QLatin1String(kTrackMime), QByteArray::number(id));
        drag->setMimeData(md);

        const QFont f = Theme::font(13, QFont::Medium);
        const QFontMetrics fm(f);
        const QString txt = fm.elidedText(title, Qt::ElideRight, 220);
        QPixmap pm(fm.horizontalAdvance(txt) + 28, 34);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::TextAntialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::accent());
            p.drawRoundedRect(QRectF(pm.rect()), 17, 17);
            p.setPen(QColor(10, 10, 10));
            p.setFont(f);
            p.drawText(pm.rect(), Qt::AlignCenter, txt);
        }
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(14, 17));
        setHovered(-1);
        drag->exec(Qt::CopyAction);
    }

    Tween* tweenFor(QHash<int, Tween*>& map, int id)
    {
        Tween* t = map.value(id, nullptr);
        if (!t) {
            t = new Tween(this, 0.0);
            connect(t, &Tween::changed, viewport(), QOverload<>::of(&QWidget::update));
            map.insert(id, t);
        }
        return t;
    }

    void updateHover(const QPoint& vpPos)
    {
        const QModelIndex ix = indexAt(vpPos);
        setHovered(ix.isValid() ? ix.data(IdRole).toInt() : -1);
    }

    void setHovered(int id)
    {
        if (id == m_hoverId) return;
        if (m_hoverId >= 0) tweenFor(m_hover, m_hoverId)->to(0.0, 320);
        if (id >= 0)        tweenFor(m_hover, id)->to(1.0, 160);
        m_hoverId = id;
        viewport()->setCursor(id >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    QHash<int, Tween*> m_hover, m_appear;
    QPropertyAnimation* m_scroll = nullptr;
    QTimer*  m_eq = nullptr;
    QElapsedTimer m_clock;
    QString  m_empty;
    int      m_hoverId = -1;
    int      m_cur = -1;
    bool     m_playing = false;
    bool     m_scrollable = true;
    bool     m_trackDrag = true;
    int      m_pressId = -1;
    QPoint   m_pressPos;
    QString  m_pressTitle;
};
