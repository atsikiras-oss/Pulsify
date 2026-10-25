#pragma once
#include <QtWidgets>
#include "widgets.h"
#include "library.h"

// ============================================================================
//  Painting helpers shared by cards, rows and the playlist page
// ============================================================================

// Square, center-cropped artwork (or a placeholder) with no clipping of its own.
inline void fillPixmap(QPainter* p, const QRectF& r, const QPixmap& pm)
{
    if (!pm.isNull()) {
        const qreal w = pm.width(), h = pm.height(), side = qMin(w, h);
        p->drawPixmap(r, pm, QRectF((w - side) / 2, (h - side) / 2, side, side));
        return;
    }
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, QColor(0x3a, 0x3a, 0x3a));
    g.setColorAt(1, QColor(0x22, 0x22, 0x22));
    p->fillRect(r, g);
    p->setPen(QColor(0x8a, 0x8a, 0x8a));
    p->setFont(Theme::font(qMax(8, int(r.height() * 0.42)), QFont::Bold));
    p->drawText(r, Qt::AlignCenter, QString(QChar(0x266A)));
}

// Playlist artwork: 2x2 mosaic with 4+ covers, a single cover with 1-3, a placeholder with none.
inline void fillMosaic(QPainter* p, const QRectF& r, const QVector<QPixmap>& pms)
{
    if (pms.size() >= 4) {
        const qreal hw = r.width() / 2.0, hh = r.height() / 2.0;
        for (int i = 0; i < 4; ++i)
            fillPixmap(p, QRectF(r.left() + (i % 2) * hw, r.top() + (i / 2) * hh, hw, hh), pms[i]);
    } else {
        fillPixmap(p, r, pms.isEmpty() ? QPixmap() : pms.first());
    }
}

inline void drawPlaylistCover(QPainter* p, const QRectF& r, const QVector<QPixmap>& pms, qreal radius)
{
    p->save();
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p->setClipPath(path);
    fillMosaic(p, r, pms);
    p->restore();
}

// Wraps text onto at most two lines; the second line is elided.
inline QStringList wrapTwoLines(const QString& s, const QFontMetrics& fm, int w)
{
    if (s.isEmpty()) return {};
    if (fm.horizontalAdvance(s) <= w) return { s };
    const QStringList words = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString line1;
    int i = 0;
    for (; i < words.size(); ++i) {
        const QString cand = line1.isEmpty() ? words[i] : line1 + QLatin1Char(' ') + words[i];
        if (fm.horizontalAdvance(cand) > w) break;
        line1 = cand;
    }
    if (line1.isEmpty()) return { fm.elidedText(s, Qt::ElideRight, w) };
    const QString rest = words.mid(i).join(QLatin1Char(' '));
    return { line1, fm.elidedText(rest, Qt::ElideRight, w) };
}

// ============================================================================
//  SmoothScrollArea: wheel scrolling with the same easing as the track lists
// ============================================================================
class SmoothScrollArea : public QScrollArea
{
public:
    explicit SmoothScrollArea(QWidget* parent = nullptr) : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"));
        viewport()->setAutoFillBackground(false);
        m_anim = new QPropertyAnimation(verticalScrollBar(), "value", this);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
    }

    void setBody(QWidget* w)
    {
        setWidget(w);
        w->setAutoFillBackground(false);      // QScrollArea turns this on; we want the page tint to show through
    }

    void scrollToTop()
    {
        m_anim->stop();
        verticalScrollBar()->setValue(0);
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        QScrollBar* sb = verticalScrollBar();
        const int delta = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->angleDelta().y();
        if (delta == 0 || sb->maximum() == 0) { e->ignore(); return; }
        const int base = (m_anim->state() == QAbstractAnimation::Running) ? m_anim->endValue().toInt()
                                                                          : sb->value();
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
//  MediaCard: one clickable item of the Home page.
//    Card  - square artwork + title + subtitle (shelves)
//    Tile  - compact horizontal tile (quick-access grid)
//    Hero  - big "Top result" card (search)
//  Hover shows a springy green play button, press squishes like an iOS control.
// ============================================================================
class MediaCard : public QWidget
{
    Q_OBJECT
public:
    enum Layout { Card, Tile, Hero };
    enum Kind   { TrackCard, PlaylistCard };

    static constexpr int CardW = 184, CardH = 254, TileH = 56, HeroH = 214;

    MediaCard(Layout layout, Kind kind, QWidget* parent = nullptr)
        : QWidget(parent), m_layout(layout), m_kind(kind)
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        if (layout == Card)      setFixedSize(CardW, CardH);
        else if (layout == Tile) setFixedHeight(TileH);
        else                     setFixedHeight(HeroH);
        bindUpdate(this, { &m_hover, &m_press, &m_appear, &m_fade, &m_playHover });
        m_skelTimer.setInterval(40);
        connect(&m_skelTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    }

    Layout layout() const { return m_layout; }
    Kind   kind()   const { return m_kind; }

    void setTrackId(int id)               { m_trackId = id; }
    int  trackId() const                  { return m_trackId; }
    void setPlaylistId(const QString& id) { m_plId = id; }
    QString playlistId() const            { return m_plId; }

    void setTexts(const QString& title, const QString& sub)
    {
        if (title == m_title && sub == m_sub) return;
        m_title = title;
        m_sub = sub;
        update();
    }
    void setBadge(const QString& b) { m_badge = b; update(); }

    void setCover(const QPixmap& pm)
    {
        if (pm.isNull() && m_cover.isNull()) return;
        if (!m_cover.isNull() && pm.cacheKey() == m_cover.cacheKey()) return;
        m_old = m_cover;
        m_cover = pm;
        m_fade.set(0.0);
        m_fade.to(1.0, 420, QEasingCurve::OutCubic);
    }
    void setCovers(const QVector<QPixmap>& v) { m_covers = v; update(); }

    void setSkeleton(bool on)
    {
        m_skel = on;
        if (on) { setCursor(Qt::ArrowCursor); m_skelTimer.start(); }
        else    { setCursor(Qt::PointingHandCursor); m_skelTimer.stop(); }
        update();
    }

    void setCurrent(bool current, bool playing)
    {
        if (m_current == current && m_playing == playing) return;
        m_current = current;
        m_playing = playing;
        update();
    }
    bool isCurrent() const { return m_current; }

    // Fade + slide in after a delay (staggered when a shelf is filled).
    void appearAfter(int ms)
    {
        m_appear.set(0.0);
        QTimer::singleShot(ms, this, [this] { m_appear.to(1.0, 520, QEasingCurve::OutCubic); });
    }

signals:
    void activated();                         // card body clicked
    void playClicked();                       // green play button clicked
    void contextRequested(const QPoint& globalPos);

protected:
    void enterEvent(QEnterEvent*) override { if (!m_skel) m_hover.to(1.0, 150); }
    void leaveEvent(QEvent*) override
    {
        m_hover.to(0.0, 250);
        m_playHover.to(0.0, 150);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_skel) m_playHover.to(playRect().contains(e->position()) ? 1.0 : 0.0, 140);
        QWidget::mouseMoveEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_skel || e->button() != Qt::LeftButton) { e->ignore(); return; }
        m_down = true;
        m_downOnPlay = m_hover.value() > 0.2 && playRect().contains(e->position());
        m_press.to(m_downOnPlay ? 1.0 : (m_layout == Tile ? 0.985 : 0.97), 100);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (!m_down) return;
        m_press.to(1.0, 520, bounce(2.4));
        const bool inside = rect().contains(e->position().toPoint());
        const bool onPlay = m_downOnPlay;
        m_down = false;
        if (!inside) return;
        if (onPlay) emit playClicked();
        else        emit activated();
    }
    void contextMenuEvent(QContextMenuEvent* e) override
    {
        if (!m_skel) emit contextRequested(e->globalPos());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const qreal app = m_appear.value();
        p.setOpacity(qBound<qreal>(0.0, app, 1.0));
        p.translate(0, (1.0 - app) * 14.0);
        const QRectF r(rect());
        p.translate(r.center());
        p.scale(m_press.value(), m_press.value());
        p.translate(-r.center());

        const qreal hov = m_hover.value();
        switch (m_layout) {
        case Card: paintCard(p, r, hov); break;
        case Tile: paintTile(p, r, hov); break;
        case Hero: paintHero(p, r, hov); break;
        }
    }

private:
    QRectF playRect() const
    {
        switch (m_layout) {
        case Card: return QRectF(12 + (CardW - 24) - 8 - 48, 12 + (CardW - 24) - 8 - 48, 48, 48);
        case Tile: return QRectF(width() - 14 - 40, (height() - 40) / 2.0, 40, 40);
        case Hero: return QRectF(width() - 20 - 56, height() - 20 - 56, 56, 56);
        }
        return {};
    }

    static qreal pulse()
    {
        static QElapsedTimer clk;
        if (!clk.isValid()) clk.start();
        return 0.5 + 0.5 * std::sin(clk.elapsed() / 1000.0 * 3.4);
    }

    // artwork clipped to an arbitrary path (so a tile can round only its left corners)
    void paintArt(QPainter& p, const QRectF& rc, const QPainterPath& clip)
    {
        p.save();
        p.setClipPath(clip);
        if (m_kind == PlaylistCard) {
            fillMosaic(&p, rc, m_covers);
        } else {
            const qreal f = m_fade.value();
            const qreal base = p.opacity();
            if (f < 1.0) fillPixmap(&p, rc, m_old);
            p.setOpacity(base * f);
            fillPixmap(&p, rc, m_cover);
            p.setOpacity(base);
        }
        p.restore();
    }
    void paintArt(QPainter& p, const QRectF& rc, qreal radius)
    {
        QPainterPath path;
        path.addRoundedRect(rc, radius, radius);
        paintArt(p, rc, path);
    }

    void paintPlay(QPainter& p, qreal hov)
    {
        const qreal a = qMax(hov, m_current ? 1.0 : 0.0);
        if (a < 0.01 || m_skel) return;
        QRectF pr = playRect();
        pr.translate(0, (1.0 - a) * 10.0);
        const qreal base = p.opacity();
        p.setOpacity(base * a);
        const qreal ph = m_playHover.value();
        p.save();
        p.translate(pr.center());
        p.scale(1.0 + 0.07 * ph, 1.0 + 0.07 * ph);
        p.translate(-pr.center());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawEllipse(pr.translated(0, 2));
        p.setBrush(Theme::mix(Theme::accent(), QColor(0x1e, 0xd7, 0x60), ph));
        p.drawEllipse(pr);
        p.setBrush(QColor(10, 10, 10));
        const QPointF c = pr.center();
        const qreal k = pr.width();
        if (m_current && m_playing) {
            p.drawRoundedRect(QRectF(c.x() - k * 0.17, c.y() - k * 0.18, k * 0.12, k * 0.36), 1.5, 1.5);
            p.drawRoundedRect(QRectF(c.x() + k * 0.05, c.y() - k * 0.18, k * 0.12, k * 0.36), 1.5, 1.5);
        } else {
            QPolygonF t;
            t << QPointF(c.x() - k * 0.12, c.y() - k * 0.19) << QPointF(c.x() + k * 0.21, c.y())
              << QPointF(c.x() - k * 0.12, c.y() + k * 0.19);
            p.drawPolygon(t);
        }
        p.restore();
        p.setOpacity(base);
    }

    void paintCard(QPainter& p, const QRectF& r, qreal hov)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(26 * hov)));
        p.drawRoundedRect(r, 8, 8);

        const QRectF cr(r.left() + 12, r.top() + 12, CardW - 24, CardW - 24);
        const qreal tx = r.left() + 12, tw = CardW - 24;

        if (m_skel) {
            const int a = int(14 + 14 * pulse());
            p.setBrush(QColor(255, 255, 255, a));
            p.drawRoundedRect(cr, 6, 6);
            p.drawRoundedRect(QRectF(tx, cr.bottom() + 16, tw * 0.8, 12), 6, 6);
            p.drawRoundedRect(QRectF(tx, cr.bottom() + 38, tw * 0.55, 10), 5, 5);
            return;
        }

        // soft shadow under the artwork
        p.setBrush(QColor(0, 0, 0, 22));
        for (int i = 0; i < 4; ++i)
            p.drawRoundedRect(cr.adjusted(-i, -i + 3, i, i + 3), 6 + i, 6 + i);
        paintArt(p, cr, 6);
        paintPlay(p, hov);

        const QFont tf = Theme::font(15, QFont::DemiBold), sf = Theme::font(13);
        p.setFont(tf);
        p.setPen(m_current ? Theme::accent() : QColor(Qt::white));
        p.drawText(QRectF(tx, cr.bottom() + 10, tw, 22), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(tf).elidedText(m_title, Qt::ElideRight, int(tw)));
        p.setFont(sf);
        p.setPen(Theme::subtext());
        const QStringList lines = wrapTwoLines(m_sub, QFontMetrics(sf), int(tw));
        for (int i = 0; i < lines.size(); ++i)
            p.drawText(QRectF(tx, cr.bottom() + 33 + i * 17, tw, 17), int(Qt::AlignLeft | Qt::AlignVCenter), lines[i]);
    }

    void paintTile(QPainter& p, const QRectF& r, qreal hov)
    {
        QPainterPath tile;
        tile.addRoundedRect(r, 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(26 + 26 * hov)));
        p.drawPath(tile);

        const QRectF art(r.left(), r.top(), TileH, TileH);
        QPainterPath artClip;
        artClip.addRect(art);
        paintArt(p, art, artClip.intersected(tile));

        const qreal tx = r.left() + TileH + 14;
        const qreal tw = r.width() - (tx - r.left()) - 14 - 40 - 8;      // leave room for the play button
        const QFont tf = Theme::font(14, QFont::DemiBold);
        p.setFont(tf);
        p.setPen(m_current ? Theme::accent() : QColor(Qt::white));
        p.drawText(QRectF(tx, r.top(), qMax<qreal>(10, tw), r.height()), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(tf).elidedText(m_title, Qt::ElideRight, int(qMax<qreal>(10, tw))));
        paintPlay(p, hov);
    }

    void paintHero(QPainter& p, const QRectF& r, qreal hov)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(18 + 20 * hov)));
        p.drawRoundedRect(r, 10, 10);

        const QRectF cr(r.left() + 22, r.top() + 22, 96, 96);
        const qreal tx = cr.left(), tw = r.width() - 44;
        paintArt(p, cr, 6);

        const QFont tf = Theme::font(30, QFont::Bold);
        p.setFont(tf);
        p.setPen(m_current ? Theme::accent() : QColor(Qt::white));
        p.drawText(QRectF(tx, cr.bottom() + 12, tw, 42), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(tf).elidedText(m_title, Qt::ElideRight, int(tw)));

        qreal x = tx;
        const qreal y = cr.bottom() + 58;
        if (!m_badge.isEmpty()) {
            const QFont bf = Theme::font(12, QFont::Bold);
            const QFontMetrics bfm(bf);
            const qreal bw = bfm.horizontalAdvance(m_badge) + 24;
            p.setBrush(QColor(0, 0, 0, 120));
            p.drawRoundedRect(QRectF(x, y, bw, 24), 12, 12);
            p.setFont(bf);
            p.setPen(Qt::white);
            p.drawText(QRectF(x, y, bw, 24), Qt::AlignCenter, m_badge);
            x += bw + 12;
        }
        const QFont sf = Theme::font(14, QFont::Medium);
        p.setFont(sf);
        p.setPen(QColor(255, 255, 255, 200));
        p.drawText(QRectF(x, y, qMax<qreal>(10, tx + tw - x - 64), 24), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(sf).elidedText(m_sub, Qt::ElideRight, int(qMax<qreal>(10, tx + tw - x - 64))));
        paintPlay(p, hov);
    }

    Layout  m_layout;
    Kind    m_kind;
    int     m_trackId = -1;
    QString m_plId, m_title, m_sub, m_badge;
    QPixmap m_cover, m_old;
    QVector<QPixmap> m_covers;
    bool    m_skel = false, m_current = false, m_playing = false;
    bool    m_down = false, m_downOnPlay = false;
    QTimer  m_skelTimer;
    Tween   m_hover{this};
    Tween   m_press{this, 1.0};
    Tween   m_appear{this, 1.0};
    Tween   m_fade{this, 1.0};
    Tween   m_playHover{this};
};

// ============================================================================
//  Shelf: titled row of cards that wraps into a grid with "Show all"
// ============================================================================
class Shelf : public QWidget
{
    Q_OBJECT
public:
    static constexpr int HeaderH = 52;

    explicit Shelf(const QString& title, QWidget* parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_title = new QLabel(title, this);
        m_title->setStyleSheet(QStringLiteral("font-size:24px;font-weight:800;color:white;background:transparent;"));
        m_all = new QPushButton(QStringLiteral("Show all"), this);
        m_all->setCursor(Qt::PointingHandCursor);
        m_all->setFocusPolicy(Qt::NoFocus);
        m_all->setFlat(true);
        m_all->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;border:none;color:#b3b3b3;font-size:13px;font-weight:700;padding:4px 12px;}"
            "QPushButton:hover{color:white;text-decoration:underline;}"));
        m_all->hide();
        connect(m_all, &QPushButton::clicked, this, [this] { m_expanded = !m_expanded; relayout(); });
        setFixedHeight(HeaderH + MediaCard::CardH + 8);
    }

    void setTitle(const QString& t) { m_title->setText(t); }
    int  count() const              { return int(m_cards.size()); }
    bool hasContent() const         { return !m_cards.isEmpty(); }
    QList<MediaCard*> cards() const { return m_cards; }

    QVector<int> trackIds() const
    {
        QVector<int> v;
        for (MediaCard* c : m_cards)
            if (c->kind() == MediaCard::TrackCard) v.push_back(c->trackId());
        return v;
    }

    // Takes ownership of the cards.
    void setCards(const QList<MediaCard*>& cards)
    {
        clearCards();
        m_skeleton = false;
        int i = 0;
        for (MediaCard* c : cards) {
            c->setParent(this);
            c->appearAfter(qMin(i, 10) * 35);
            m_cards.push_back(c);
            ++i;
        }
        relayout();
    }

    // Placeholder cards while a shelf is loading.
    void setSkeleton(int n)
    {
        clearCards();
        m_skeleton = true;
        for (int i = 0; i < n; ++i) {
            auto* c = new MediaCard(MediaCard::Card, MediaCard::TrackCard, this);
            c->setSkeleton(true);
            m_cards.push_back(c);
        }
        relayout();
    }
    bool isLoading() const { return m_skeleton; }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        relayout();
    }

private:
    void clearCards()
    {
        for (MediaCard* c : m_cards) { c->hide(); c->deleteLater(); }
        m_cards.clear();
        m_expanded = false;
    }

    void relayout()
    {
        const int W = width();
        const int n = int(m_cards.size());
        const int cols = qMax(1, W / MediaCard::CardW);
        const bool canExpand = n > cols && !m_skeleton;
        if (!canExpand) m_expanded = false;
        const int rows = m_expanded ? (n + cols - 1) / cols : 1;
        const int visible = m_expanded ? n : qMin(n, cols);

        m_title->setGeometry(12, 8, qMax(60, W - 140), 36);
        m_all->setText(m_expanded ? QStringLiteral("Show less") : QStringLiteral("Show all"));
        m_all->setVisible(canExpand);
        const QSize sh = m_all->sizeHint();
        m_all->setGeometry(W - sh.width(), 12, sh.width(), 30);

        for (int i = 0; i < n; ++i) {
            MediaCard* c = m_cards[i];
            if (i < visible) {
                c->move((i % cols) * MediaCard::CardW, HeaderH + (i / cols) * MediaCard::CardH);
                c->show();
            } else {
                c->hide();
            }
        }
        const int h = HeaderH + rows * MediaCard::CardH + 8;
        if (height() != h) setFixedHeight(h);
    }

    QLabel*      m_title = nullptr;
    QPushButton* m_all = nullptr;
    QList<MediaCard*> m_cards;
    bool m_expanded = false;
    bool m_skeleton = false;
};

// ============================================================================
//  TileGrid: the quick-access grid at the top of Home (2 rows, 2-4 columns)
// ============================================================================
class TileGrid : public QWidget
{
public:
    explicit TileGrid(QWidget* parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFixedHeight(MediaCard::TileH);
    }

    int count() const { return int(m_cards.size()); }
    QList<MediaCard*> cards() const { return m_cards; }

    QVector<int> trackIds() const
    {
        QVector<int> v;
        for (MediaCard* c : m_cards)
            if (c->kind() == MediaCard::TrackCard) v.push_back(c->trackId());
        return v;
    }

    void setCards(const QList<MediaCard*>& cards)
    {
        for (MediaCard* c : m_cards) { c->hide(); c->deleteLater(); }
        m_cards.clear();
        int i = 0;
        for (MediaCard* c : cards) {
            c->setParent(this);
            c->appearAfter(i * 30);
            m_cards.push_back(c);
            ++i;
        }
        relayout();
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        relayout();
    }

private:
    void relayout()
    {
        const int gap = 12, H = MediaCard::TileH;
        const int W = width();
        const int cols = qBound(2, (W + gap) / (260 + gap), 4);
        const int n = int(m_cards.size());
        const int show = qMin(n, cols * 2);
        const int rows = (show + cols - 1) / cols;
        const int tw = (W - gap * (cols - 1)) / cols;
        for (int i = 0; i < n; ++i) {
            MediaCard* c = m_cards[i];
            if (i < show) {
                c->setGeometry((i % cols) * (tw + gap), (i / cols) * (H + gap), tw, H);
                c->show();
            } else {
                c->hide();
            }
        }
        const int h = rows > 0 ? rows * H + (rows - 1) * gap : 0;
        if (height() != h) setFixedHeight(h);
    }

    QList<MediaCard*> m_cards;
};

// ============================================================================
//  PlaylistRow: sidebar entry (artwork, name, "Playlist · N songs").
//  Accepts songs dragged from any list.
// ============================================================================
class PlaylistRow : public QAbstractButton
{
    Q_OBJECT
public:
    explicit PlaylistRow(const QString& playlistId, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_id(playlistId)
    {
        setFixedHeight(60);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAcceptDrops(true);
        bindUpdate(this, { &m_hover, &m_press, &m_drop });
    }

    QString playlistId() const { return m_id; }
    void setInfo(const QString& name, const QString& sub) { m_name = name; m_sub = sub; update(); }
    void setCovers(const QVector<QPixmap>& v)             { m_covers = v; update(); }
    void setSelected(bool s)                              { m_selected = s; update(); }
    void setPlayingFrom(bool on)                          { m_playingFrom = on; update(); }

signals:
    void contextRequested(const QPoint& globalPos);
    void trackDropped(int trackId);

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 150); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 250); }
    void mousePressEvent(QMouseEvent* e) override
    {
        QAbstractButton::mousePressEvent(e);
        if (e->button() == Qt::LeftButton) m_press.to(0.97, 100);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        QAbstractButton::mouseReleaseEvent(e);
        m_press.to(1.0, 520, bounce(2.4));
    }
    void contextMenuEvent(QContextMenuEvent* e) override { emit contextRequested(e->globalPos()); }

    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(QLatin1String(kTrackMime))) {
            e->acceptProposedAction();
            m_drop.to(1.0, 140);
        }
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (e->mimeData()->hasFormat(QLatin1String(kTrackMime))) e->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override { m_drop.to(0.0, 200); }
    void dropEvent(QDropEvent* e) override
    {
        m_drop.to(0.0, 200);
        if (!e->mimeData()->hasFormat(QLatin1String(kTrackMime))) return;
        e->acceptProposedAction();
        emit trackDropped(e->mimeData()->data(QLatin1String(kTrackMime)).toInt());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(rect()).adjusted(2, 1, -2, -1);
        p.translate(r.center());
        p.scale(m_press.value(), m_press.value());
        p.translate(-r.center());

        const qreal hv = m_hover.value(), dr = m_drop.value();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(24 * hv) + (m_selected ? 16 : 0)));
        p.drawRoundedRect(r, 8, 8);
        if (dr > 0.01) {
            p.setBrush(QColor(0x1d, 0xb9, 0x54, int(50 * dr)));
            p.setPen(QPen(QColor(0x1d, 0xb9, 0x54, int(255 * dr)), 2));
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
        }

        const QRectF cov(r.left() + 8, r.center().y() - 24, 48, 48);
        drawPlaylistCover(&p, cov, m_covers, 5);

        const qreal tx = cov.right() + 12, tw = r.right() - 8 - tx;
        const QFont nf = Theme::font(15, QFont::Medium), sf = Theme::font(13);
        p.setPen(m_playingFrom ? Theme::accent() : QColor(Qt::white));
        p.setFont(nf);
        p.drawText(QRectF(tx, r.top() + 9, tw, 21), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(nf).elidedText(m_name, Qt::ElideRight, int(tw)));
        p.setPen(Theme::subtext());
        p.setFont(sf);
        p.drawText(QRectF(tx, r.top() + 30, tw, 19), int(Qt::AlignLeft | Qt::AlignVCenter),
                   QFontMetrics(sf).elidedText(m_sub, Qt::ElideRight, int(tw)));
    }

private:
    QString m_id, m_name, m_sub;
    QVector<QPixmap> m_covers;
    bool m_selected = false, m_playingFrom = false;
    Tween m_hover{this};
    Tween m_press{this, 1.0};
    Tween m_drop{this};
};

// ============================================================================
//  PlaylistCover: the large artwork on a playlist page
// ============================================================================
class PlaylistCover : public QWidget
{
public:
    explicit PlaylistCover(int size, QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(size + 36, size + 36);
        m_size = size;
    }
    void setCovers(const QVector<QPixmap>& v) { m_covers = v; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF inner(18, 14, m_size, m_size);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 14));
        for (int i = 0; i < 10; ++i)
            p.drawRoundedRect(inner.adjusted(-i * 2, -i * 2 + 8, i * 2, i * 2 + 8), 8 + i * 2, 8 + i * 2);
        drawPlaylistCover(&p, inner, m_covers, 8);
    }

private:
    int m_size = 180;
    QVector<QPixmap> m_covers;
};

// ============================================================================
//  Toast: small green pill that springs up above the player bar
// ============================================================================
class Toast : public QWidget
{
public:
    explicit Toast(QWidget* host) : QWidget(host), m_host(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
        bindUpdate(this, { &m_t });
        m_hide.setSingleShot(true);
        connect(&m_hide, &QTimer::timeout, this, [this] { m_t.to(0.0, 260, QEasingCurve::InCubic); });
        connect(&m_t, &Tween::changed, this, [this] {
            if (m_t.value() <= 0.002 && m_t.target() == 0.0) hide();
        });
    }

    void showMessage(const QString& text, int bottomOffset)
    {
        m_text = text;
        const QFontMetrics fm(Theme::font(14, QFont::DemiBold));
        const int w = qMin(m_host->width() - 40, fm.horizontalAdvance(text) + 48);
        resize(qMax(120, w), 46);
        move((m_host->width() - width()) / 2, m_host->height() - bottomOffset - height());
        m_t.set(0.0);                         // (hides while the value is 0 and the target is 0 ...)
        m_t.to(1.0, 380, bounce(1.7));        // ... so retarget first, then show
        show();
        raise();
        m_hide.start(2300);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        const qreal t = m_t.value();
        p.setOpacity(qBound<qreal>(0.0, t * 1.4, 1.0));
        p.translate(0, (1.0 - t) * 22.0);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(r.translated(0, 2), 23, 23);
        p.setBrush(Theme::accent());
        p.drawRoundedRect(r, 23, 23);
        p.setPen(QColor(10, 10, 10));
        p.setFont(Theme::font(14, QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, QFontMetrics(Theme::font(14, QFont::DemiBold))
                                           .elidedText(m_text, Qt::ElideRight, int(r.width()) - 24));
    }

private:
    QWidget* m_host;
    QString  m_text;
    QTimer   m_hide;
    Tween    m_t{this};
};

// ============================================================================
//  NameDialog: dark rounded prompt for creating / renaming a playlist
// ============================================================================
class NameDialog : public QDialog
{
public:
    static bool ask(QWidget* parent, const QString& heading, const QString& initial,
                    const QString& okText, QString& out)
    {
        NameDialog d(parent, heading, initial, okText);
        if (d.exec() != QDialog::Accepted) return false;
        out = d.m_edit->text().trimmed();
        return !out.isEmpty();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(255, 255, 255, 26), 1));
        p.setBrush(QColor(0x28, 0x28, 0x28));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    }

    void showEvent(QShowEvent* e) override
    {
        QDialog::showEvent(e);
        if (parentWidget()) {
            const QRect pr = parentWidget()->window()->geometry();
            move(pr.center() - rect().center());
        }
        auto* a = new QPropertyAnimation(this, "windowOpacity", this);
        a->setDuration(180);
        a->setStartValue(0.0);
        a->setEndValue(1.0);
        a->start(QAbstractAnimation::DeleteWhenStopped);
        m_edit->setFocus();
        m_edit->selectAll();
    }

private:
    NameDialog(QWidget* parent, const QString& heading, const QString& initial, const QString& okText)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(420, 214);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(28, 26, 28, 22);
        v->setSpacing(14);

        auto* h = new QLabel(heading);
        h->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:white;background:transparent;"));
        v->addWidget(h);

        m_edit = new QLineEdit(initial);
        m_edit->setMaxLength(80);
        m_edit->setFixedHeight(46);
        m_edit->setPlaceholderText(QStringLiteral("Playlist name"));
        m_edit->setStyleSheet(QStringLiteral(
            "QLineEdit{background:rgba(255,255,255,0.10);border:1px solid transparent;border-radius:8px;"
            "padding:0 14px;color:white;font-size:15px;selection-background-color:#1DB954;}"
            "QLineEdit:hover{background:rgba(255,255,255,0.14);}"
            "QLineEdit:focus{background:rgba(255,255,255,0.14);border:1px solid rgba(255,255,255,0.7);}"));
        v->addWidget(m_edit);
        v->addStretch(1);

        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* cancel = new QPushButton(QStringLiteral("Cancel"));
        auto* ok = new QPushButton(okText);
        for (auto* b : { cancel, ok }) {
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setAutoDefault(false);
            b->setDefault(false);
        }
        cancel->setFixedHeight(42);
        ok->setFixedHeight(42);
        cancel->setStyleSheet(QStringLiteral(
            "QPushButton{background:transparent;color:#b3b3b3;border:none;padding:0 20px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{color:white;}"));
        ok->setStyleSheet(QStringLiteral(
            "QPushButton{background:#1DB954;color:black;border:none;border-radius:21px;padding:0 28px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#1ed760;}QPushButton:pressed{background:#169c46;}"
            "QPushButton:disabled{background:rgba(255,255,255,0.12);color:#777;}"));
        row->addStretch(1);
        row->addWidget(cancel);
        row->addWidget(ok);
        v->addLayout(row);

        ok->setEnabled(!initial.trimmed().isEmpty());
        connect(m_edit, &QLineEdit::textChanged, this, [ok](const QString& t) { ok->setEnabled(!t.trimmed().isEmpty()); });
        connect(m_edit, &QLineEdit::returnPressed, this, [this, ok] { if (ok->isEnabled()) accept(); });
        connect(ok, &QPushButton::clicked, this, [this] { accept(); });
        connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
    }

    QLineEdit* m_edit = nullptr;
};

// ============================================================================
//  AvatarButton: round profile picture (the signed-in Google / YouTube account).
//  Signed out it shows a neutral person icon; signed in without a picture yet it
//  shows the first letter of the account name.
// ============================================================================
class AvatarButton : public QAbstractButton
{
public:
    explicit AvatarButton(int size, QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setFixedSize(size, size);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        bindUpdate(this, { &m_hover });
    }

    void setProfile(const QPixmap& pm, const QString& name, bool signedIn)
    {
        m_pm = pm;
        m_name = name;
        m_in = signedIn;
        update();
    }

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 160); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 260); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal d = qMin(width(), height());
        const qreal hot = m_hover.value();

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, int(30 * hot)));
        p.drawEllipse(QRectF(0, 0, d, d));

        const QRectF r(4, 4, d - 8, d - 8);
        QPainterPath clip;
        clip.addEllipse(r);
        p.save();
        p.setClipPath(clip);
        if (m_in && !m_pm.isNull()) {
            const qreal dpr = devicePixelRatioF();
            QPixmap sc = m_pm.scaled((r.size() * dpr).toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            sc.setDevicePixelRatio(dpr);
            const QSizeF ls = QSizeF(sc.size()) / dpr;
            p.drawPixmap(QPointF(r.center().x() - ls.width() / 2, r.center().y() - ls.height() / 2), sc);
        } else if (m_in) {
            p.fillRect(r, Theme::accent());
            p.restore();
            p.save();
            QFont f = font();
            f.setBold(true);
            f.setPixelSize(int(d * 0.42));
            p.setFont(f);
            p.setPen(QColor(10, 10, 10));
            const QString t = m_name.trimmed().left(1).toUpper();
            p.drawText(r, Qt::AlignCenter, t.isEmpty() ? QStringLiteral("?") : t);
        } else {
            p.fillRect(r, QColor(0x2e, 0x2e, 0x2e));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xb3, 0xb3, 0xb3));
            const QPointF c = r.center();
            p.drawEllipse(QPointF(c.x(), c.y() - r.height() * 0.14), r.width() * 0.21, r.width() * 0.21);
            p.drawEllipse(QPointF(c.x(), c.y() + r.height() * 0.56), r.width() * 0.42, r.height() * 0.36);
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, int(60 + 150 * hot)), 1.2));
        p.drawEllipse(r);
    }

private:
    QPixmap m_pm;
    QString m_name;
    bool    m_in = false;
    Tween   m_hover{this};
};
