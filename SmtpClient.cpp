#include "SmtpClient.h"
#include <QDebug>

SmtpClient::SmtpClient(QObject *parent)
    : QObject(parent), socket(new QSslSocket(this)), step(0)
{
    connect(socket, &QSslSocket::connected, this, &SmtpClient::onConnected);
    connect(socket, &QSslSocket::readyRead, this, &SmtpClient::onReadyRead);
    connect(socket, &QSslSocket::errorOccurred, this, &SmtpClient::onErrorOccurred);
}

SmtpClient::~SmtpClient()
{
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
}

void SmtpClient::setCredentials(const QString &email, const QString &password)
{
    this->email = email;
    this->password = password;
}

void SmtpClient::sendMessage(const QString &to, const QString &subject, const QString &body)
{
    // Белая валидация: email не должен содержать \r, \n и должен содержать '@'
    if (to.contains('\r') || to.contains('\n') || !to.contains('@')) {
        emit errorOccurred("Invalid recipient email");
        return;
    }

    QString fullMsg = "From: <" + email + ">\r\n"
                                          "To: <" + to + ">\r\n"
                             "Subject: " + subject + "\r\n"
                                  "Content-Type: text/plain; charset=utf-8\r\n\r\n"
                      + body + "\r\n.\r\n";

    messageQueue.enqueue(qMakePair(to, fullMsg));
    if (!sendingInProgress)
        processQueue();
}

void SmtpClient::processQueue()
{
    if (messageQueue.isEmpty()) {
        sendingInProgress = false;
        return;
    }
    sendingInProgress = true;
    auto pair = messageQueue.dequeue();
    recipient = pair.first;
    messageBody = pair.second;

    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
        socket->close();
    }
    socket->connectToHostEncrypted("smtp.mail.ru", 465);
    step = 0;
}

void SmtpClient::onConnected() {}

void SmtpClient::onReadyRead()
{
    while (socket->canReadLine()) {
        QByteArray line = socket->readLine();
        qDebug() << "S:" << line.trimmed();

        if (step == 0) {
            sendCommand("EHLO localhost\r\n");
            step = 1;
        } else if (line.startsWith("250") && step == 1) {
            sendCommand("AUTH LOGIN\r\n");
            step = 2;
        } else if (line.startsWith("334 VXNlcm5hbWU6") && step == 2) {
            sendCommand(encodeBase64(email) + "\r\n");
            step = 3;
        } else if (line.startsWith("334 UGFzc3dvcmQ6") && step == 3) {
            sendCommand(encodeBase64(password) + "\r\n");
            step = 4;
        } else if (line.startsWith("235") && step == 4) {
            sendCommand("MAIL FROM:<" + email.toUtf8() + ">\r\n");
            step = 5;
        } else if (line.startsWith("250") && step == 5) {
            sendCommand("RCPT TO:<" + recipient.toUtf8() + ">\r\n");
            step = 6;
        } else if (line.startsWith("250") && step == 6) {
            sendCommand("DATA\r\n");
            step = 7;
        } else if (line.startsWith("354") && step == 7) {
            sendCommand(messageBody);
            step = 8;
        } else if (line.startsWith("250") && step == 8) {
            sendCommand("QUIT\r\n");
            step = 9;
        } else if (line.startsWith("221") && step == 9) {
            emit messageSent();
            socket->disconnectFromHost();
            step = 0;
            processQueue();   // запуск следующего письма из очереди
        } else if (line.startsWith("5") || line.startsWith("4")) {
            emit errorOccurred("SMTP error: " + line);
            socket->disconnectFromHost();
            sendingInProgress = false;   // чтобы очередь не подвисла
        }
    }
}

void SmtpClient::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit errorOccurred(socket->errorString());
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
    sendingInProgress = false;
}

void SmtpClient::sendCommand(const QString &cmd)
{
    socket->write(cmd.toUtf8());
    qDebug() << "C:" << cmd.trimmed();
}

QByteArray SmtpClient::encodeBase64(const QString &input)
{
    return input.toUtf8().toBase64();
}
