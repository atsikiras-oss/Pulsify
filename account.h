#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <memory>
#include "library.h"
#include "online.h"

// Optional build-time defaults:  cmake -DPULSE_GOOGLE_CLIENT_ID=... -DPULSE_GOOGLE_CLIENT_SECRET=...
#ifndef PULSE_GOOGLE_CLIENT_ID
#define PULSE_GOOGLE_CLIENT_ID ""
#endif
#ifndef PULSE_GOOGLE_CLIENT_SECRET
#define PULSE_GOOGLE_CLIENT_SECRET ""
#endif

using FormFields = QList<QPair<QString, QString>>;

inline void addField(FormFields& f, const char* key, const QString& value)
{
    f.append(qMakePair(QString::fromLatin1(key), value));
}

inline QByteArray formBody(const FormFields& f)
{
    QByteArray b;
    for (const auto& kv : f) {
        if (!b.isEmpty()) b += '&';
        b += QUrl::toPercentEncoding(kv.first);
        b += '=';
        b += QUrl::toPercentEncoding(kv.second);
    }
    return b;
}

// ============================================================================
//  GoogleAuth: "installed app" OAuth 2.0 with PKCE.
//  Opens the system browser, listens on 127.0.0.1:<random port> for Google's
//  redirect, swaps the code for tokens. Only a refresh token is stored.
// ============================================================================
class GoogleAuth : public QObject
{
    Q_OBJECT
public:
    using TokenCb = std::function<void(bool ok, const QString& token, const QString& err)>;

    explicit GoogleAuth(QObject* parent = nullptr) : QObject(parent)
    {
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(3 * 60 * 1000);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            stopServer();
            finishFail(QStringLiteral("Sign-in timed out. Try again when you\u2019re ready."));
        });
        connect(&m_server, &QTcpServer::newConnection, this, &GoogleAuth::onConnection);
    }

    ~GoogleAuth() override { stopServer(); }

    static QString clientId()
    {
        const QString v = QSettings().value(QStringLiteral("google/clientId")).toString().trimmed();
        return v.isEmpty() ? QString::fromUtf8(PULSE_GOOGLE_CLIENT_ID) : v;
    }
    static QString clientSecret()
    {
        const QString v = QSettings().value(QStringLiteral("google/clientSecret")).toString().trimmed();
        return v.isEmpty() ? QString::fromUtf8(PULSE_GOOGLE_CLIENT_SECRET) : v;
    }
    static bool configured() { return !clientId().isEmpty() && !clientSecret().isEmpty(); }

    bool    signedIn() const   { return !QSettings().value(QStringLiteral("google/refresh")).toString().isEmpty(); }
    bool    busy() const       { return m_busy; }
    QString name() const       { return QSettings().value(QStringLiteral("google/name")).toString(); }
    QString email() const      { return QSettings().value(QStringLiteral("google/email")).toString(); }
    QString pictureUrl() const { return QSettings().value(QStringLiteral("google/picture")).toString(); }

    void signIn()
    {
        if (m_busy) return;
        if (!configured()) {
            emit failed(QStringLiteral("Enter a Google client ID and client secret in Settings first."));
            return;
        }
        stopServer();
        if (!m_server.listen(QHostAddress::LocalHost, 0)) {
            emit failed(QStringLiteral("Couldn\u2019t open a local port for Google\u2019s sign-in redirect."));
            return;
        }
        m_busy = true;
        m_redirect = QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());   // must match in the token exchange
        m_verifier = randomToken(64);
        m_state = randomToken(16);
        const QByteArray challenge =
            QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

        QUrl url(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("client_id"), clientId());
        q.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
        q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
        q.addQueryItem(QStringLiteral("scope"),
                       QStringLiteral("openid email profile https://www.googleapis.com/auth/youtube.readonly"));
        q.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
        q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
        q.addQueryItem(QStringLiteral("state"), m_state);
        q.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
        q.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
        url.setQuery(q);

        m_timeout.start();
        emit stateChanged();
        QDesktopServices::openUrl(url);
    }

    void cancel()
    {
        if (!m_busy) return;
        stopServer();
        m_timeout.stop();
        m_busy = false;
        emit stateChanged();
    }

    void signOut()
    {
        const QString refresh = QSettings().value(QStringLiteral("google/refresh")).toString();
        if (!refresh.isEmpty()) {                        // best effort: tell Google to forget the grant
            QNetworkRequest req{ QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")) };
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
            req.setTransferTimeout(10000);
            FormFields f;
            addField(f, "token", refresh);
            QNetworkReply* rep = m_nam.post(req, formBody(f));
            connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
        }
        signOutLocal();
    }

    // Calls back with a valid access token (refreshing it first when needed).
    void withToken(const TokenCb& cb)
    {
        if (!signedIn()) { cb(false, QString(), QStringLiteral("Not signed in")); return; }
        if (!m_access.isEmpty() && m_accessExp > QDateTime::currentDateTime()) {
            cb(true, m_access, QString());
            return;
        }
        m_waiters.push_back(cb);
        if (m_refreshing) return;
        m_refreshing = true;
        FormFields f;
        addField(f, "client_id", clientId());
        addField(f, "client_secret", clientSecret());
        addField(f, "refresh_token", QSettings().value(QStringLiteral("google/refresh")).toString());
        addField(f, "grant_type", QStringLiteral("refresh_token"));
        post(f, [this](bool ok, const QJsonObject& o, const QString& err) {
            m_refreshing = false;
            const QVector<TokenCb> waiters = m_waiters;
            m_waiters.clear();
            if (ok) {
                storeToken(o);
                for (const TokenCb& w : waiters) w(true, m_access, QString());
                return;
            }
            QString msg = err;
            if (jstr(o, "error") == QLatin1String("invalid_grant")) {
                signOutLocal();
                msg = QStringLiteral("Your Google session expired \u2013 please log in again.");
            }
            for (const TokenCb& w : waiters) w(false, QString(), msg);
        });
    }

    void invalidateToken() { m_access.clear(); }

signals:
    void stateChanged();                 // signed in / out / busy changed
    void loggedIn();                     // a sign-in just completed
    void failed(const QString& message);

private:
    using JsonCb = std::function<void(bool ok, const QJsonObject& o, const QString& err)>;

    QString redirectUri() const { return m_redirect; }

    static QString randomToken(int bytes)
    {
        QByteArray b(bytes, 0);
        for (int i = 0; i < bytes; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
        return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }

    void stopServer()
    {
        if (m_server.isListening()) m_server.close();
    }

    void finishFail(const QString& msg)
    {
        m_busy = false;
        emit failed(msg);
        emit stateChanged();
    }

    void signOutLocal()
    {
        QSettings s;
        s.remove(QStringLiteral("google/refresh"));
        s.remove(QStringLiteral("google/name"));
        s.remove(QStringLiteral("google/email"));
        s.remove(QStringLiteral("google/picture"));
        m_access.clear();
        m_busy = false;
        emit stateChanged();
    }

    void storeToken(const QJsonObject& o)
    {
        m_access = jstr(o, "access_token");
        const int ttl = o.value(QLatin1String("expires_in")).toInt(3600);
        m_accessExp = QDateTime::currentDateTime().addSecs(qMax(60, ttl - 60));
    }

    void post(const FormFields& f, const JsonCb& cb)
    {
        QNetworkRequest req{ QUrl(QStringLiteral("https://oauth2.googleapis.com/token")) };
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        req.setTransferTimeout(20000);
        QNetworkReply* rep = m_nam.post(req, formBody(f));
        connect(rep, &QNetworkReply::finished, this, [rep, cb] {
            rep->deleteLater();
            const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
            const int st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (st >= 200 && st < 300 && !o.isEmpty()) { cb(true, o, QString()); return; }
            QString msg = jstr(o, "error_description");
            if (msg.isEmpty()) msg = jstr(o, "error");
            if (msg.isEmpty()) msg = rep->errorString();
            cb(false, o, msg);
        });
    }

    // ---- loopback redirect ---------------------------------------------------
    void onConnection()
    {
        while (QTcpSocket* s = m_server.nextPendingConnection()) {
            auto buf = std::make_shared<QByteArray>();
            connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
                if (s->property("handled").toBool()) return;
                *buf += s->readAll();
                const int eol = int(buf->indexOf("\r\n"));
                if (eol < 0) {
                    if (buf->size() > 16384) s->close();
                    return;
                }
                s->setProperty("handled", true);
                handleRequest(s, buf->left(eol));
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        }
    }

    static void respond(QTcpSocket* s, int status, const QString& html)
    {
        const QByteArray body = html.toUtf8();
        QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Not Found") + "\r\n";
        r += "Content-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ";
        r += QByteArray::number(body.size());
        r += "\r\n\r\n";
        r += body;
        s->write(r);
        s->disconnectFromHost();
    }

    static QString resultPage(bool ok, const QString& msg)
    {
        return QStringLiteral(
                   "<!doctype html><meta charset='utf-8'><title>Pulse</title>"
                   "<body style='margin:0;background:#121212;color:#fff;font-family:Segoe UI,Helvetica,Arial,sans-serif;"
                   "display:flex;align-items:center;justify-content:center;height:100vh;text-align:center'>"
                   "<div><div style='font-size:44px;color:%1'>%2</div>"
                   "<h2 style='margin:12px 0 6px'>%3</h2>"
                   "<p style='color:#b3b3b3'>You can close this tab and go back to Pulse.</p></div></body>")
            .arg(ok ? QStringLiteral("#1DB954") : QStringLiteral("#ff8a65"),
                 ok ? QStringLiteral("&#10003;") : QStringLiteral("!"),
                 msg.toHtmlEscaped());
    }

    void handleRequest(QTcpSocket* s, const QByteArray& requestLine)
    {
        const QList<QByteArray> parts = requestLine.split(' ');
        const QString target = QString::fromLatin1(parts.value(1));
        const QUrlQuery q(QUrl(QStringLiteral("http://127.0.0.1") + target));
        const QString code = q.queryItemValue(QStringLiteral("code"));
        const QString err = q.queryItemValue(QStringLiteral("error"));
        if (code.isEmpty() && err.isEmpty()) {            // favicon & friends
            respond(s, 404, QStringLiteral("Not found"));
            return;
        }
        stopServer();
        m_timeout.stop();
        if (!err.isEmpty()) {
            const QString msg = err == QLatin1String("access_denied") ? QStringLiteral("Sign-in was cancelled")
                                                                      : QStringLiteral("Google said: %1").arg(err);
            respond(s, 200, resultPage(false, msg));
            finishFail(msg);
            return;
        }
        if (q.queryItemValue(QStringLiteral("state")) != m_state) {
            respond(s, 200, resultPage(false, QStringLiteral("Sign-in response didn\u2019t match")));
            finishFail(QStringLiteral("The sign-in response didn\u2019t match this request. Please try again."));
            return;
        }
        respond(s, 200, resultPage(true, QStringLiteral("You\u2019re signed in")));
        exchangeCode(code);
    }

    void exchangeCode(const QString& code)
    {
        FormFields f;
        addField(f, "code", code);
        addField(f, "client_id", clientId());
        addField(f, "client_secret", clientSecret());
        addField(f, "redirect_uri", redirectUri());
        addField(f, "grant_type", QStringLiteral("authorization_code"));
        addField(f, "code_verifier", m_verifier);
        post(f, [this](bool ok, const QJsonObject& o, const QString& err) {
            if (!ok) { finishFail(err); return; }
            storeToken(o);
            const QString refresh = jstr(o, "refresh_token");
            if (!refresh.isEmpty()) QSettings().setValue(QStringLiteral("google/refresh"), refresh);
            if (!signedIn()) {
                finishFail(QStringLiteral("Google didn\u2019t return a refresh token. Remove Pulse under "
                                          "myaccount.google.com/permissions and try again."));
                return;
            }
            fetchProfile();
        });
    }

    void fetchProfile()
    {
        QNetworkRequest req{ QUrl(QStringLiteral("https://www.googleapis.com/oauth2/v3/userinfo")) };
        req.setRawHeader("Authorization", "Bearer " + m_access.toUtf8());
        req.setTransferTimeout(15000);
        QNetworkReply* rep = m_nam.get(req);
        connect(rep, &QNetworkReply::finished, this, [this, rep] {
            rep->deleteLater();
            const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
            QSettings s;
            s.setValue(QStringLiteral("google/name"), jstr(o, "name"));
            s.setValue(QStringLiteral("google/email"), jstr(o, "email"));
            s.setValue(QStringLiteral("google/picture"), jstr(o, "picture"));
            m_busy = false;
            emit stateChanged();
            emit loggedIn();
        });
    }

    QTcpServer             m_server;
    QNetworkAccessManager  m_nam;
    QTimer                 m_timeout;
    QString                m_verifier, m_state, m_redirect;
    QString                m_access;
    QDateTime              m_accessExp;
    QVector<TokenCb>       m_waiters;
    bool                   m_busy = false;
    bool                   m_refreshing = false;
};

// ============================================================================
//  YouTubeApi: the signed-in user's playlists (incl. private ones + Liked videos)
// ============================================================================
struct RemotePlaylist {
    QString id, title, thumbUrl;
    int     count = -1;
    bool    liked = false;
};

inline qint64 parseIsoDuration(const QString& s)       // "PT3M45S" -> ms
{
    static const QRegularExpression re(
        QStringLiteral(R"RX(^P(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?$)RX"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) return 0;
    const qint64 sec = m.captured(1).toLongLong() * 86400 + m.captured(2).toLongLong() * 3600
                       + m.captured(3).toLongLong() * 60 + m.captured(4).toLongLong();
    return sec * 1000;
}

class YouTubeApi : public QObject
{
    Q_OBJECT
public:
    using PlaylistsCb = std::function<void(bool ok, const QVector<RemotePlaylist>& lists, const QString& err)>;
    using ItemsCb     = std::function<void(bool ok, const QVector<Track>& tracks, const QString& err)>;
    using ProgressCb  = std::function<void(int count)>;

    explicit YouTubeApi(GoogleAuth* auth, QObject* parent = nullptr) : QObject(parent), m_auth(auth) {}

    void listPlaylists(const PlaylistsCb& cb)
    {
        auto st = std::make_shared<PlState>();
        st->cb = cb;
        // "Liked videos" is a special playlist; ask for its id first (a failure here is not fatal)
        get(QStringLiteral("https://www.googleapis.com/youtube/v3/channels?part=contentDetails&mine=true"),
            [this, st](const QJsonObject& o, const QString&) {
                const QJsonArray items = jarr(o, "items");
                if (!items.isEmpty()) {
                    const QString likes = jstr(jobj(jobj(items.at(0).toObject(), "contentDetails"),
                                                    "relatedPlaylists"), "likes");
                    if (!likes.isEmpty()) {
                        RemotePlaylist r;
                        r.id = likes;
                        r.title = QStringLiteral("Liked videos");
                        r.liked = true;
                        st->lists.push_back(r);
                    }
                }
                playlistsPage(st, QString());
            });
    }

    void listItems(const QString& playlistId, const ProgressCb& progress, const ItemsCb& cb)
    {
        auto st = std::make_shared<ItState>();
        st->pid = playlistId;
        st->progress = progress;
        st->cb = cb;
        itemsPage(st, QString());
    }

private:
    struct PlState { QVector<RemotePlaylist> lists; PlaylistsCb cb; int pages = 0; };
    struct ItState { QString pid; QVector<Track> tracks; ProgressCb progress; ItemsCb cb; int pages = 0; };

    using GetCb = std::function<void(const QJsonObject& o, const QString& err)>;   // err empty = success

    static QString enc(const QString& s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }

    static QString apiError(int status, const QJsonObject& o, const QString& net)
    {
        const QJsonObject e = jobj(o, "error");
        const QString msg = jstr(e, "message");
        const QJsonArray errs = jarr(e, "errors");
        const QString reason = errs.isEmpty() ? QString() : jstr(errs.at(0).toObject(), "reason");
        if (reason == QLatin1String("quotaExceeded") || reason == QLatin1String("dailyLimitExceeded"))
            return QStringLiteral("Today\u2019s YouTube API quota for your Google project is used up. Try again tomorrow.");
        if (reason == QLatin1String("accessNotConfigured") || msg.contains(QLatin1String("has not been used in project")))
            return QStringLiteral("The YouTube Data API v3 isn\u2019t enabled for your Google Cloud project yet "
                                  "(APIs & Services \u2192 Library \u2192 YouTube Data API v3 \u2192 Enable).");
        if (reason == QLatin1String("insufficientPermissions") || msg.contains(QLatin1String("insufficient authentication scopes")))
            return QStringLiteral("Pulse needs permission to read your YouTube account. Log out, log in again and allow access.");
        if (reason == QLatin1String("playlistNotFound") || status == 404)
            return QStringLiteral("That playlist wasn\u2019t found (it may have been deleted or made unavailable).");
        if (!msg.isEmpty()) return msg.left(220);
        return net.isEmpty() ? QStringLiteral("YouTube request failed (%1)").arg(status) : net;
    }

    void get(const QString& url, const GetCb& cb, bool retried = false)
    {
        m_auth->withToken([this, url, cb, retried](bool ok, const QString& token, const QString& err) {
            if (!ok) { cb(QJsonObject(), err); return; }
            QNetworkRequest req{ QUrl(url) };
            req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
            req.setTransferTimeout(20000);
            QNetworkReply* rep = m_nam.get(req);
            connect(rep, &QNetworkReply::finished, this, [this, rep, url, cb, retried] {
                rep->deleteLater();
                const int st = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QJsonObject o = QJsonDocument::fromJson(rep->readAll()).object();
                if (st == 401 && !retried) {              // token went stale: refresh once and retry
                    m_auth->invalidateToken();
                    get(url, cb, true);
                    return;
                }
                if (st >= 200 && st < 300) { cb(o, QString()); return; }
                cb(o, apiError(st, o, rep->errorString()));
            });
        });
    }

    void playlistsPage(const std::shared_ptr<PlState>& st, const QString& pageToken)
    {
        QString url = QStringLiteral("https://www.googleapis.com/youtube/v3/playlists?part=snippet,contentDetails&mine=true&maxResults=50");
        if (!pageToken.isEmpty()) url += QStringLiteral("&pageToken=") + enc(pageToken);
        get(url, [this, st](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { st->cb(false, QVector<RemotePlaylist>(), err); return; }
            const QJsonArray items = jarr(o, "items");
            for (const QJsonValue& v : items) {
                const QJsonObject it = v.toObject();
                const QJsonObject sn = jobj(it, "snippet");
                RemotePlaylist r;
                r.id = jstr(it, "id");
                r.title = jstr(sn, "title");
                r.count = jobj(it, "contentDetails").value(QLatin1String("itemCount")).toInt(-1);
                const QJsonObject th = jobj(sn, "thumbnails");
                r.thumbUrl = jstr(jobj(th, "medium"), "url");
                if (r.thumbUrl.isEmpty()) r.thumbUrl = jstr(jobj(th, "default"), "url");
                if (!r.id.isEmpty() && !r.title.isEmpty()) st->lists.push_back(r);
            }
            const QString next = jstr(o, "nextPageToken");
            if (!next.isEmpty() && ++st->pages < 10) { playlistsPage(st, next); return; }
            st->cb(true, st->lists, QString());
        });
    }

    void itemsPage(const std::shared_ptr<ItState>& st, const QString& pageToken)
    {
        QString url = QStringLiteral("https://www.googleapis.com/youtube/v3/playlistItems?part=snippet,contentDetails&maxResults=50&playlistId=")
                      + enc(st->pid);
        if (!pageToken.isEmpty()) url += QStringLiteral("&pageToken=") + enc(pageToken);
        get(url, [this, st](const QJsonObject& o, const QString& err) {
            if (!err.isEmpty()) { st->cb(false, QVector<Track>(), err); return; }
            const QJsonArray items = jarr(o, "items");
            for (const QJsonValue& v : items) {
                const QJsonObject it = v.toObject();
                const QJsonObject sn = jobj(it, "snippet");
                QString vid = jstr(jobj(it, "contentDetails"), "videoId");
                if (vid.isEmpty()) vid = jstr(jobj(sn, "resourceId"), "videoId");
                const QString title = jstr(sn, "title");
                if (vid.size() != 11 || title.isEmpty()
                    || title == QLatin1String("Private video") || title == QLatin1String("Deleted video"))
                    continue;
                Track t;
                t.videoId = vid;
                t.webUrl = QStringLiteral("https://www.youtube.com/watch?v=") + vid;
                guessMeta(t, title, jstr(sn, "videoOwnerChannelTitle"), QString(), QString());
                st->tracks.push_back(t);
            }
            if (st->progress) st->progress(int(st->tracks.size()));
            const QString next = jstr(o, "nextPageToken");
            if (!next.isEmpty() && ++st->pages < 40) { itemsPage(st, next); return; }   // up to 2000 songs
            durationsBatch(st, 0);
        });
    }

    // playlistItems has no durations: look them up 50 at a time so the list isn't full of "--:--"
    void durationsBatch(const std::shared_ptr<ItState>& st, int from)
    {
        const int total = int(st->tracks.size());
        if (from >= total) { st->cb(true, st->tracks, QString()); return; }
        const int to = qMin(from + 50, total);
        QStringList ids;
        for (int i = from; i < to; ++i) ids << st->tracks[i].videoId;
        const QString url = QStringLiteral("https://www.googleapis.com/youtube/v3/videos?part=contentDetails&maxResults=50&id=")
                            + ids.join(QLatin1Char(','));
        get(url, [this, st, from, to](const QJsonObject& o, const QString& err) {
            if (err.isEmpty()) {
                QHash<QString, qint64> dur;
                const QJsonArray items = jarr(o, "items");
                for (const QJsonValue& v : items) {
                    const QJsonObject it = v.toObject();
                    dur.insert(jstr(it, "id"), parseIsoDuration(jstr(jobj(it, "contentDetails"), "duration")));
                }
                for (int i = from; i < to; ++i) st->tracks[i].durationMs = dur.value(st->tracks[i].videoId, 0);
            }
            durationsBatch(st, to);                       // a failed batch only leaves those durations empty
        });
    }

    GoogleAuth*           m_auth;
    QNetworkAccessManager m_nam;
};
