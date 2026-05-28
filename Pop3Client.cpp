#include "Pop3Client.h"
#include <QDebug>
#include <QRegularExpression>
#include <QDateTime>

Pop3Client::Pop3Client(QObject *parent)
    : QObject(parent), socket(new QSslSocket(this)), timer(new QTimer(this)),
    step(0), totalMessages(0), currentMessageIndex(0),
    mailRetrievalInProgress(false), expectingDeleResponse(false),
    expectingQuitResponse(false)
{
    connect(timer, &QTimer::timeout, this, &Pop3Client::checkMail);
    connect(socket, &QSslSocket::readyRead, this, &Pop3Client::onReadyRead);
    connect(socket, &QSslSocket::errorOccurred, this, &Pop3Client::onError);
}

Pop3Client::~Pop3Client()
{
    stopChecking();
}

void Pop3Client::setCredentials(const QString &email, const QString &password)
{
    this->email = email;
    this->password = password;
}

void Pop3Client::startChecking(int intervalMs)
{
    timer->start(intervalMs);
}

void Pop3Client::stopChecking()
{
    timer->stop();
    if (socket->state() == QAbstractSocket::ConnectedState)
        socket->disconnectFromHost();
}

void Pop3Client::checkMail()
{
    if (isProcessing) {
        // qDebug() << "Already checking mail, skipping...";
        return;
    }

    // Если сокет еще занят прошлым соединением — сбрасываем его
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
    }

    // Сброс всех флагов и буферов
    step = 0;
    buffer.clear();
    currentMail.clear();
    mailRetrievalInProgress = false;

    qDebug() << "Connecting to POP3...";
    socket->connectToHostEncrypted("pop.mail.ru", 995);
}

void Pop3Client::onReadyRead()
{
    // Добавляем новые данные в буфер
    buffer.append(socket->readAll());

    // Если мы в режиме скачивания письма (RETR), нам НЕЛЬЗЯ читать по строкам,
    // потому что письмо — это один большой кусок данных.
    if (step == 4) {
        // Проверяем, пришел ли маркер конца письма POP3: "\r\n.\r\n"
        if (buffer.contains("\r\n.\r\n")) {
            int dotIndex = buffer.indexOf("\r\n.\r\n");

            // Забираем всё тело письма до точки
            QByteArray fullMailData = buffer.left(dotIndex);
            // Удаляем обработанное письмо и маркер из буфера
            buffer.remove(0, dotIndex + 5);

            mailRetrievalInProgress = false;

            // Парсим накопленное письмо (используем ваш метод)
            QList<Message> msgs = processRetrievedMessage(fullMailData);
            if (!msgs.isEmpty()) {
                emit newMessages(msgs);
            }

            // Переходим к следующему или выходим
            if (currentMessageIndex < totalMessages) {
                currentMessageIndex++;
                sendCommand(QString("RETR %1\r\n").arg(currentMessageIndex));
            } else {
                sendCommand("QUIT\r\n");
                step = 5;
            }
        }
        return; // Выходим, пока не получим всю точку целиком
    }

    // Логика для команд (USER, PASS, STAT) — здесь читаем построчно
    while (buffer.contains('\n')) {
        int nIndex = buffer.indexOf('\n');
        QByteArray lineData = buffer.left(nIndex).trimmed();
        buffer.remove(0, nIndex + 1);

        if (lineData.isEmpty()) continue;
        QString line = QString::fromUtf8(lineData);

        if (line.startsWith("-ERR")) {
            emit errorOccurred("POP3 Error: " + line);
            socket->disconnectFromHost();
            buffer.clear();
            return;
        }

        if (step == 0 && line.startsWith("+OK")) {
            sendCommand("USER " + email + "\r\n");
            step = 1;
        } else if (step == 1 && line.startsWith("+OK")) {
            sendCommand("PASS " + password + "\r\n");
            step = 2;
        } else if (step == 2 && line.startsWith("+OK")) {
            sendCommand("STAT\r\n");
            step = 3;
        } else if (step == 3 && line.startsWith("+OK")) {
            QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                totalMessages = parts.at(1).toInt();
                if (totalMessages > 0) {
                    currentMessageIndex = 1;
                    step = 4; // Переходим в режим RETR
                    sendCommand("RETR 1\r\n");
                } else {
                    sendCommand("QUIT\r\n");
                    step = 5;
                }
            }
        } else if (step == 5 && line.startsWith("+OK")) {
            socket->disconnectFromHost();
            isProcessing = false;
        }
    }
}

void Pop3Client::onError(QAbstractSocket::SocketError error)
{
    isProcessing = false;

    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }

    emit errorOccurred(socket->errorString());
}

void Pop3Client::sendCommand(const QString &cmd)
{
    socket->write(cmd.toUtf8());
}

QList<Message> Pop3Client::processRetrievedMessage(const QByteArray &rawMail)
{
    QList<Message> messages;
    QString mail = QString::fromUtf8(rawMail);
    if (mail.isEmpty()) return messages;

    Message msg;
    msg.incoming = true;
    msg.dateTime = QDateTime::currentDateTime();
    msg.to = email;

    // 1. Извлекаем отправителя и ID (ваша текущая логика верна)
    QRegularExpression fromRe("From:\\s*(?:.*<)?([^>\\s]+@[^>\\s]+)(?:>)?", QRegularExpression::CaseInsensitiveOption);
    msg.from = fromRe.match(mail).captured(1).trimmed().toLower();

    QRegularExpression msgIdRe("Message-ID:\\s*<([^>]+)>", QRegularExpression::CaseInsensitiveOption);
    msg.messageId = msgIdRe.match(mail).captured(1).trimmed();
    if(msg.messageId.isEmpty()) msg.messageId = QString::number(qHash(mail));

    // 2. Находим тело письма
    int headerEnd = mail.indexOf("\r\n\r\n");
    QString body = (headerEnd != -1) ? mail.mid(headerEnd + 4) : mail;

    // 3. ДЕКОДИРОВАНИЕ BASE64 (Решение вашей проблемы)
    // Если в письме есть заголовок о Base64
    if (mail.contains("Content-Transfer-Encoding: base64", Qt::CaseInsensitive)) {
        // Извлекаем только блок данных (до следующего разделителя или конца)
        QRegularExpression base64DataRe("(?:\r\n\r\n|^)([A-Za-z0-9+/=\r\n]+)(?=\r\n--|$)");
        auto match = base64DataRe.match(body);
        if (match.hasMatch()) {
            QByteArray decoded = QByteArray::fromBase64(match.captured(1).toUtf8());
            body = QString::fromUtf8(decoded);
        }
    }

    // 4. Очистка от HTML и подписей Mail.ru
    body.remove(QRegularExpression("<[^>]*>"));
    body = body.split("--").at(0); // Отрезаем подписи после разделителя "--"

    msg.text = body.trimmed();
    if (msg.text.isEmpty()) msg.text = "[Письмо без текста]";

    messages.append(msg);
    return messages;
}
