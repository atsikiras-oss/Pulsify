#pragma once
#include <QtWidgets>
#include <functional>
#include "theme.h"
#include "importdialog.h"      // pulseDialogStyle()

// ============================================================================
//  Themes dialog: presets, gradients, colours, panel glass and a custom background image.
//  Everything applies live; "Done" keeps it, closing / Cancel puts the old look back.
// ============================================================================

// A clickable preview card for one theme.
class ThemeSwatch : public QAbstractButton
{
public:
    explicit ThemeSwatch(const Theme::Data& d, QWidget* parent = nullptr) : QAbstractButton(parent), m_d(d)
    {
        setCheckable(true);
        setFixedSize(144, 108);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }
    void setData(const Theme::Data& d) { m_d = d; update(); }
    const Theme::Data& data() const { return m_d; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(3, 3, -3, -3);
        const QRectF pv(card.left(), card.top(), card.width(), 76);

        p.save();
        QPainterPath clip;
        clip.addRoundedRect(pv, 12, 12);
        p.setClipPath(clip);
        const bool light = Theme::luminance(m_d.bg) > 0.55;
        if (m_d.grad.size() >= 2) p.fillRect(pv, Theme::gradientFor(pv, m_d.grad, m_d.angle));
        else p.fillRect(pv, light ? Theme::mix(m_d.bg, Qt::black, 0.07) : QColor(Qt::black));

        // a miniature panel
        const QRectF panel = pv.adjusted(10, 10, -10, -10);
        QColor pc = m_d.bg;
        pc.setAlphaF(m_d.grad.size() >= 2 ? qBound(0.35, m_d.panel / 100.0, 1.0) : 1.0);
        p.setPen(Qt::NoPen);
        p.setBrush(pc);
        p.drawRoundedRect(panel, 8, 8);
        const QColor f = light ? QColor(0x16, 0x16, 0x16) : QColor(Qt::white);
        QColor fl = f; fl.setAlpha(190);
        QColor fl2 = f; fl2.setAlpha(90);
        p.setBrush(fl);
        p.drawRoundedRect(QRectF(panel.left() + 10, panel.top() + 10, 46, 6), 3, 3);
        p.setBrush(fl2);
        p.drawRoundedRect(QRectF(panel.left() + 10, panel.top() + 22, 30, 5), 2.5, 2.5);
        p.setBrush(m_d.accent);
        p.drawEllipse(QPointF(panel.right() - 18, panel.bottom() - 16), 9, 9);
        p.restore();

        p.setPen(QPen(isChecked() ? Theme::accent() : Theme::fgA(40), isChecked() ? 2.5 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(pv.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

        p.setPen(isChecked() ? Theme::fg() : Theme::subtext());
        QFont nf = Theme::font(13, isChecked() ? QFont::DemiBold : QFont::Medium);
        p.setFont(nf);
        p.drawText(QRectF(card.left(), pv.bottom() + 4, card.width(), 22), Qt::AlignCenter,
                   QFontMetrics(nf).elidedText(m_d.name, Qt::ElideRight, int(card.width())));
    }

private:
    Theme::Data m_d;
};

// A button that shows a colour and opens the colour picker.
class ColorButton : public QPushButton
{
public:
    explicit ColorButton(const QString& label, QWidget* parent = nullptr) : QPushButton(label, parent), m_label(label)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoDefault(false);
        setFixedHeight(36);
        sty(this, QStringLiteral("QPushButton{padding:0 18px 0 40px;text-align:left;}"));
        connect(this, &QPushButton::clicked, this, [this] {
            const QColor c = QColorDialog::getColor(m_c, window(), m_label);
            if (!c.isValid() || c == m_c) return;
            m_c = c;
            update();
            if (onPicked) onPicked(c);
        });
    }
    void setColor(const QColor& c) { m_c = c; update(); }
    QColor color() const { return m_c; }
    std::function<void(const QColor&)> onPicked;

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QPushButton::paintEvent(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::fgA(110), 1.0));
        p.setBrush(m_c);
        p.drawEllipse(QRectF(12, height() / 2.0 - 9, 18, 18));
    }

private:
    QString m_label;
    QColor  m_c{Qt::gray};
};

class ThemeDialog : public QDialog
{
public:
    explicit ThemeDialog(QWidget* parent) : QDialog(parent), m_orig(Theme::cur()), m_d(Theme::cur())
    {
        setWindowTitle(QStringLiteral("Themes"));
        setMinimumSize(640, 540);
        resize(780, 780);
        sty(this, pulseDialogStyle());

        // the custom slot starts from what the user saved before, or from the current look
        {
            QSettings s;
            s.beginGroup(QStringLiteral("theme"));
            const QByteArray js = s.value(QStringLiteral("custom")).toByteArray();
            m_custom = js.isEmpty() ? m_d : Theme::fromJson(QJsonDocument::fromJson(js).object());
            m_custom.id = QStringLiteral("custom");
            m_custom.name = QStringLiteral("Custom");
            if (m_d.id == QLatin1String("custom")) m_custom = m_d;
        }
        seedGradient();

        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { flush(); });

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(26, 22, 26, 18);
        root->setSpacing(10);

        auto* h = new QLabel(QStringLiteral("Themes"));
        sty(h, QStringLiteral("font-size:24px;font-weight:800;color:white;background:transparent;"));
        auto* sub = new QLabel(QStringLiteral("Pick a theme, then fine-tune it. Everything updates live."));
        sty(sub, QStringLiteral("font-size:13px;color:#b3b3b3;background:transparent;"));
        root->addWidget(h);
        root->addWidget(sub);

        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->viewport()->setAutoFillBackground(false);
        auto* body = new QWidget;
        body->setAutoFillBackground(false);
        auto* bl = new QVBoxLayout(body);
        bl->setContentsMargins(0, 6, 8, 6);
        bl->setSpacing(10);
        scroll->setWidget(body);
        root->addWidget(scroll, 1);

        // ---- presets
        bl->addWidget(section(QStringLiteral("Themes")));
        auto* grid = new QGridLayout;
        grid->setSpacing(10);
        grid->setContentsMargins(0, 0, 0, 0);
        int i = 0;
        for (const Theme::Data& p : Theme::presets()) {
            auto* sw = new ThemeSwatch(p);
            m_swatches.push_back(sw);
            grid->addWidget(sw, i / 4, i % 4);
            const QString id = p.id;
            connect(sw, &QAbstractButton::clicked, this, [this, id] { pickPreset(id); });
            ++i;
        }
        m_customSwatch = new ThemeSwatch(m_custom);
        grid->addWidget(m_customSwatch, i / 4, i % 4);
        connect(m_customSwatch, &QAbstractButton::clicked, this, [this] { pickPreset(QStringLiteral("custom")); });
        grid->setColumnStretch(4, 1);
        bl->addLayout(grid);

        // ---- colours
        bl->addWidget(section(QStringLiteral("Colors")));
        auto* colRow = new QHBoxLayout;
        colRow->setSpacing(10);
        m_accentBtn = new ColorButton(QStringLiteral("Accent"));
        m_bgBtn = new ColorButton(QStringLiteral("Background"));
        colRow->addWidget(m_accentBtn);
        colRow->addWidget(m_bgBtn);
        colRow->addStretch(1);
        bl->addLayout(colRow);
        m_accentBtn->onPicked = [this](const QColor& c) { toCustom(); m_d.accent = c; changed(true); };
        m_bgBtn->onPicked = [this](const QColor& c) { toCustom(); m_d.bg = c; changed(true); };

        m_tint = new QCheckBox(QStringLiteral("Tint pages with the colours of the current album art"));
        bl->addWidget(m_tint);
        connect(m_tint, &QCheckBox::toggled, this, [this](bool on) { toCustom(); m_d.coverTint = on; changed(false); });

        // ---- gradient
        bl->addWidget(section(QStringLiteral("Gradient backdrop")));
        m_gradOn = new QCheckBox(QStringLiteral("Show a gradient behind the panels"));
        bl->addWidget(m_gradOn);
        auto* gRow = new QHBoxLayout;
        gRow->setSpacing(10);
        const QStringList gl = { QStringLiteral("Start"), QStringLiteral("Middle"), QStringLiteral("End") };
        for (int k = 0; k < 3; ++k) {
            m_gradBtn[k] = new ColorButton(gl[k]);
            gRow->addWidget(m_gradBtn[k]);
            m_gradBtn[k]->onPicked = [this, k](const QColor& c) {
                toCustom();
                m_gradDraft[k] = c;
                if (m_gradOn->isChecked()) m_d.grad = gradFromDraft();
                changed(false);
            };
        }
        gRow->addStretch(1);
        bl->addLayout(gRow);
        m_angle = slider(bl, QStringLiteral("Angle"), 0, 360, QStringLiteral("\u00B0"));
        connect(m_gradOn, &QCheckBox::toggled, this, [this](bool on) {
            toCustom();
            m_d.grad = on ? gradFromDraft() : QVector<QColor>();
            if (on && m_d.panel > 70) m_d.panel = 60;       // the gradient only shows through glassy panels
            syncEnabled();
            changed(false);
            syncSliders();
        });
        connect(m_angle, &QSlider::valueChanged, this, [this](int v) { if (m_syncing) return; toCustom(); m_d.angle = v; changed(false); });

        // ---- panels
        bl->addWidget(section(QStringLiteral("Panels")));
        m_panel = slider(bl, QStringLiteral("Panel opacity"), 20, 100, QStringLiteral("%"));
        auto* hint = new QLabel(QStringLiteral("Lower it to let the gradient or your background image shine through."));
        sty(hint, QStringLiteral("font-size:12px;color:#7a7a7a;background:transparent;"));
        bl->addWidget(hint);
        connect(m_panel, &QSlider::valueChanged, this, [this](int v) {
            if (m_syncing) return;
            if (!m_d.image.isEmpty()) { m_d.imgPanel = v; }
            else { toCustom(); m_d.panel = v; }
            changed(false);
        });

        // ---- background image
        bl->addWidget(section(QStringLiteral("Background image")));
        auto* imgRow = new QHBoxLayout;
        imgRow->setSpacing(10);
        auto* choose = new QPushButton(QStringLiteral("Choose image..."));
        m_removeImg = new QPushButton(QStringLiteral("Remove"));
        for (auto* b : { choose, m_removeImg }) { b->setAutoDefault(false); b->setCursor(Qt::PointingHandCursor); b->setFocusPolicy(Qt::NoFocus); }
        m_imgName = new QLabel;
        sty(m_imgName, QStringLiteral("font-size:12px;color:#b3b3b3;background:transparent;"));
        imgRow->addWidget(choose);
        imgRow->addWidget(m_removeImg);
        imgRow->addWidget(m_imgName, 1);
        bl->addLayout(imgRow);
        m_blur = slider(bl, QStringLiteral("Blur"), 0, 40, QString());
        m_dim = slider(bl, QStringLiteral("Dim"), 0, 90, QStringLiteral("%"));
        connect(choose, &QPushButton::clicked, this, [this] { chooseImage(); });
        connect(m_removeImg, &QPushButton::clicked, this, [this] { m_d.image.clear(); syncAll(); changed(false); });
        connect(m_blur, &QSlider::valueChanged, this, [this](int v) { if (m_syncing) return; m_d.blur = v; changed(false); });
        connect(m_dim, &QSlider::valueChanged, this, [this](int v) { if (m_syncing) return; m_d.dim = v; changed(false); });
        bl->addStretch(1);

        // ---- buttons
        auto* row = new QHBoxLayout;
        auto* reset = new QPushButton(QStringLiteral("Reset to default"));
        auto* cancel = new QPushButton(QStringLiteral("Cancel"));
        auto* done = new QPushButton(QStringLiteral("Done"));
        done->setObjectName(QStringLiteral("primary"));
        for (auto* b : { reset, cancel, done }) { b->setAutoDefault(false); b->setCursor(Qt::PointingHandCursor); }
        row->addWidget(reset);
        row->addStretch(1);
        row->addWidget(cancel);
        row->addWidget(done);
        root->addLayout(row);
        connect(reset, &QPushButton::clicked, this, [this] { resetDefault(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(done, &QPushButton::clicked, this, &QDialog::accept);

        syncAll();
    }

    void accept() override
    {
        m_timer.stop();
        Theme::setTheme(m_d, true, true);
        if (m_d.id == QLatin1String("custom")) m_custom = m_d;
        // old managed background copies that are no longer in use
        cleanupImages(m_d.image);
        QDialog::accept();
    }

    void reject() override
    {
        m_timer.stop();
        Theme::setTheme(m_orig, false, true);
        cleanupImages(m_orig.image);
        QDialog::reject();
    }

private:
    // ---------------------------------------------------------------- building blocks
    QLabel* section(const QString& t)
    {
        auto* l = new QLabel(t);
        sty(l, QStringLiteral("font-size:15px;font-weight:700;color:#b3b3b3;background:transparent;padding-top:10px;"));
        return l;
    }

    QSlider* slider(QVBoxLayout* into, const QString& label, int lo, int hi, const QString& unit)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);
        auto* l = new QLabel(label);
        l->setFixedWidth(120);
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(lo, hi);
        s->setFocusPolicy(Qt::NoFocus);
        auto* v = new QLabel;
        v->setFixedWidth(52);
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sty(v, QStringLiteral("font-size:12px;color:#b3b3b3;background:transparent;"));
        v->setText(QString::number(lo) + unit);
        connect(s, &QSlider::valueChanged, v, [v, unit](int x) { v->setText(QString::number(x) + unit); });
        row->addWidget(l);
        row->addWidget(s, 1);
        row->addWidget(v);
        into->addLayout(row);
        return s;
    }

    // ---------------------------------------------------------------- state helpers
    void seedGradient()
    {
        if (m_d.grad.size() >= 2) {
            m_gradDraft[0] = m_d.grad.first();
            m_gradDraft[2] = m_d.grad.last();
            m_gradDraft[1] = m_d.grad.size() > 2 ? m_d.grad[m_d.grad.size() / 2]
                                                 : Theme::mix(m_gradDraft[0], m_gradDraft[2], 0.5);
        } else {
            m_gradDraft[0] = m_d.accent;
            m_gradDraft[2] = Theme::mix(m_d.accent, QColor(Qt::black), 0.75);
            m_gradDraft[1] = Theme::mix(m_gradDraft[0], m_gradDraft[2], 0.5);
        }
    }
    QVector<QColor> gradFromDraft() const { return { m_gradDraft[0], m_gradDraft[1], m_gradDraft[2] }; }

    // Editing any control turns the current theme into the "Custom" one (seeded from what you were looking at).
    void toCustom()
    {
        if (m_d.id == QLatin1String("custom")) return;
        m_d.id = QStringLiteral("custom");
        m_d.name = QStringLiteral("Custom");
    }

    void pickPreset(const QString& id)
    {
        const Theme::Data keep = m_d;
        if (id == QLatin1String("custom")) {
            Theme::Data c = m_custom;
            c.image = keep.image; c.blur = keep.blur; c.dim = keep.dim; c.imgPanel = keep.imgPanel;
            m_d = c;
        } else if (const Theme::Data* p = Theme::findPreset(id)) {
            if (keep.id == QLatin1String("custom")) m_custom = keep;
            Theme::Data n = *p;
            n.image = keep.image; n.blur = keep.blur; n.dim = keep.dim; n.imgPanel = keep.imgPanel;
            m_d = n;
        }
        seedGradient();
        syncAll();
        changed(true);
    }

    void resetDefault()
    {
        if (m_d.id == QLatin1String("custom")) m_custom = m_d;
        m_d = *Theme::findPreset(QStringLiteral("pulsify"));
        seedGradient();
        syncAll();
        changed(true);
    }

    void chooseImage()
    {
        QSettings s;
        const QString start = s.value(QStringLiteral("lastBgDir"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a background image"), start,
            QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff);;All files (*)"));
        if (f.isEmpty()) return;
        s.setValue(QStringLiteral("lastBgDir"), QFileInfo(f).absolutePath());
        if (!QImageReader(f).canRead()) {
            QMessageBox::warning(this, QStringLiteral("Background image"), QStringLiteral("That file couldn't be read as an image."));
            return;
        }
        // keep our own copy so moving or deleting the original never breaks the theme
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backgrounds");
        QDir().mkpath(dir);
        const QString dest = dir + QStringLiteral("/background-%1.%2")
                                       .arg(QDateTime::currentMSecsSinceEpoch()).arg(QFileInfo(f).suffix().toLower());
        if (!QFile::copy(f, dest)) {
            QMessageBox::warning(this, QStringLiteral("Background image"), QStringLiteral("Couldn't copy the image into Pulsify's data folder."));
            return;
        }
        m_created << dest;
        m_d.image = dest;
        syncAll();
        changed(false);
    }

    // Deletes background copies Pulsify made earlier that nothing points at any more.
    void cleanupImages(const QString& keep)
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/backgrounds");
        const QStringList files = QDir(dir).entryList({ QStringLiteral("background-*") }, QDir::Files);
        const QString keepName = keep.isEmpty() ? QString() : QFileInfo(keep).fileName();
        for (const QString& n : files)
            if (n != keepName) QFile::remove(dir + QLatin1Char('/') + n);
    }

    // ---------------------------------------------------------------- applying
    void changed(bool restyle)
    {
        m_restyle = m_restyle || restyle;
        m_customSwatch->setData(m_d.id == QLatin1String("custom") ? m_d : m_custom);
        markSwatches();
        m_timer.start(restyle ? 0 : 40);
    }

    void flush()
    {
        Theme::setTheme(m_d, false, m_restyle);
        m_restyle = false;
    }

    // ---------------------------------------------------------------- syncing the controls
    void markSwatches()
    {
        for (ThemeSwatch* s : m_swatches) s->setChecked(s->data().id == m_d.id);
        m_customSwatch->setChecked(m_d.id == QLatin1String("custom"));
    }

    void syncEnabled()
    {
        const bool on = m_gradOn->isChecked();
        for (auto* b : m_gradBtn) b->setEnabled(on);
        m_angle->setEnabled(on);
        const bool img = !m_d.image.isEmpty();
        m_blur->setEnabled(img);
        m_dim->setEnabled(img);
        m_removeImg->setEnabled(img);
    }

    void syncSliders()
    {
        m_syncing = true;
        const bool img = !m_d.image.isEmpty();
        m_panel->setValue(img ? m_d.imgPanel : m_d.panel);
        m_angle->setValue(m_d.angle);
        m_blur->setValue(m_d.blur);
        m_dim->setValue(m_d.dim);
        m_syncing = false;
    }

    void syncAll()
    {
        m_accentBtn->setColor(m_d.accent);
        m_bgBtn->setColor(m_d.bg);
        for (int k = 0; k < 3; ++k) m_gradBtn[k]->setColor(m_gradDraft[k]);
        {
            QSignalBlocker a(m_tint), b(m_gradOn);
            m_tint->setChecked(m_d.coverTint);
            m_gradOn->setChecked(m_d.grad.size() >= 2);
        }
        m_imgName->setText(m_d.image.isEmpty() ? QStringLiteral("No image") : QFileInfo(m_d.image).fileName());
        syncSliders();
        syncEnabled();
        m_customSwatch->setData(m_d.id == QLatin1String("custom") ? m_d : m_custom);
        markSwatches();
    }

    Theme::Data m_orig, m_d, m_custom;
    QColor m_gradDraft[3];
    QList<ThemeSwatch*> m_swatches;
    ThemeSwatch* m_customSwatch = nullptr;
    ColorButton *m_accentBtn = nullptr, *m_bgBtn = nullptr;
    ColorButton* m_gradBtn[3] = { nullptr, nullptr, nullptr };
    QCheckBox *m_tint = nullptr, *m_gradOn = nullptr;
    QSlider *m_angle = nullptr, *m_panel = nullptr, *m_blur = nullptr, *m_dim = nullptr;
    QPushButton* m_removeImg = nullptr;
    QLabel* m_imgName = nullptr;
    QStringList m_created;
    QTimer m_timer;
    bool m_restyle = false;
    bool m_syncing = false;
};
