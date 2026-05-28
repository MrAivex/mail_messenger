#ifndef POP3CLIENT_H
#define POP3CLIENT_H

#include <QObject>
#include <QSslSocket>
#include <QTimer>
#include "Message.h"

class Pop3Client : public QObject
{
    Q_OBJECT
public:
    explicit Pop3Client(QObject *parent = nullptr);
    ~Pop3Client();

    void setCredentials(const QString &email, const QString &password);
    void startChecking(int intervalMs = 10000);
    void stopChecking();

signals:
    void newMessages(const QList<Message> &messages);
    void errorOccurred(const QString &error);

private slots:
    void checkMail();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);

private:
    void sendCommand(const QString &cmd);
    QList<Message> processRetrievedMessage(const QByteArray &rawMail);

    QSslSocket *socket;
    QString email;
    QString password;
    QTimer *timer;
    int step;
    int totalMessages;
    int currentMessageIndex;
    QByteArray buffer;
    QByteArray currentMail;
    bool mailRetrievalInProgress;
    bool expectingDeleResponse;
    bool expectingQuitResponse;

    bool isProcessing = false;
};

#endif // POP3CLIENT_H
