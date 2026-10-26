#pragma once
#include <QtWidgets>
#include "widgets.h"
#include "library.h"
#include "online.h"
#include "playlists.h"

// Outlined "pill" button used for secondary actions in page headers.
inline QPushButton* makePill(const QString& text, QWidget* parent = nullptr)
{
    auto* b = new QPushButton(text, parent);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedHeight(34);
    b->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;color:white;border:1px solid rgba(255,255,255,0.35);"
        "border-radius:17px;padding:0 18px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{border:1px solid white;background:rgba(255,255,255,0.08);}"
        "QPushButton:pressed{background:rgba(255,255,255,0.16);}"
        "QPushButton:checked{background:white;color:black;border:1px solid white;}"));
    return b;
}

inline QString searchBoxStyle()
{
    return QStringLiteral(
        "QLineEdit{background:rgba(255,255,255,0.10);border:1px solid transparent;border-radius:20px;"
        "padding:0 16px;color:white;font-size:14px;selection-background-color:#1DB954;}"
        "QLineEdit:hover{background:rgba(255,255,255,0.14);}"
        "QLineEdit:focus{background:rgba(255,255,255,0.16);border:1px solid rgba(255,255,255,0.75);}");
}

inline QString durationText(qint64 ms)
{
    if (ms <= 0) return QString();
    const qint64 mins = ms / 60000;
    return mins >= 60 ? QStringLiteral(" \u00b7 %1 hr %2 min").arg(mins / 60).arg(mins % 60)
                      : QStringLiteral(" \u00b7 %1 min").arg(qMax<qint64>(1, mins));
}

// ============================================================================
//  TrackPage: title + count + (filter | query) box + hero buttons + track list.
//  Used for the offline library, online search results and a playlist's songs.
// ============================================================================
class TrackPage : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Filter, Query };

    TrackPage(const QString& title, Mode mode, TrackModel* model, QWidget* parent = nullptr)
        : QWidget(parent), m_mode(mode), m_model(model)
    {
        auto* pl = new QVBoxLayout(this);
        pl->setContentsMargins(0, 28, 0, 0);
        pl->setSpacing(0);

        auto* head = new QHBoxLayout;
        head->setContentsMargins(34, 0, 34, 0);
        auto* titleCol = new QVBoxLayout;
        titleCol->setSpacing(2);
        m_title = new QLabel(title);
        m_title->setStyleSheet(QStringLiteral("font-size:40px;font-weight:800;color:white;background:transparent;"));
        m_count = new QLabel;
        m_count->setStyleSheet(QStringLiteral("font-size:13px;color:#c8c8c8;background:transparent;"));
        titleCol->addWidget(m_title);
        titleCol->addWidget(m_count);
        m_edit = new QLineEdit;
        m_edit->setPlaceholderText(mode == Query ? QStringLiteral("Search songs, artists, albums\u2026")
                                                 : QStringLiteral("Search songs"));
        m_edit->setClearButtonEnabled(true);
        m_edit->setFixedSize(mode == Query ? 400 : 280, 40);
        m_edit->setStyleSheet(searchBoxStyle());
        head->addLayout(titleCol, 1);
        head->addWidget(m_edit, 0, Qt::AlignTop);
        pl->addLayout(head);

        m_actions = new QHBoxLayout;
        m_actions->setContentsMargins(34, 20, 34, 12);
        m_actions->setSpacing(16);
        m_play = new IconButton(IconButton::Play, 62);
        m_play->setCircle(Theme::accent());
        m_shuffle = new IconButton(IconButton::Shuffle, 42);
        m_actions->addWidget(m_play);
        m_actions->addWidget(m_shuffle);
        m_actions->addStretch(1);
        pl->addLayout(m_actions);

        m_proxy = new QSortFilterProxyModel(this);
        m_proxy->setFilterRole(SearchRole);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_proxy->setSourceModel(model);
        m_view = new TrackView;
        m_view->setModel(m_proxy);
        m_view->setContextMenuPolicy(Qt::NoContextMenu);
        m_view->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
        pl->addWidget(m_view, 1);

        connect(m_model, &TrackModel::libraryChanged, this, &TrackPage::updateCount);
        connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& t) {
            if (m_mode != Filter) return;
            m_proxy->setFilterFixedString(t);
            updateCount();
        });
        connect(m_edit, &QLineEdit::returnPressed, this, [this] {
            if (m_mode == Query) emit queryEntered(m_edit->text().trimmed());
        });
        connect(m_view, &QAbstractItemView::clicked, this, [this](const QModelIndex& ix) {
            emit trackClicked(ix.data(IdRole).toInt());
        });
        // addRequested signal not in this TrackView — context menu handles it via contextRequested
        connect(m_view->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
            const QModelIndex ix = m_view->indexAt(pt);
            if (ix.isValid())
                emit contextRequested(ix.data(IdRole).toInt(), m_view->viewport()->mapToGlobal(pt));
        });
        connect(m_play, &QAbstractButton::clicked, this, &TrackPage::playClicked);
        connect(m_shuffle, &QAbstractButton::clicked, this, &TrackPage::shuffleClicked);

        updateCount();
    }

    TrackView*   view() const  { return m_view; }
    QLineEdit*   edit() const  { return m_edit; }
    TrackModel*  model() const { return m_model; }

    void setTitle(const QString& t) { m_title->setText(t); }
    void setQueryBoxVisible(bool v) { m_edit->setVisible(v); }   // the top bar owns the search box
    void setEmptyHints(const QString& empty, const QString& noMatch)
    {
        m_emptyHint = empty;
        m_noMatchHint = noMatch;
        updateCount();
    }
    void setPlaying(bool p)         { m_play->setPlaying(p); }
    void setShuffleActive(bool on)  { m_shuffle->setActive(on); }

    // Adds a widget (pill button etc.) to the hero row, right of the shuffle button.
    void addActionWidget(QWidget* w) { m_actions->insertWidget(m_actions->count() - 1, w); }

    // Swaps the underlying model (playlist detail page).
    void setModel(TrackModel* m)
    {
        if (m_model) disconnect(m_model, nullptr, this, nullptr);
        m_model = m;
        m_view->resetRowState();
        m_edit->clear();
        m_proxy->setSourceModel(m);
        connect(m, &TrackModel::libraryChanged, this, &TrackPage::updateCount);
        updateCount();
    }

    QVector<Track> visibleTracks() const
    {
        QVector<Track> out;
        const int n = m_proxy->rowCount();
        out.reserve(n);
        for (int r = 0; r < n; ++r) {
            const QModelIndex src = m_proxy->mapToSource(m_proxy->index(r, 0));
            const int id = src.data(IdRole).toInt();
            if (const Track* t = m_model->byId(id)) out.push_back(*t);
        }
        return out;
    }

    void updateCount()
    {
        const int total = m_model ? m_model->rowCount() : 0;
        const int shown = m_proxy->rowCount();
        const QString dur = durationText(m_model ? m_model->totalDuration() : 0);
        if (m_mode == Query) {
            m_count->setText(total > 0 ? QStringLiteral("%1 result%2").arg(total).arg(total == 1 ? "" : "s")
                                       : QString());
            return;
        }
        if (shown == total)
            m_count->setText(QStringLiteral("%1 song%2%3").arg(total).arg(total == 1 ? "" : "s").arg(dur));
        else
            m_count->setText(QStringLiteral("%1 of %2 songs").arg(shown).arg(total));

        if (total == 0)          m_view->setEmptyText(m_emptyHint);
        else if (shown == 0)     m_view->setEmptyText(m_noMatchHint);
        else                     m_view->setEmptyText(QString());
    }

signals:
    void queryEntered(const QString& q);
    void trackClicked(int id);
    void addRequested(int id, const QPoint& globalPos);
    void contextRequested(int id, const QPoint& globalPos);
    void playClicked();
    void shuffleClicked();

private:
    Mode        m_mode;
    TrackModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    TrackView*  m_view = nullptr;
    QLabel*     m_title = nullptr;
    QLabel*     m_count = nullptr;
    QLineEdit*  m_edit = nullptr;
    QHBoxLayout* m_actions = nullptr;
    IconButton* m_play = nullptr;
    IconButton* m_shuffle = nullptr;
    QString     m_emptyHint = QStringLiteral("Nothing here yet.");
    QString     m_noMatchHint = QStringLiteral("No songs match your search.");
};

// ============================================================================
//  Playlist list model (feeds the Your Library panel)
// ============================================================================
class PlaylistListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum { CountRole = Qt::UserRole + 20, DurRole, CoverRole2 };

    explicit PlaylistListModel(PlaylistStore* s, QObject* parent = nullptr)
        : QAbstractListModel(parent), m_store(s) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : int(m_store->playlists().size());
    }

    QVariant data(const QModelIndex& ix, int role) const override
    {
        if (!ix.isValid() || ix.row() < 0 || ix.row() >= m_store->playlists().size()) return {};
        const Playlist& p = m_store->playlists()[ix.row()];
        switch (role) {
        case Qt::DisplayRole: return p.name;
        case CountRole:       return int(p.entries.size());
        case DurRole: {
            qint64 total = 0;
            for (const PlaylistEntry& e : p.entries) total += e.durationMs;
            return total;
        }
        case CoverRole2:
            // No pixmap available from entries directly; return empty
            return QPixmap();
        }
        return {};
    }

    void reset()
    {
        beginResetModel();
        endResetModel();
    }

    void touch()
    {
        if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
    }

private:
    PlaylistStore* m_store;
};
