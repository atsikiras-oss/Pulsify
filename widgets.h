#pragma once
#include <QtWidgets>
#include <QVariantAnimation>
#include <cmath>
#include <initializer_list>

#include "theme.h"

inline QString fmtTime(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 s = ms / 1000;
    const int h = int(s / 3600), m = int((s % 3600) / 60), sec = int(s % 60);
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
}

// Springy easing used for "iOS-like" release / settle animations.
inline QEasingCurve bounce(qreal overshoot)
{
    QEasingCurve c(QEasingCurve::OutBack);
    c.setOvershoot(overshoot);
    return c;
}

// ============================================================================
//  Tween: a single animated number. Everything in the UI is built on this.
// ============================================================================
class Tween : public QObject
{
    Q_OBJECT
public:
    explicit Tween(QObject* parent, qreal initial = 0.0)
        : QObject(parent), m_value(initial), m_target(initial)
    {
        m_anim = new QVariantAnimation(this);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_value = v.toReal();
            emit changed();
        });
    }

    qreal value()  const { return m_value; }
    qreal target() const { return m_target; }

    void set(qreal v)
    {
        m_anim->stop();
        m_value = m_target = v;
        emit changed();
    }

    void to(qreal target, int ms = 250, QEasingCurve curve = QEasingCurve::OutCubic)
    {
        m_target = target;
        m_anim->stop();
        m_anim->setStartValue(m_value);
        m_anim->setEndValue(target);
        m_anim->setDuration(ms);
        m_anim->setEasingCurve(curve);
        m_anim->start();
    }

signals:
    void changed();

private:
    QVariantAnimation* m_anim = nullptr;
    qreal m_value;
    qreal m_target;
};

inline void bindUpdate(QWidget* w, std::initializer_list<Tween*> tweens)
{
    for (Tween* t : tweens)
        QObject::connect(t, &Tween::changed, w, QOverload<>::of(&QWidget::update));
}

// Draws a square, center-cropped, rounded cover (or a placeholder).
inline void drawCover(QPainter* p, const QRectF& r, const QPixmap& pm, qreal radius)
{
    p->save();
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p->setClipPath(path);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    if (!pm.isNull()) {
        const qreal w = pm.width(), h = pm.height(), side = qMin(w, h);
        p->drawPixmap(r, pm, QRectF((w - side) / 2, (h - side) / 2, side, side));
    } else {
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, Theme::greyAt(0.169));
        g.setColorAt(1, Theme::greyAt(0.084));
        p->fillRect(r, g);
        p->setPen(Theme::greyAt(0.506));
        p->setFont(Theme::font(qMax(8, int(r.height() * 0.42)), QFont::Bold));
        p->drawText(r, Qt::AlignCenter, QString(QChar(0x266A)));
    }
    p->restore();
}

// ============================================================================
//  IconButton: vector icons, hover grow, press squish + springy release
// ============================================================================
class IconButton : public QAbstractButton
{
public:
    enum Kind { Prev, Next, Shuffle, Repeat, Play, Chevron, Speaker, Home, Plus, More, Lyrics, Info };

    IconButton(Kind kind, int size, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_kind(kind)
    {
        setFixedSize(size, size);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        bindUpdate(this, { &m_hover, &m_press, &m_morph, &m_active });
    }

    void setCircle(const QColor& c, const QColor& glyph = QColor())
    { m_circle = c; m_glyph = glyph.isValid() ? glyph : Theme::contrast(c); m_hasCircle = true; update(); }
    void setPlaying(bool p)         { m_morph.to(p ? 1.0 : 0.0, 420, QEasingCurve::OutCubic); }
    void setActive(bool a)          { m_active.to(a ? 1.0 : 0.0, 220); }
    void setRepeatOne(bool one)     { m_one = one; update(); }
    void setLevel(qreal l)          { m_level = l; update(); }

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 160); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 260); }

    void mousePressEvent(QMouseEvent* e) override
    {
        QAbstractButton::mousePressEvent(e);
        if (e->button() == Qt::LeftButton) m_press.to(0.84, 110);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        QAbstractButton::mouseReleaseEvent(e);
        m_press.to(1.0, 560, bounce(2.8));
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal d = qMin(width(), height());
        const qreal hv = m_hover.value();
        p.translate(width() / 2.0, height() / 2.0);
        const qreal s = (1.0 + 0.07 * hv) * m_press.value();
        p.scale(s, s);

        QColor col = Theme::mix(Theme::mix(Theme::subtext(), Theme::fg(), hv),
                                Theme::accent(), m_active.value());
        if (m_hasCircle) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::mix(m_circle, m_circle.lighter(112), hv));
            p.drawEllipse(QPointF(0, 0), d / 2.0 - 1, d / 2.0 - 1);
            col = m_glyph;
        }

        const QPen stroke(col, 0.07, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        switch (m_kind) {
        case Prev:
            p.scale(d * 1.15, d * 1.15);
            drawPrev(p, col);
            break;
        case Next:
            p.scale(-d * 1.15, d * 1.15);
            drawPrev(p, col);
            break;

        case Play: {
            const qreal k = d * 0.6;
            p.scale(k, k);
            const qreal m = m_morph.value();
            auto L = [m](QPointF a, QPointF b) { return a + (b - a) * m; };
            QPolygonF left, right;
            left  << L({-0.30, -0.42}, {-0.34, -0.40}) << L({0.04, -0.21}, {-0.08, -0.40})
                  << L({0.04,  0.21}, {-0.08,  0.40}) << L({-0.30,  0.42}, {-0.34,  0.40});
            right << L({0.04, -0.21}, {0.08, -0.40})   << L({0.38, 0.0}, {0.34, -0.40})
                  << L({0.38,  0.0},  {0.34,  0.40})   << L({0.04, 0.21}, {0.08,  0.40});
            p.setPen(QPen(col, 0.07, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(col);
            p.drawPolygon(left);
            p.drawPolygon(right);
            break;
        }

        case Shuffle: {
            p.scale(d, d);
            p.setPen(stroke);
            p.setBrush(Qt::NoBrush);
            QPainterPath a, b;
            a.moveTo(-0.26, 0.12);  a.cubicTo(0.0, 0.12, 0.0, -0.12, 0.15, -0.12);
            b.moveTo(-0.26, -0.12); b.cubicTo(0.0, -0.12, 0.0, 0.12, 0.15, 0.12);
            p.drawPath(a);
            p.drawPath(b);
            p.setBrush(col);
            for (qreal y : { -0.12, 0.12 }) {
                QPolygonF h;
                h << QPointF(0.14, y - 0.06) << QPointF(0.25, y) << QPointF(0.14, y + 0.06);
                p.drawPolygon(h);
            }
            drawDot(p, col);
            break;
        }

        case Repeat: {
            p.scale(d, d);
            p.setPen(stroke);
            p.setBrush(Qt::NoBrush);
            const qreal T = -0.11, B = 0.11, Lx = -0.20, R = 0.20, r = 0.07;
            QPainterPath a, b;
            a.moveTo(Lx, 0.04); a.lineTo(Lx, T + r); a.quadTo(Lx, T, Lx + r, T); a.lineTo(0.12, T);
            b.moveTo(R, -0.04); b.lineTo(R, B - r);  b.quadTo(R, B, R - r, B);   b.lineTo(-0.12, B);
            p.drawPath(a);
            p.drawPath(b);
            p.setBrush(col);
            QPolygonF h1, h2;
            h1 << QPointF(0.10, T - 0.055) << QPointF(0.20, T) << QPointF(0.10, T + 0.055);
            h2 << QPointF(-0.10, B - 0.055) << QPointF(-0.20, B) << QPointF(-0.10, B + 0.055);
            p.drawPolygon(h1);
            p.drawPolygon(h2);
            if (m_one) {
                p.setPen(QPen(col, 0.04, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                QPainterPath one;
                one.moveTo(-0.02, -0.035); one.lineTo(0.012, -0.055); one.lineTo(0.012, 0.055);
                p.drawPath(one);
            }
            drawDot(p, col);
            break;
        }

        case Chevron: {
            p.scale(d * 1.2, d * 1.2);
            p.setPen(stroke);
            p.setBrush(Qt::NoBrush);
            QPainterPath c;
            c.moveTo(-0.14, -0.05); c.lineTo(0.0, 0.07); c.lineTo(0.14, -0.05);
            p.drawPath(c);
            break;
        }

        case Speaker: {
            p.scale(d * 1.1, d * 1.1);
            p.setPen(QPen(col, 0.03, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(col);
            QPolygonF body;
            body << QPointF(-0.26, -0.06) << QPointF(-0.17, -0.06) << QPointF(-0.05, -0.16)
                 << QPointF(-0.05, 0.16) << QPointF(-0.17, 0.06) << QPointF(-0.26, 0.06);
            p.drawPolygon(body);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(col, 0.045, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (m_level <= 0.005) {
                p.drawLine(QPointF(0.05, -0.07), QPointF(0.19, 0.07));
                p.drawLine(QPointF(0.19, -0.07), QPointF(0.05, 0.07));
            } else {
                p.drawArc(QRectF(-0.05 - 0.10, -0.10, 0.20, 0.20), -50 * 16, 100 * 16);
                if (m_level > 0.45)
                    p.drawArc(QRectF(-0.05 - 0.18, -0.18, 0.36, 0.36), -50 * 16, 100 * 16);
            }
            break;
        }

        case Home: {
            p.scale(d * 1.1, d * 1.1);
            p.setPen(QPen(col, 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath h;
            h.moveTo(-0.23, -0.02); h.lineTo(0.0, -0.23); h.lineTo(0.23, -0.02);
            h.moveTo(-0.16, -0.08); h.lineTo(-0.16, 0.21); h.lineTo(-0.05, 0.21); h.lineTo(-0.05, 0.08);
            h.lineTo(0.05, 0.08);   h.lineTo(0.05, 0.21);  h.lineTo(0.16, 0.21);  h.lineTo(0.16, -0.08);
            p.drawPath(h);
            break;
        }

        case Plus: {
            p.scale(d, d);
            p.setPen(QPen(col, 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            if (!m_hasCircle) p.drawEllipse(QPointF(0, 0), 0.30, 0.30);
            p.drawLine(QPointF(-0.12, 0), QPointF(0.12, 0));
            p.drawLine(QPointF(0, -0.12), QPointF(0, 0.12));
            break;
        }

        case More: {
            p.scale(d, d);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            for (qreal x : { -0.13, 0.0, 0.13 }) p.drawEllipse(QPointF(x, 0), 0.036, 0.036);
            break;
        }

        case Lyrics: {            // speech bubble with text lines
            p.scale(d, d);
            p.setPen(QPen(col, 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath b;
            b.addRoundedRect(QRectF(-0.22, -0.19, 0.44, 0.31), 0.07, 0.07);
            b.moveTo(-0.10, 0.12); b.lineTo(-0.10, 0.22); b.lineTo(0.02, 0.12);
            p.drawPath(b);
            p.drawLine(QPointF(-0.12, -0.08), QPointF(0.12, -0.08));
            p.drawLine(QPointF(-0.12, 0.01),  QPointF(0.05, 0.01));
            break;
        }

        case Info: {              // window with a side panel
            p.scale(d, d);
            p.setPen(QPen(col, 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(-0.22, -0.17, 0.44, 0.34), 0.06, 0.06);
            p.drawLine(QPointF(0.06, -0.17), QPointF(0.06, 0.17));
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawRoundedRect(QRectF(0.10, -0.12, 0.08, 0.24), 0.02, 0.02);
            break;
        }
        }
    }

private:
    static void drawPrev(QPainter& p, const QColor& c)
    {
        p.setPen(QPen(c, 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(-0.20, -0.16, 0.055, 0.32), 0.02, 0.02);
        QPolygonF tri;
        tri << QPointF(0.20, -0.17) << QPointF(0.20, 0.17) << QPointF(-0.10, 0.0);
        p.drawPolygon(tri);
    }

    void drawDot(QPainter& p, const QColor& col)
    {
        QColor c = col;
        c.setAlphaF(float(m_active.value()));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(0, 0.30), 0.03, 0.03);
    }

    Kind   m_kind;
    bool   m_hasCircle = false;
    QColor m_circle;
    QColor m_glyph{10, 10, 10};
    bool   m_one = false;
    qreal  m_level = 1.0;
    Tween  m_hover{this};
    Tween  m_press{this, 1.0};
    Tween  m_morph{this};
    Tween  m_active{this};
};

// ============================================================================
//  SmoothSlider: Spotify-style (grows + turns green on hover, handle pops in)
// ============================================================================
class SmoothSlider : public QWidget
{
    Q_OBJECT
public:
    explicit SmoothSlider(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(18);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        bindUpdate(this, { &m_hover, &m_press });
    }

    qreal value() const    { return m_value; }
    bool  dragging() const { return m_drag; }
    void  setValue(qreal v)
    {
        if (m_drag) return;
        m_value = qBound<qreal>(0.0, v, 1.0);
        update();
    }

signals:
    void moved(qreal v);     // while dragging / on click
    void released(qreal v);  // on release (commit)

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 150); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 250); }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_drag = true;
        m_press.to(1.0, 140);
        setFromX(e->position().x());
        emit moved(m_value);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_drag) return;
        setFromX(e->position().x());
        emit moved(m_value);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (!m_drag) return;
        m_drag = false;
        m_press.to(0.0, 260);
        setFromX(e->position().x());
        emit released(m_value);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal hot = qMax(m_hover.value(), m_press.value());
        const qreal pad = 7.0;
        const qreal h = 4.0 + 2.0 * hot;
        QRectF track(pad, height() / 2.0 - h / 2.0, width() - 2 * pad, h);

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::fgA(60));
        p.drawRoundedRect(track, h / 2, h / 2);

        const qreal fw = track.width() * m_value;
        if (fw > 0.0) {
            p.setBrush(Theme::mix(Theme::fg(), Theme::accent(), hot));
            p.drawRoundedRect(QRectF(track.left(), track.top(), qMax(fw, h), h), h / 2, h / 2);
        }
        const qreal hr = 6.5 * hot;
        if (hr > 0.3) {
            const QPointF c(track.left() + fw, height() / 2.0);
            p.setBrush(QColor(0, 0, 0, 70));
            p.drawEllipse(c + QPointF(0, 1), hr + 1.5, hr + 1.5);
            p.setBrush(Theme::fg());
            p.drawEllipse(c, hr, hr);
        }
    }

private:
    void setFromX(qreal x)
    {
        const qreal pad = 7.0;
        m_value = qBound<qreal>(0.0, (x - pad) / qMax<qreal>(1.0, width() - 2 * pad), 1.0);
        update();
    }

    qreal m_value = 0.0;
    bool  m_drag = false;
    Tween m_hover{this};
    Tween m_press{this};
};

// ============================================================================
//  CoverView: rounded artwork with cross-fade, springy scale and soft shadow
// ============================================================================
class CoverView : public QWidget
{
    Q_OBJECT
public:
    CoverView(qreal radius, int margin = 0, QWidget* parent = nullptr)
        : QWidget(parent), m_radius(radius), m_margin(margin)
    {
        bindUpdate(this, { &m_fade, &m_scale });
    }

    void setClickable(bool c) { setCursor(c ? Qt::PointingHandCursor : Qt::ArrowCursor); m_clickable = c; }

    void setPixmap(const QPixmap& pm)
    {
        if (pm.cacheKey() == m_pm.cacheKey()) return;
        m_old = m_pm;
        m_pm = pm;
        m_fade.set(0.0);
        m_fade.to(1.0, 500, QEasingCurve::InOutQuad);
    }

    void setScale(qreal s) { m_scale.to(s, 600, bounce(1.7)); }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_clickable && e->button() == Qt::LeftButton) m_down = true;
        else e->ignore();
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_down && rect().contains(e->position().toPoint())) emit clicked();
        m_down = false;
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF inner = QRectF(rect()).adjusted(m_margin, m_margin, -m_margin, -m_margin);
        const qreal s = m_scale.value();
        p.translate(inner.center());
        p.scale(s, s);
        p.translate(-inner.center());

        if (m_margin > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 14));
            for (int i = 0; i < 10; ++i)
                p.drawRoundedRect(inner.adjusted(-i * 2, -i * 2 + 8, i * 2, i * 2 + 8),
                                  m_radius + i * 2, m_radius + i * 2);
        }
        const qreal f = m_fade.value();
        if (f < 1.0) drawCover(&p, inner, m_old, m_radius);
        p.setOpacity(f);
        drawCover(&p, inner, m_pm, m_radius);
    }

private:
    qreal   m_radius;
    int     m_margin;
    bool    m_clickable = false;
    bool    m_down = false;
    QPixmap m_pm, m_old;
    Tween   m_fade{this, 1.0};
    Tween   m_scale{this, 1.0};
};

// ============================================================================
//  ElidedLabel: single-line label that cross-slides between texts
// ============================================================================
class ElidedLabel : public QWidget
{
public:
    ElidedLabel(int px, QFont::Weight weight, const Theme::Color& color,
                Qt::Alignment align = Qt::AlignLeft, QWidget* parent = nullptr)
        : QWidget(parent), m_font(Theme::font(px, weight)), m_color(color), m_align(align)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(QFontMetrics(m_font).height() + 6);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        bindUpdate(this, { &m_fade });
    }

    QString text() const { return m_text; }
    void setText(const QString& t, bool animate = true)
    {
        if (t == m_text) return;
        m_old = m_text;
        m_text = t;
        if (animate) { m_fade.set(0.0); m_fade.to(1.0, 380, QEasingCurve::OutCubic); }
        else         { m_fade.set(1.0); }
        update();
    }

    QSize sizeHint() const override        { return QSize(200, height()); }
    QSize minimumSizeHint() const override { return QSize(0, height()); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(m_font);
        p.setPen(m_color.get());
        const QFontMetrics fm(m_font);
        const qreal f = m_fade.value();
        auto draw = [&](const QString& s, qreal op, qreal dx) {
            p.setOpacity(op);
            p.drawText(QRectF(dx, 0, width(), height()),
                       int(m_align | Qt::AlignVCenter),
                       fm.elidedText(s, Qt::ElideRight, width()));
        };
        if (f < 1.0) draw(m_old, 1.0 - f, -14.0 * f);
        draw(m_text, f, 14.0 * (1.0 - f));
    }

private:
    QFont         m_font;
    Theme::Color  m_color;
    Qt::Alignment m_align;
    QString       m_text, m_old;
    Tween         m_fade{this, 1.0};
};

// ============================================================================
//  TintWidget: rounded panel whose top gradient glides to the cover's colour
// ============================================================================
class TintWidget : public QWidget
{
public:
    TintWidget(qreal strength, int reach, int radius, QWidget* parent = nullptr)
        : QWidget(parent), m_strength(strength), m_reach(reach), m_radius(radius)
    {
        m_from = m_to = Theme::defaultTint();
        bindUpdate(this, { &m_t });
    }

    void setTint(const QColor& c)
    {
        if (c == m_to) return;
        m_from = current();
        m_to = c;
        m_t.set(0.0);
        m_t.to(1.0, 800, QEasingCurve::InOutQuad);
    }
    QColor current() const { return Theme::mix(m_from, m_to, m_t.value()); }

    // Glass panels let the theme's gradient / image show through; the full-screen sheet stays solid.
    void setSolid(bool solid) { m_solid = solid; update(); }
    void setAlwaysTint(bool on) { m_alwaysTint = on; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), m_radius, m_radius);
        p.setClipPath(path);
        const qreal pa = m_solid ? 1.0 : Theme::panelAlpha();
        QColor base = Theme::bg();
        base.setAlphaF(float(pa));
        p.fillRect(rect(), base);
        if (m_strength > 0.0 && (m_alwaysTint || Theme::cur().coverTint)) {
            const int h = m_reach > 0 ? m_reach : height();
            QColor tint = current();
            if (Theme::isLight()) tint = Theme::mix(tint, QColor(Qt::white), 0.6);
            QColor top = Theme::mix(Theme::bg(), tint, m_strength);
            top.setAlphaF(float(pa));
            QLinearGradient g(0, 0, 0, h);
            g.setColorAt(0, top);
            g.setColorAt(1, base);
            p.fillRect(QRect(0, 0, width(), h), g);
        }
    }

private:
    qreal  m_strength;
    int    m_reach;
    int    m_radius;
    QColor m_from, m_to;
    bool   m_solid = false;
    bool   m_alwaysTint = false;
    Tween  m_t{this, 1.0};
};

// ============================================================================
//  NavButton: sidebar entry with hover pill + press squish
// ============================================================================
class NavButton : public QAbstractButton
{
public:
    enum Kind { Library, AddFiles, AddFolder, Online, Home };

    NavButton(Kind kind, const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_kind(kind)
    {
        setText(text);
        setFixedHeight(46);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        bindUpdate(this, { &m_hover, &m_press });
    }
    void setSelected(bool s) { m_selected = s; update(); }

protected:
    void enterEvent(QEnterEvent*) override { m_hover.to(1.0, 150); }
    void leaveEvent(QEvent*) override      { m_hover.to(0.0, 250); }
    void mousePressEvent(QMouseEvent* e) override
    {
        QAbstractButton::mousePressEvent(e);
        m_press.to(0.96, 100);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        QAbstractButton::mouseReleaseEvent(e);
        m_press.to(1.0, 520, bounce(2.4));
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        QRectF r = QRectF(rect()).adjusted(4, 2, -4, -2);
        p.translate(r.center());
        p.scale(m_press.value(), m_press.value());
        p.translate(-r.center());

        const qreal hv = m_hover.value();
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::fgA(int(26 * hv) + (m_selected ? 14 : 0)));
        p.drawRoundedRect(r, 9, 9);

        const QColor col = m_selected ? Theme::fg()
                                      : Theme::mix(Theme::subtext(), Theme::fg(), hv);
        // icon in a 22x22 box
        p.save();
        p.translate(r.left() + 14, r.center().y() - 11);
        p.setPen(QPen(col, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (m_kind) {
        case Library:
            p.setBrush(col);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(3, 6, 3.5, 13), 1.5, 1.5);
            p.drawRoundedRect(QRectF(9, 3, 3.5, 16), 1.5, 1.5);
            p.drawRoundedRect(QRectF(15, 8, 3.5, 11), 1.5, 1.5);
            break;
        case AddFiles:
            p.drawRoundedRect(QRectF(2.5, 2.5, 17, 17), 5, 5);
            p.drawLine(QPointF(11, 6.5), QPointF(11, 15.5));
            p.drawLine(QPointF(6.5, 11), QPointF(15.5, 11));
            break;
        case AddFolder: {
            QPainterPath f;
            f.moveTo(2.5, 5.5); f.lineTo(8.5, 5.5); f.lineTo(10.5, 8);
            f.lineTo(19.5, 8);  f.lineTo(19.5, 17.5); f.lineTo(2.5, 17.5); f.closeSubpath();
            p.drawPath(f);
            break;
        }
        case Online:            // magnifier
            p.drawEllipse(QRectF(3, 3, 12.5, 12.5));
            p.drawLine(QPointF(13.2, 13.2), QPointF(19, 19));
            break;
        case Home: {
            QPainterPath h;
            h.moveTo(2.5, 10.5); h.lineTo(11, 3.2); h.lineTo(19.5, 10.5);
            h.moveTo(5, 9);      h.lineTo(5, 19);   h.lineTo(9.3, 19);  h.lineTo(9.3, 13.2);
            h.lineTo(12.7, 13.2); h.lineTo(12.7, 19); h.lineTo(17, 19); h.lineTo(17, 9);
            p.drawPath(h);
            break;
        }
        }
        p.restore();

        p.setFont(Theme::font(15, QFont::Medium));
        p.setPen(col);
        p.drawText(QRectF(r.left() + 50, r.top(), r.width() - 50, r.height()),
                   int(Qt::AlignVCenter | Qt::AlignLeft), text());
    }

private:
    Kind  m_kind;
    bool  m_selected = false;
    Tween m_hover{this};
    Tween m_press{this, 1.0};
};

// ============================================================================
//  NowPlayingSheet: full-screen sheet that slides up (and can be dragged down).
//  It has two looks that glide into each other: big cover, or cover on the left
//  with the lyrics on the right.
// ============================================================================
class NowPlayingSheet : public TintWidget
{
    Q_OBJECT
public:
    explicit NowPlayingSheet(QWidget* host)
        : TintWidget(1.0, 0, 12, host), m_host(host)
    {
        setSolid(true);
        setAlwaysTint(true);
        hide();
        host->installEventFilter(this);

        m_close = new IconButton(IconButton::Chevron, 44, this);
        auto* hdr = new ElidedLabel(12, QFont::DemiBold, Theme::Color::fgAlpha(170), Qt::AlignCenter, this);
        hdr->setText(QStringLiteral("NOW PLAYING"), false);

        m_body = new QWidget(this);
        m_body->installEventFilter(this);
        m_cover  = new CoverView(16, 44, m_body);
        m_title  = new ElidedLabel(30, QFont::Bold, Theme::Color::fgRole(), Qt::AlignCenter, m_body);
        m_artist = new ElidedLabel(17, QFont::Medium, Theme::Color::fgAlpha(170), Qt::AlignCenter, m_body);

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(24, 18, 24, 28);
        auto* top = new QHBoxLayout;
        top->addWidget(m_close);
        top->addWidget(hdr, 1);
        top->addSpacing(44);
        v->addLayout(top);
        v->addWidget(m_body, 1);

        connect(m_close, &QAbstractButton::clicked, this, [this] { closeSheet(); });
        connect(&m_pos, &Tween::changed, this, [this] {
            relayout();
            if (m_pos.value() <= 0.0005 && m_pos.target() == 0.0) hide();
        });
        connect(&m_lyr, &Tween::changed, this, [this] { layoutBody(); });
    }

    bool isOpen() const { return m_pos.target() > 0.5; }
    bool lyricsMode() const { return m_lyr.target() > 0.5; }

    // Slides the sheet up. `lyrics` picks the look it opens in.
    void openSheet(bool lyrics = false)
    {
        if (!isOpen()) m_lyr.set(lyrics ? 1.0 : 0.0);
        else           m_lyr.to(lyrics ? 1.0 : 0.0, 520, QEasingCurve::OutCubic);
        layoutBody();
        relayout();
        show();
        raise();
        m_pos.to(1.0, 540, QEasingCurve::OutCubic);
        emit stateChanged();
    }
    void closeSheet()
    {
        m_pos.to(0.0, 380, QEasingCurve::InOutCubic);
        emit stateChanged();
    }
    void setLyricsMode(bool on)
    {
        m_lyr.to(on ? 1.0 : 0.0, 520, QEasingCurve::OutCubic);
        emit stateChanged();
    }

    void setTrack(const QString& title, const QString& artist, const QPixmap& cover, const QColor& tint)
    {
        m_title->setText(title);
        m_artist->setText(artist);
        m_cover->setPixmap(cover);
        setTint(tint);
    }
    void setPlaying(bool p) { m_cover->setScale(p ? 1.0 : 0.84); }

    // The lyrics widget lives on the right of the sheet while in lyrics mode.
    void addLyricsView(QWidget* lv)
    {
        if (!lv) return;
        m_lyrics = lv;
        lv->setParent(m_body);
        lv->hide();
        layoutBody();
    }

signals:
    void stateChanged();

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_host && e->type() == QEvent::Resize) relayout();
        else if (o == m_body && e->type() == QEvent::Resize) layoutBody();
        return QWidget::eventFilter(o, e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        TintWidget::resizeEvent(e);
        layoutBody();
    }

    // Drag down to dismiss, like an iOS sheet.
    void mousePressEvent(QMouseEvent* e) override
    {
        m_dragging = true;
        m_dragY = e->globalPosition().y();
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_dragging) return;
        qreal dy = e->globalPosition().y() - m_dragY;
        if (dy < 0) dy = 0;
        m_pos.set(1.0 - dy / qMax(1, m_host->height()));
    }
    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (!m_dragging) return;
        m_dragging = false;
        if (m_pos.value() < 0.86) closeSheet();
        else m_pos.to(1.0, 520, bounce(1.8));
    }

private:
    void relayout()
    {
        const QRect area = m_host->rect().marginsRemoved(QMargins(8, 8, 8, 0));
        const int off = int((1.0 - qBound(0.0, m_pos.value(), 1.0)) * m_host->height());
        setGeometry(area.translated(0, off));
    }

    // Positions cover, title, artist and lyrics for the current blend (m_lyr: 0 = cover only, 1 = with lyrics).
    void layoutBody()
    {
        if (!m_body) return;
        const int W = m_body->width(), H = m_body->height();
        if (W < 10 || H < 10) return;
        const qreal t = qBound(0.0, m_lyr.value(), 1.0);
        const int th = m_title->height(), ah = m_artist->height();
        auto lerp = [t](qreal a, qreal b) { return a + (b - a) * t; };

        // look 0: big centred cover
        const qreal a0 = qBound(180.0, qMin(W * 0.55, H * 0.46), 560.0);
        const qreal tw0 = qMin(W - 48, 900);
        const qreal stack0 = (a0 + 88) + 8 + th + 2 + ah;
        // look 1: cover column on the left, lyrics on the right
        const qreal lw = qBound<qreal>(240.0, W * 0.38, 460.0);
        const qreal a1 = qBound<qreal>(110.0, qMin(lw - 88, H * 0.5), 560.0);
        const qreal tw1 = lw - 16;
        const qreal stack1 = (a1 + 88) + 8 + th + 2 + ah;

        const qreal a = lerp(a0, a1), cx = lerp(W / 2.0, lw / 2.0), tw = lerp(tw0, tw1);
        const qreal y = lerp((H - stack0) / 2.0, (H - stack1) / 2.0);
        const int cs = int(a + 88);
        m_cover->setGeometry(int(cx - cs / 2.0), int(y), cs, cs);
        m_title->setGeometry(int(cx - tw / 2.0), int(y) + cs + 8, int(tw), th);
        m_artist->setGeometry(int(cx - tw / 2.0), int(y) + cs + 8 + th + 2, int(tw), ah);

        if (m_lyrics) {
            const int lx = int(lw) + 16;
            const int fx = int(lerp(W, lx));
            m_lyrics->setGeometry(fx, 0, qMax(100, W - lx - 8), H);
            m_lyrics->setVisible(t > 0.001);
        }
    }

    QWidget*     m_host;
    QWidget*     m_body = nullptr;
    QWidget*     m_lyrics = nullptr;
    IconButton*  m_close;
    CoverView*   m_cover;
    ElidedLabel* m_title;
    ElidedLabel* m_artist;
    Tween        m_pos{this, 0.0};
    Tween        m_lyr{this, 0.0};
    bool         m_dragging = false;
    qreal        m_dragY = 0;
};
