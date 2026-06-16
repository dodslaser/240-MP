#pragma once
#include <QObject>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QDateTime>
#include <QJsonObject>

class TidalBackend : public QObject {
    Q_OBJECT
public:
    explicit TidalBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    Q_INVOKABLE QString get_auth_state();

    Q_INVOKABLE void start_auth();
    Q_INVOKABLE void logout();

    Q_INVOKABLE void load_playlists();
    Q_INVOKABLE void load_favorite_tracks();
    Q_INVOKABLE void load_favorite_albums();
    Q_INVOKABLE void load_mixes();
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void load_playlist_tracks(const QString &playlistId);
    Q_INVOKABLE void load_album_tracks(const QString &albumId);
    Q_INVOKABLE void load_mix_tracks(const QString &mixId);
    Q_INVOKABLE void get_stream_url(const QString &trackId);
    Q_INVOKABLE void get_quality_options();

signals:
    // Emitted once the device authorization request completes.
    // qrData is a row-major string of '0'/'1' chars (length qrSize*qrSize),
    // or empty if QR generation is not available.
    void deviceAuthReady(const QString &userCode,
                         const QString &verificationUri,
                         const QString &qrData,
                         int qrSize);
    void authSuccess();
    void authStateChanged();
    void logoutComplete();
    void playlistsLoaded(const QVariant &items);
    void tracksLoaded(const QVariant &items);
    void albumsLoaded(const QVariant &items);
    void mixesLoaded(const QVariant &items);
    void searchResultsLoaded(const QVariant &tracks, const QVariant &albums);
    void streamUrlReady(const QString &url, const QString &accessToken);
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void errorOccurred(const QString &message);

private slots:
    void pollForToken();

private:
    QJsonObject loadAuth() const;
    void saveAuth(const QJsonObject &auth);
    QJsonObject loadConfig() const;
    void refreshToken();

    QNetworkReply *tidalGet(const QUrl &url);
    QVariantMap formatTrack(const QJsonObject &obj) const;
    QVariantMap formatAlbum(const QJsonObject &obj) const;
    static QString generateQrData(const QString &text, int &outSize);

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam;
    QTimer *m_pollTimer;

    QString m_deviceCode;
    int     m_pollInterval = 2;

    QString   m_accessToken;
    QString   m_refreshToken;
    QString   m_userId;
    QString   m_countryCode;
    QDateTime m_tokenExpiry;
};
