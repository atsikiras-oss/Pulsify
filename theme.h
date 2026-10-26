#pragma once
#include <QtWidgets>
#include <cmath>

// ============================================================================
//  Theme engine
//
//  * Theme::Data describes a theme: base colour, accent, optional multi-stop
//    gradient, "glass" panel opacity and an optional custom background image.
//  * Light or dark text is chosen automatically from the base colour.
//  * Every stylesheet in the app is written once in the original dark palette and
//    passed through Theme::skin() (see sty()), so a theme change re-skins
//    everything, including widgets created later.
//  * Painted widgets read Theme::fg() / bg() / accent() / greyAt() while painting.
// ============================================================================
namespace Theme {

inline QColor mix(const QColor& a, const QColor& b, qreal t)
{
    t = qBound<qreal>(0.0, t, 1.0);
    return QColor::fromRgbF(float(a.redF()   + (b.redF()   - a.redF())   * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF()  + (b.blueF()  - a.blueF())  * t),
                            float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

inline qreal luminance(const QColor& c)
{
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

struct Data {
    QString id = QStringLiteral("pulsify");
    QString name = QStringLiteral("Pulsify Dark");
    QColor  bg{0x12, 0x12, 0x12};          // panel colour; decides light / dark text
    QColor  accent{0x1D, 0xB9, 0x54};
    QVector<QColor> grad;                  // 2+ stops = gradient behind the panels
    int     angle = 160;                   // degrees: 0 = left to right, 90 = top to bottom
    bool    coverTint = true;              // tint pages with the current album art
    int     panel = 100;                   // panel opacity (%) - below 100 lets the backdrop show through

    // user background (independent of the chosen theme)
    QString image;
    int     blur = 12;                     // 0..40
    int     dim = 45;                      // 0..90 (%)
    int     imgPanel = 62;                 // panel opacity (%) while an image is set
};

inline Data& cur() { static Data d; return d; }

inline bool isLight()          { return luminance(cur().bg) > 0.55; }
inline bool hasImage()         { return !cur().image.isEmpty(); }
inline bool hasBackdrop()      { return hasImage() || cur().grad.size() >= 2; }
inline qreal panelAlpha()      { return qBound(0.15, (hasImage() ? cur().imgPanel : cur().panel) / 100.0, 1.0); }

inline QColor bg()             { return cur().bg; }
inline QColor accent()         { return cur().accent; }
inline QColor fg()             { return isLight() ? QColor(0x16, 0x16, 0x16) : QColor(Qt::white); }
inline QColor fgA(int a)       { QColor c = fg(); c.setAlpha(qBound(0, a, 255)); return c; }
inline QColor contrast(const QColor& c) { return luminance(c) > 0.5 ? QColor(10, 10, 10) : QColor(Qt::white); }
inline QColor onFg()           { return contrast(fg()); }
inline QColor onAccent()       { return contrast(accent()); }
inline QColor accentHover()    { return mix(accent(), QColor(Qt::white), 0.12); }
inline QColor accentDark()     { return mix(accent(), QColor(Qt::black), 0.18); }
inline QColor accentA(int a)   { QColor c = accent(); c.setAlpha(qBound(0, a, 255)); return c; }

// A grey between the panel colour (0) and the text colour (1). The original dark UI used fixed greys;
// L = (grey - 0x12) / (0xff - 0x12) keeps them identical in the default theme.
inline QColor greyAt(qreal l)  { return mix(bg(), fg(), l); }
inline QColor subtext()        { return greyAt(0.64); }

// The colour behind / between the panels when there is no gradient or image.
inline QColor canvas()         { return isLight() ? mix(bg(), QColor(Qt::black), 0.07) : QColor(Qt::black); }

// A colour that is either fixed or follows the theme (used by labels that are created once).
struct Color {
    enum Kind { Fixed, Fg, Sub, FgA };
    Kind   kind = Fixed;
    QColor c;
    int    a = 255;
    Color(const QColor& q) : c(q) {}
    Color(Qt::GlobalColor g) : c(g) {}
    static Color fgRole()            { Color k{QColor()}; k.kind = Fg;  return k; }
    static Color subRole()           { Color k{QColor()}; k.kind = Sub; return k; }
    static Color fgAlpha(int alpha)  { Color k{QColor()}; k.kind = FgA; k.a = alpha; return k; }
    QColor get() const
    {
        switch (kind) {
        case Fg:  return fg();
        case Sub: return subtext();
        case FgA: return fgA(a);
        default:  return c;
        }
    }
};

inline QFont font(int px, QFont::Weight w = QFont::Normal)
{
    QFont f;
    f.setFamilies({ "Segoe UI Variable Text", "Segoe UI", "Inter", "SF Pro Text",
                    "Helvetica Neue", "Ubuntu", "Arial" });
    f.setPixelSize(px);
    f.setWeight(w);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// Pleasant, dark-ish tint derived from cover art (used for the page gradients).
inline QColor defaultTint() { return QColor(0x1f, 0x4d, 0x35); }

inline QColor tintFrom(const QImage& src)
{
    if (src.isNull()) return defaultTint();
    QImage s = src.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGB32);
    qint64 r = 0, g = 0, b = 0;
    for (int y = 0; y < s.height(); ++y)
        for (int x = 0; x < s.width(); ++x) {
            QRgb c = s.pixel(x, y);
            r += qRed(c); g += qGreen(c); b += qBlue(c);
        }
    const int n = s.width() * s.height();
    QColor c(int(r / n), int(g / n), int(b / n));
    float h = float(c.hsvHueF());
    float sat = float(c.hsvSaturationF());
    float v = float(c.valueF());
    if (h < 0) h = 0;
    sat = qMin(1.0f, sat * 1.25f);
    v = qBound(0.32f, v, 0.62f);
    return QColor::fromHsvF(h, sat, v);
}

// ---------------------------------------------------------------- gradient helper
inline QLinearGradient gradientFor(const QRectF& r, const QVector<QColor>& stops, int angleDeg)
{
    const qreal a = qDegreesToRadians(qreal(angleDeg));
    const QPointF dir(std::cos(a), std::sin(a));
    const qreal len = std::abs(r.width() * dir.x()) + std::abs(r.height() * dir.y());
    const QPointF c = r.center();
    QLinearGradient g(c - dir * (len / 2.0), c + dir * (len / 2.0));
    const int n = int(stops.size());
    for (int i = 0; i < n; ++i) g.setColorAt(n > 1 ? qreal(i) / (n - 1) : 0.0, stops[i]);
    return g;
}

// ---------------------------------------------------------------- presets
inline Data makeTheme(const char* id, const char* name, const char* bg, const char* accent,
                      std::initializer_list<const char*> grad = {}, int angle = 160,
                      bool tint = true, int panel = 100)
{
    Data d;
    d.id = QString::fromLatin1(id);
    d.name = QString::fromUtf8(name);
    d.bg = QColor(QLatin1String(bg));
    d.accent = QColor(QLatin1String(accent));
    for (const char* g : grad) d.grad.push_back(QColor(QLatin1String(g)));
    d.angle = angle;
    d.coverTint = tint;
    d.panel = panel;
    return d;
}

inline const QVector<Data>& presets()
{
    static const QVector<Data> list = {
        makeTheme("pulsify",  "Pulsify Dark", "#121212", "#1DB954"),
        makeTheme("amoled",   "AMOLED Black", "#000000", "#2EE66B", {}, 160, false),
        makeTheme("midnight", "Midnight",     "#0b1020", "#5B8CFF"),
        makeTheme("graphite", "Graphite",     "#1b1d21", "#f2b84b"),
        makeTheme("aurora",   "Aurora",       "#0a1a1c", "#3EE6B0", { "#0b5d57", "#1d8a6b", "#3a2a8f" }, 135, false, 58),
        makeTheme("sunset",   "Sunset",       "#1c0f1a", "#FF8A5B", { "#ff7e5f", "#d4407c", "#5b2a86" }, 150, false, 56),
        makeTheme("ocean",    "Ocean",        "#08141f", "#36C5F0", { "#0a4d80", "#1b8fd1", "#0b1f3a" }, 160, false, 58),
        makeTheme("rose",     "Rose",         "#1f0d14", "#FF5C8A", { "#ff5c8a", "#b0245a", "#3b0d2a" }, 145, false, 56),
        makeTheme("neon",     "Neon Nights",  "#0e0a1f", "#B86BFF", { "#3a0ca3", "#7209b7", "#f72585" }, 135, false, 56),
        makeTheme("forest",   "Forest",       "#0c150f", "#7BD88F", { "#1b5e3b", "#2d8a5f", "#081c15" }, 165, false, 58),
        makeTheme("ember",    "Ember",        "#160d0a", "#FFB020", { "#8a2b0b", "#d9541e", "#1a0a05" }, 155, false, 56),
        makeTheme("daylight", "Daylight",     "#f4f5f7", "#1DB954"),
        makeTheme("peach",    "Peach Fizz",   "#fff4ee", "#FF6B4A", { "#ffd2bd", "#ffb8d1", "#c9d4ff" }, 150, false, 62),
        makeTheme("mint",     "Mint Cream",   "#f1faf6", "#12a37a", { "#c6f1de", "#d8f0ff", "#f4f1ff" }, 140, false, 64),
    };
    return list;
}

inline const Data* findPreset(const QString& id)
{
    for (const Data& d : presets()) if (d.id == id) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- persistence
inline QJsonObject toJson(const Data& d)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), d.id);
    o.insert(QStringLiteral("name"), d.name);
    o.insert(QStringLiteral("bg"), d.bg.name());
    o.insert(QStringLiteral("accent"), d.accent.name());
    QJsonArray g;
    for (const QColor& c : d.grad) g.push_back(c.name());
    o.insert(QStringLiteral("grad"), g);
    o.insert(QStringLiteral("angle"), d.angle);
    o.insert(QStringLiteral("tint"), d.coverTint);
    o.insert(QStringLiteral("panel"), d.panel);
    return o;
}

inline Data fromJson(const QJsonObject& o)
{
    Data d;
    d.id = o.value(QLatin1String("id")).toString(QStringLiteral("custom"));
    d.name = o.value(QLatin1String("name")).toString(QStringLiteral("Custom"));
    const QColor bg(o.value(QLatin1String("bg")).toString());
    const QColor ac(o.value(QLatin1String("accent")).toString());
    if (bg.isValid()) d.bg = bg;
    if (ac.isValid()) d.accent = ac;
    for (const QJsonValue& v : o.value(QLatin1String("grad")).toArray()) {
        const QColor c(v.toString());
        if (c.isValid()) d.grad.push_back(c);
    }
    if (d.grad.size() < 2) d.grad.clear();
    d.angle = o.value(QLatin1String("angle")).toInt(160);
    d.coverTint = o.value(QLatin1String("tint")).toBool(true);
    d.panel = qBound(15, o.value(QLatin1String("panel")).toInt(100), 100);
    return d;
}

inline void load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("theme"));
    const QString id = s.value(QStringLiteral("id"), QStringLiteral("pulsify")).toString();
    Data d;
    if (id == QLatin1String("custom"))
        d = fromJson(QJsonDocument::fromJson(s.value(QStringLiteral("custom")).toByteArray()).object());
    else if (const Data* p = findPreset(id))
        d = *p;
    d.image = s.value(QStringLiteral("image")).toString();
    if (!d.image.isEmpty() && !QFileInfo::exists(d.image)) d.image.clear();
    d.blur = qBound(0, s.value(QStringLiteral("blur"), 12).toInt(), 40);
    d.dim = qBound(0, s.value(QStringLiteral("dim"), 45).toInt(), 90);
    d.imgPanel = qBound(15, s.value(QStringLiteral("imgPanel"), 62).toInt(), 100);
    cur() = d;
}

inline void save()
{
    const Data& d = cur();
    QSettings s;
    s.beginGroup(QStringLiteral("theme"));
    s.setValue(QStringLiteral("id"), d.id);
    if (d.id == QLatin1String("custom"))
        s.setValue(QStringLiteral("custom"), QJsonDocument(toJson(d)).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("image"), d.image);
    s.setValue(QStringLiteral("blur"), d.blur);
    s.setValue(QStringLiteral("dim"), d.dim);
    s.setValue(QStringLiteral("imgPanel"), d.imgPanel);
}

// ---------------------------------------------------------------- stylesheet skinning
// Maps one hex colour from the original dark stylesheets onto the current theme.
inline QString mapHex(const QString& hex)
{
    QString h = hex.toLower();
    if (h.size() == 3) h = QString(h[0]) + h[0] + h[1] + h[1] + h[2] + h[2];
    if (h == QLatin1String("1db954")) return accent().name();
    if (h == QLatin1String("1ed760")) return accentHover().name();
    if (h == QLatin1String("169c46")) return accentDark().name();
    bool ok1 = false, ok2 = false, ok3 = false;
    const int r = h.mid(0, 2).toInt(&ok1, 16), g = h.mid(2, 2).toInt(&ok2, 16), b = h.mid(4, 2).toInt(&ok3, 16);
    if (ok1 && ok2 && ok3 && r == g && g == b)
        return greyAt(qBound<qreal>(0.0, (r - 18) / 237.0, 1.0)).name();
    return QStringLiteral("#") + hex;
}

inline QString skin(const QString& in)
{
    QString css = in;

    static const QRegularExpression accPair(QStringLiteral(R"((background\s*:\s*#1[dD][bB]954\s*;\s*color\s*:\s*)black)"));
    css.replace(accPair, QStringLiteral("\\1") + onAccent().name());

    static const QRegularExpression whiteRgba(QStringLiteral(R"(rgba\(\s*255\s*,\s*255\s*,\s*255\s*,)"));
    const QColor f = fg();
    css.replace(whiteRgba, QStringLiteral("rgba(%1,%2,%3,").arg(f.red()).arg(f.green()).arg(f.blue()));

    static const QRegularExpression hexRe(QStringLiteral(R"(#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-fA-F]))"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = hexRe.globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += css.mid(last, int(m.capturedStart()) - last);
        out += mapHex(m.captured(1));
        last = int(m.capturedEnd());
    }
    out += css.mid(last);
    css = out;

    static const QRegularExpression whiteRe(QStringLiteral(R"((?<![\w#-])white(?![\w-]))"));
    static const QRegularExpression blackRe(QStringLiteral(R"((?<![\w#-])black(?![\w-]))"));
    css.replace(whiteRe, fg().name());
    css.replace(blackRe, onFg().name());
    return css;
}

// Use instead of QWidget::setStyleSheet: remembers the original text so a theme change can re-skin it.
inline void sty(QWidget* w, const QString& css)
{
    if (!w) return;
    w->setProperty("_pcss", css);
    w->setStyleSheet(skin(css));
}
inline QString origSheet(const QWidget* w) { return w->property("_pcss").toString(); }

} // namespace Theme

using Theme::sty;

// ============================================================================
//  Change notifications + applying a theme app-wide
// ============================================================================
class ThemeBus : public QObject
{
    Q_OBJECT
signals:
    void changed();
};

inline ThemeBus& themeBus() { static ThemeBus b; return b; }

namespace Theme {

inline QString& appSheetRaw() { static QString s; return s; }
inline void setAppSheet(const QString& s) { appSheetRaw() = s; }

// The app icon (also used by every dialog's title bar), drawn in the accent colour.
inline QIcon appIcon()
{
    QPixmap pm(256, 256);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(accent());
    p.drawRoundedRect(QRectF(8, 8, 240, 240), 56, 56);
    p.setBrush(onAccent());
    const qreal hs[] = { 70, 130, 170, 100, 56 };
    for (int i = 0; i < 5; ++i) {
        const qreal x = 52 + i * 34, h = hs[i];
        p.drawRoundedRect(QRectF(x, 128 - h / 2, 20, h), 10, 10);
    }
    p.end();
    return QIcon(pm);
}

// full = false: only repaint (panel opacity, blur, gradient colours...) - nothing needs re-skinning.
inline void apply(bool full = true)
{
    if (!qApp) return;
    if (!full) {
        const QList<QWidget*> all = qApp->allWidgets();
        for (QWidget* w : all) w->update();
        emit themeBus().changed();
        return;
    }
    QPalette pal;
    pal.setColor(QPalette::Window, bg());
    pal.setColor(QPalette::WindowText, fg());
    pal.setColor(QPalette::Base, greyAt(0.05));
    pal.setColor(QPalette::AlternateBase, greyAt(0.076));
    pal.setColor(QPalette::Text, fg());
    pal.setColor(QPalette::PlaceholderText, greyAt(0.574));
    pal.setColor(QPalette::Button, greyAt(0.10));
    pal.setColor(QPalette::ButtonText, fg());
    pal.setColor(QPalette::Highlight, accent());
    pal.setColor(QPalette::HighlightedText, onAccent());
    pal.setColor(QPalette::ToolTipBase, greyAt(0.093));
    pal.setColor(QPalette::ToolTipText, fg());
    pal.setColor(QPalette::Link, isLight() ? accentDark() : accentHover());
    qApp->setPalette(pal);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    qApp->styleHints()->setColorScheme(isLight() ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
#endif
    qApp->setStyleSheet(skin(appSheetRaw()));
    qApp->setWindowIcon(appIcon());

    const QList<QWidget*> all = qApp->allWidgets();
    for (QWidget* w : all) {
        const QString orig = origSheet(w);
        if (!orig.isEmpty()) w->setStyleSheet(skin(orig));
        w->update();
    }
    emit themeBus().changed();
}

inline void setTheme(const Data& d, bool persist = true, bool full = true)
{
    cur() = d;
    if (persist) save();
    apply(full);
}

} // namespace Theme

// ============================================================================
//  ThemedCentral: paints the window backdrop (flat canvas, gradient or custom image)
// ============================================================================
class ThemedCentral : public QWidget
{
public:
    explicit ThemedCentral(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        connect(&themeBus(), &ThemeBus::changed, this, [this] { m_key.clear(); update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Theme::Data& d = Theme::cur();
        p.fillRect(rect(), Theme::canvas());
        if (!d.image.isEmpty()) {
            ensureImage();
            if (!m_pix.isNull()) {
                p.drawPixmap(0, 0, m_pix);
                QColor dim = Theme::isLight() ? QColor(255, 255, 255) : QColor(0, 0, 0);
                dim.setAlphaF(d.dim / 100.0);
                p.fillRect(rect(), dim);
                return;
            }
        }
        if (d.grad.size() >= 2)
            p.fillRect(rect(), Theme::gradientFor(QRectF(rect()), d.grad, d.angle));
    }

private:
    void ensureImage()
    {
        const Theme::Data& d = Theme::cur();
        const qreal dpr = devicePixelRatioF();
        const QString key = QStringLiteral("%1|%2x%3|%4|%5").arg(d.image).arg(width()).arg(height()).arg(d.blur).arg(dpr);
        if (key == m_key) return;
        m_key = key;
        if (m_srcPath != d.image) {
            m_srcPath = d.image;
            QImageReader rd(d.image);
            rd.setAutoTransform(true);
            const QSize sz = rd.size();
            if (sz.isValid() && (sz.width() > 3200 || sz.height() > 3200))
                rd.setScaledSize(sz.scaled(3200, 3200, Qt::KeepAspectRatio));
            m_src = rd.read();
        }
        m_pix = QPixmap();
        if (m_src.isNull() || width() < 2 || height() < 2) return;
        const QSize target = (QSizeF(size()) * dpr).toSize();
        QImage img = m_src.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        img = img.copy((img.width() - target.width()) / 2, (img.height() - target.height()) / 2,
                       target.width(), target.height());
        if (d.blur > 0) {                       // cheap blur: shrink and enlarge in two soft steps
            const qreal f = std::sqrt(1.0 + d.blur * 0.5);
            for (int i = 0; i < 2; ++i) {
                const QSize small(qMax(2, int(img.width() / f)), qMax(2, int(img.height() / f)));
                img = img.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            img = img.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        m_pix = QPixmap::fromImage(img);
        m_pix.setDevicePixelRatio(dpr);
    }

    QString m_key, m_srcPath;
    QImage  m_src;
    QPixmap m_pix;
};

// Slight scrim behind the player bar when there is a gradient / image backdrop.
class PlayerBarBg : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!Theme::hasBackdrop()) return;
        QPainter p(this);
        QColor c = Theme::bg();
        c.setAlphaF(qMin(0.92, Theme::panelAlpha() + 0.12));
        p.fillRect(rect(), c);
    }
};
