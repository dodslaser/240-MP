#include "TidalBackend.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QDebug>
#include <QDateTime>

#ifdef TIDAL_HAS_QRCODE
#include <qrencode.h>
#endif

// Tidal's unofficial v1 API — same endpoints Tidal's own apps use.
// Credentials are well-known public values used by open-source Tidal clients.
static const QString TIDAL_API    = QStringLiteral("https://api.tidal.com/v1");
static const QString TIDAL_AUTH   = QStringLiteral("https://auth.tidal.com/v1/oauth2");
static const QString TIDAL_CLIENT = QStringLiteral("zU4XHVVkc2tDPo4t");
static const QString TIDAL_SECRET = QStringLiteral("VspDuSFOVnLsok3cR1cVFNVxb9v1XjuVKRQBHqkEuE4=");

// Tidal's device auth and token endpoints require HTTP Basic auth in the header
// in addition to client_id in the request body.
static QByteArray tidalBasicAuth() {
    return "Basic " + (TIDAL_CLIENT + ":" + TIDAL_SECRET).toUtf8().toBase64();
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TidalBackend::TidalBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
    , m_countryCode(QStringLiteral("US"))
{
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &TidalBackend::pollForToken);

    QJsonObject auth = loadAuth();
    if (!auth.isEmpty()) {
        m_accessToken  = auth["access_token"].toString();
        m_refreshToken = auth["refresh_token"].toString();
        m_userId       = auth["userId"].toString();
        m_countryCode  = auth["countryCode"].toString();
        if (m_countryCode.isEmpty()) m_countryCode = QStringLiteral("US");
        qint64 exp    = auth["expires_at"].toInteger(0);
        m_tokenExpiry = QDateTime::fromSecsSinceEpoch(exp);

        if (!m_refreshToken.isEmpty() && QDateTime::currentDateTimeUtc() >= m_tokenExpiry)
            refreshToken();
    }
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

QJsonObject TidalBackend::loadAuth() const {
    QFile f(m_dataRoot + "/tidal_auth.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

void TidalBackend::saveAuth(const QJsonObject &auth) {
    QFile f(m_dataRoot + "/tidal_auth.json");
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[TidalBackend] Could not write tidal_auth.json");
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
}

QJsonObject TidalBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

// ---------------------------------------------------------------------------
// Auth state
// ---------------------------------------------------------------------------

QString TidalBackend::get_auth_state() {
    if (!m_accessToken.isEmpty() && !m_userId.isEmpty())
        return QStringLiteral("authenticated");
    return QStringLiteral("unauthenticated");
}

// ---------------------------------------------------------------------------
// Device authorization flow (RFC 8628)
// ---------------------------------------------------------------------------

void TidalBackend::start_auth() {
    m_pollTimer->stop();
    m_deviceCode.clear();

    QUrl url(TIDAL_AUTH + "/device_authorization");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Authorization", tidalBasicAuth());

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), TIDAL_CLIENT);
    body.addQueryItem(QStringLiteral("scope"),     QStringLiteral("r_usr+w_usr+w_sub"));

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("Device authorization failed: ") + reply->errorString());
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();

        m_deviceCode       = obj["deviceCode"].toString();
        QString userCode   = obj["userCode"].toString();
        QString verUri     = obj["verificationUri"].toString();
        QString verUriFull = obj["verificationUriComplete"].toString();
        if (verUriFull.isEmpty()) verUriFull = verUri;
        m_pollInterval     = qMax(2, obj["interval"].toInt(2));

        int qrSize = 0;
        QString qrData = generateQrData(verUriFull, qrSize);

        emit deviceAuthReady(userCode, verUri, qrData, qrSize);

        m_pollTimer->setInterval(m_pollInterval * 1000);
        m_pollTimer->start();
    });
}

void TidalBackend::pollForToken() {
    if (m_deviceCode.isEmpty()) { m_pollTimer->stop(); return; }

    QUrl url(TIDAL_AUTH + "/token");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Authorization", tidalBasicAuth());

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"),   TIDAL_CLIENT);
    body.addQueryItem(QStringLiteral("device_code"), m_deviceCode);
    body.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    body.addQueryItem(QStringLiteral("scope"), QStringLiteral("r_usr+w_usr+w_sub"));

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const QByteArray data = reply->readAll();
        QJsonObject obj = QJsonDocument::fromJson(data).object();

        if (reply->error() != QNetworkReply::NoError) {
            const QString errType = obj["error"].toString();
            // "authorization_pending" is normal — keep polling
            if (errType == QLatin1String("authorization_pending") ||
                errType == QLatin1String("slow_down"))
                return;
            m_pollTimer->stop();
            if (errType == QLatin1String("expired_token"))
                emit errorOccurred(QStringLiteral("Authorization expired. Press Enter to try again."));
            else
                emit errorOccurred(QStringLiteral("Authorization failed: ") + errType);
            return;
        }

        m_pollTimer->stop();
        m_deviceCode.clear();

        m_accessToken  = obj["access_token"].toString();
        m_refreshToken = obj["refresh_token"].toString();
        int expiresIn  = obj["expires_in"].toInt(3600);
        m_tokenExpiry  = QDateTime::currentDateTimeUtc().addSecs(expiresIn);

        // v1 token response includes user info directly
        QJsonObject user = obj["user"].toObject();
        qint64 uid = user["userId"].toInteger(user["userId"].toInt());
        m_userId      = QString::number(uid);
        m_countryCode = user["countryCode"].toString();
        if (m_countryCode.isEmpty()) m_countryCode = QStringLiteral("US");

        QJsonObject auth;
        auth["access_token"]  = m_accessToken;
        auth["refresh_token"] = m_refreshToken;
        auth["expires_at"]    = m_tokenExpiry.toSecsSinceEpoch();
        auth["userId"]        = m_userId;
        auth["countryCode"]   = m_countryCode;
        saveAuth(auth);

        emit authSuccess();
        emit authStateChanged();
    });
}

void TidalBackend::refreshToken() {
    QUrl url(TIDAL_AUTH + "/token");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Authorization", tidalBasicAuth());

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"),     TIDAL_CLIENT);
    body.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"),    QStringLiteral("refresh_token"));

    QNetworkReply *reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("[TidalBackend] Token refresh failed: %s",
                     qPrintable(reply->errorString()));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_accessToken = obj["access_token"].toString();
        int expiresIn = obj["expires_in"].toInt(3600);
        m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(expiresIn);

        QJsonObject auth = loadAuth();
        auth["access_token"] = m_accessToken;
        auth["expires_at"]   = m_tokenExpiry.toSecsSinceEpoch();
        saveAuth(auth);
        emit authStateChanged();
    });
}

void TidalBackend::logout() {
    m_pollTimer->stop();
    QFile::remove(m_dataRoot + "/tidal_auth.json");
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userId.clear();
    m_deviceCode.clear();
    m_tokenExpiry = QDateTime();
    emit logoutComplete();
    emit authStateChanged();
}

// ---------------------------------------------------------------------------
// HTTP helper
// ---------------------------------------------------------------------------

QNetworkReply *TidalBackend::tidalGet(const QUrl &url) {
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());
    req.setRawHeader("Accept", "application/json");
    return m_nam->get(req);
}

// ---------------------------------------------------------------------------
// Item formatters
// ---------------------------------------------------------------------------

QVariantMap TidalBackend::formatTrack(const QJsonObject &obj) const {
    QString id = QString::number(obj["id"].toInt());

    QString artist;
    QJsonArray artists = obj["artists"].toArray();
    if (!artists.isEmpty())
        artist = artists[0].toObject()["name"].toString();
    if (artist.isEmpty())
        artist = obj["artist"].toObject()["name"].toString();

    QVariantMap m;
    m["id"]         = id;
    m["title"]      = obj["title"].toString();
    m["artist"]     = artist;
    m["album"]      = obj["album"].toObject()["title"].toString();
    m["durationMs"] = obj["duration"].toInt(0) * 1000;
    return m;
}

QVariantMap TidalBackend::formatAlbum(const QJsonObject &obj) const {
    QString id = QString::number(obj["id"].toInt());

    QString artist;
    QJsonArray artists = obj["artists"].toArray();
    if (!artists.isEmpty())
        artist = artists[0].toObject()["name"].toString();
    if (artist.isEmpty())
        artist = obj["artist"].toObject()["name"].toString();

    QVariantMap m;
    m["id"]         = id;
    m["title"]      = obj["title"].toString();
    m["artist"]     = artist;
    m["trackCount"] = obj["numberOfTracks"].toInt(0);
    return m;
}

// ---------------------------------------------------------------------------
// QR code generation (optional — requires libqrencode)
// ---------------------------------------------------------------------------

QString TidalBackend::generateQrData(const QString &text, int &outSize) {
    outSize = 0;
#ifdef TIDAL_HAS_QRCODE
    QRcode *qr = QRcode_encodeString(text.toUtf8().constData(),
                                     0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) return {};
    outSize = qr->width;
    QString result;
    result.reserve(qr->width * qr->width);
    for (int i = 0; i < qr->width * qr->width; ++i)
        result += (qr->data[i] & 1) ? QLatin1Char('1') : QLatin1Char('0');
    QRcode_free(qr);
    return result;
#else
    Q_UNUSED(text)
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Browse — My Playlists
// ---------------------------------------------------------------------------

void TidalBackend::load_playlists() {
    if (m_userId.isEmpty()) { emit errorOccurred(QStringLiteral("Not authenticated")); return; }
    QUrl url(QString("%1/users/%2/playlists").arg(TIDAL_API, m_userId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("limit"),       QStringLiteral("50"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray()) {
            QJsonObject obj = v.toObject();
            QVariantMap m;
            m["id"]          = obj["uuid"].toString();
            m["title"]       = obj["title"].toString();
            m["trackCount"]  = obj["numberOfTracks"].toInt(0);
            m["description"] = obj["description"].toString();
            result << m;
        }
        emit playlistsLoaded(QVariant::fromValue(result));
    });
}

// ---------------------------------------------------------------------------
// Browse — Favorite Tracks
// ---------------------------------------------------------------------------

void TidalBackend::load_favorite_tracks() {
    if (m_userId.isEmpty()) { emit errorOccurred(QStringLiteral("Not authenticated")); return; }
    QUrl url(QString("%1/users/%2/favorites/tracks").arg(TIDAL_API, m_userId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("limit"),       QStringLiteral("100"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray())
            result << QVariant::fromValue(formatTrack(v.toObject()["item"].toObject()));
        emit tracksLoaded(QVariant::fromValue(result));
    });
}

// ---------------------------------------------------------------------------
// Browse — Favorite Albums
// ---------------------------------------------------------------------------

void TidalBackend::load_favorite_albums() {
    if (m_userId.isEmpty()) { emit errorOccurred(QStringLiteral("Not authenticated")); return; }
    QUrl url(QString("%1/users/%2/favorites/albums").arg(TIDAL_API, m_userId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("limit"),       QStringLiteral("100"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray())
            result << QVariant::fromValue(formatAlbum(v.toObject()["item"].toObject()));
        emit albumsLoaded(QVariant::fromValue(result));
    });
}

// ---------------------------------------------------------------------------
// Browse — Mixes
// ---------------------------------------------------------------------------

void TidalBackend::load_mixes() {
    if (m_userId.isEmpty()) { emit errorOccurred(QStringLiteral("Not authenticated")); return; }
    // The v1 mix list lives inside the my_collection page API.
    QUrl url(TIDAL_API + "/pages/my_collection");
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("deviceType"),  QStringLiteral("DESKTOP"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;

        for (const QJsonValue &row : root["rows"].toArray()) {
            for (const QJsonValue &mod : row.toObject()["modules"].toArray()) {
                QJsonObject module = mod.toObject();
                if (module["type"].toString() != QLatin1String("MIX_LIST")) continue;
                for (const QJsonValue &item : module["pagedList"].toObject()["items"].toArray()) {
                    QJsonObject mix = item.toObject();
                    QVariantMap m;
                    m["id"]       = mix["id"].toString();
                    m["title"]    = mix["title"].toString();
                    m["subTitle"] = mix["subTitle"].toString();
                    result << m;
                }
            }
        }
        emit mixesLoaded(QVariant::fromValue(result));
    });
}

// ---------------------------------------------------------------------------
// Browse — Playlist / Album / Mix tracks
// ---------------------------------------------------------------------------

void TidalBackend::load_playlist_tracks(const QString &playlistId) {
    QUrl url(QString("%1/playlists/%2/tracks").arg(TIDAL_API, playlistId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("limit"),       QStringLiteral("100"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray())
            result << QVariant::fromValue(formatTrack(v.toObject()));
        emit tracksLoaded(QVariant::fromValue(result));
    });
}

void TidalBackend::load_album_tracks(const QString &albumId) {
    QUrl url(QString("%1/albums/%2/tracks").arg(TIDAL_API, albumId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray())
            result << QVariant::fromValue(formatTrack(v.toObject()));
        emit tracksLoaded(QVariant::fromValue(result));
    });
}

void TidalBackend::load_mix_tracks(const QString &mixId) {
    QUrl url(QString("%1/mixes/%2/items").arg(TIDAL_API, mixId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList result;
        for (const QJsonValue &v : root["items"].toArray()) {
            QJsonObject item = v.toObject();
            // Mix items are wrapped in a typed container with an "item" key
            QJsonObject track = item.contains("item") ? item["item"].toObject() : item;
            if (track.contains("title"))
                result << QVariant::fromValue(formatTrack(track));
        }
        emit tracksLoaded(QVariant::fromValue(result));
    });
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void TidalBackend::search(const QString &query) {
    QUrl url(TIDAL_API + "/search");
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"),       query);
    q.addQueryItem(QStringLiteral("types"),       QStringLiteral("TRACKS,ALBUMS"));
    q.addQueryItem(QStringLiteral("countryCode"), m_countryCode);
    q.addQueryItem(QStringLiteral("limit"),       QStringLiteral("25"));
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();

        QVariantList tracks;
        for (const QJsonValue &v : root["tracks"].toObject()["items"].toArray())
            tracks << QVariant::fromValue(formatTrack(v.toObject()));

        QVariantList albums;
        for (const QJsonValue &v : root["albums"].toObject()["items"].toArray())
            albums << QVariant::fromValue(formatAlbum(v.toObject()));

        emit searchResultsLoaded(QVariant::fromValue(tracks), QVariant::fromValue(albums));
    });
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void TidalBackend::get_stream_url(const QString &trackId) {
    QString quality = QStringLiteral("HIGH");
    QJsonObject cfg = loadConfig();
    QString saved = cfg["modules"].toObject()["com.240mp.tidal"].toObject()["audio_quality"].toString();
    if (!saved.isEmpty()) quality = saved;

    QUrl url(QString("%1/tracks/%2/streamUrl").arg(TIDAL_API, trackId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("audioquality"),      quality);
    q.addQueryItem(QStringLiteral("playbackmode"),      QStringLiteral("STREAM"));
    q.addQueryItem(QStringLiteral("assetpresentation"), QStringLiteral("FULL"));
    q.addQueryItem(QStringLiteral("countryCode"),       m_countryCode);
    url.setQuery(q);

    QNetworkReply *reply = tidalGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit errorOccurred(reply->errorString()); return; }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QString streamUrl = obj["url"].toString();
        if (streamUrl.isEmpty())
            streamUrl = obj["manifest"].toString();
        if (streamUrl.isEmpty()) {
            emit errorOccurred(QStringLiteral("Could not get stream URL from Tidal"));
            return;
        }
        emit streamUrlReady(streamUrl, m_accessToken);
    });
}

// ---------------------------------------------------------------------------
// Settings dynamic options
// ---------------------------------------------------------------------------

void TidalBackend::get_quality_options() {
    QVariantList options;
    auto add = [&](const char *id, const char *label) {
        QVariantMap m; m["id"] = QString(id); m["label"] = QString(label);
        options << m;
    };
    add("LOW",      "Low (AAC 96 kbps)");
    add("HIGH",     "High (AAC 320 kbps)");
    add("LOSSLESS", "Lossless (FLAC)");
    emit dynamicOptionsReady(QStringLiteral("audio_quality"), QVariant::fromValue(options));
}
