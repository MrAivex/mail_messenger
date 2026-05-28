#ifndef SMTPCLIENT_H
#define SMTPCLIENT_H

#include <QObject>
#include <QSslSocket>
#include <QQueue>
#include <QPair>

class SmtpClient : public QObject
{
    Q_OBJECT
public:
    explicit SmtpClient(QObject *parent = nullptr);
    ~SmtpClient();

    void setCredentials(const QString &email, const QString &password);
    void sendMessage(const QString &to, const QString &subject, const QString &body);

signals:
    void messageSent();
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    void sendCommand(const QString &cmd);
    QByteArray encodeBase64(const QString &input);
    void processQueue();

    QSslSocket *socket;
    QString email;
    QString password;
    QString recipient;
    QString messageBody;
    int step;

    QQueue<QPair<QString, QString>> messageQueue;   // пара (получатель, полное тело)
    bool sendingInProgress = false;
};

#endif // SMTPCLIENT_H
