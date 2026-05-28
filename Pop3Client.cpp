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
    // Считываем всё, что пришло в сокет, и добавляем в накопительный буфер
    buffer.append(socket->readAll());

    // Важно: обрабатываем буфер, пока в нем есть символ конца строки
    while (buffer.contains('\n')) {
        int nIndex = buffer.indexOf('\n');
        // Извлекаем одну строку
        QByteArray lineData = buffer.left(nIndex).trimmed();
        // УДАЛЯЕМ извлеченную строку из буфера (включая символ \n)
        buffer.remove(0, nIndex + 1);

        if (lineData.isEmpty() && !mailRetrievalInProgress) continue;

        QString line = QString::fromUtf8(lineData);

        // УБЕРИТЕ или закомментируйте qDebug здесь, если писем много!
        // qDebug() << "S:" << line;

        if (line.startsWith("-ERR")) {
            emit errorOccurred("POP3 Error: " + line);
            socket->disconnectFromHost();
            buffer.clear();
            return;
        }

        // Переключение состояний (Steps)
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
                    currentMail.clear();
                    mailRetrievalInProgress = true;
                    sendCommand("RETR 1\r\n");
                    step = 4;
                } else {
                    sendCommand("QUIT\r\n");
                    step = 5;
                }
            }
        } else if (step == 4) {
            // Режим накопления письма
            if (line == "." || line.trimmed() == ".") {
                mailRetrievalInProgress = false;
                QList<Message> msgs = processRetrievedMessage(currentMail);
                if (!msgs.isEmpty()) {
                    emit newMessages(msgs);
                }

                if (currentMessageIndex < totalMessages) {
                    currentMessageIndex++;
                    currentMail.clear();
                    mailRetrievalInProgress = true;
                    sendCommand(QString("RETR %1\r\n").arg(currentMessageIndex));
                } else {
                    sendCommand("QUIT\r\n");
                    step = 5;
                }
            } else {
                // Добавляем строку в текущее письмо
                currentMail.append(lineData + "\n");
            }
        } else if (step == 5 && line.startsWith("+OK")) {
            socket->disconnectFromHost();
            isProcessing = false; // ОСВОБОЖДАЕМ флаг здесь
        }
    }
}

void Pop3Client::onError(QAbstractSocket::SocketError error)
{
    isProcessing = false;
    Q_UNUSED(error);
    emit errorOccurred(socket->errorString());
    timer->stop();
}

void Pop3Client::sendCommand(const QString &cmd)
{
    socket->write(cmd.toUtf8());
    // qDebug() << "POP3 C:" << cmd.trimmed();
}

QList<Message> Pop3Client::processRetrievedMessage(const QByteArray &rawMail)
{
    QList<Message> messages;
    QString mail = QString::fromUtf8(rawMail);
    if (mail.isEmpty()) return messages;

    Message msg;
    msg.incoming = true;

    // 1. Извлекаем Отправителя
    QRegularExpression fromRe("From:.*<([^>]+)>", QRegularExpression::CaseInsensitiveOption);
    auto fromMatch = fromRe.match(mail);
    msg.from = fromMatch.hasMatch() ? fromMatch.captured(1).trimmed() : "Unknown";

    // 2. ID (используем для уникальности)
    QRegularExpression msgIdRe("Message-ID:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
    auto msgIdMatch = msgIdRe.match(mail);
    msg.messageId = msgIdMatch.hasMatch() ? msgIdMatch.captured(1).trimmed() : QString::number(qHash(mail));

    // 3. Улучшенный поиск текста
    QString finalBody = "";
    int headerEnd = mail.indexOf("\r\n\r\n");

    if (headerEnd != -1) {
        QString fullBody = mail.mid(headerEnd + 4);

        // Если это MIME (сложное письмо)
        if (fullBody.contains("Content-Type:", Qt::CaseInsensitive)) {
            // Ищем сначала text/plain, если нет - text/html
            QRegularExpression mimeRe("Content-Type: text/(plain|html);.*?\r\n\r\n(.*?)(?=\r\n--|$)",
                                      QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);

            auto matchIt = mimeRe.globalMatch(fullBody);
            QString htmlContent = "";
            QString plainContent = "";

            while (matchIt.hasNext()) {
                auto match = matchIt.next();
                QString type = match.captured(1).toLower();
                QString content = match.captured(2).trimmed();

                // Проверяем кодировку конкретной части
                if (fullBody.contains("Content-Transfer-Encoding: base64", Qt::CaseInsensitive)) {
                    content = QString::fromUtf8(QByteArray::fromBase64(content.toUtf8()));
                } else if (fullBody.contains("Content-Transfer-Encoding: quoted-printable", Qt::CaseInsensitive)) {
                    content.replace("=\r\n", "");
                    content.replace("=\n", "");
                    QRegularExpression hexRe("=([0-9A-F]{2})");
                    auto it = hexRe.globalMatch(content);
                    while (it.hasNext()) {
                        auto m = it.next();
                        bool ok;
                        char ch = static_cast<char>(m.captured(1).toInt(&ok, 16));
                        if (ok) content.replace(m.captured(0), QString(ch));
                    }
                }

                if (type == "plain") plainContent = content;
                else if (type == "html") htmlContent = content;
            }
            finalBody = !plainContent.isEmpty() ? plainContent : htmlContent;
        } else {
            finalBody = fullBody;
        }

        // Чистим от HTML
        finalBody.remove(QRegularExpression("<[^>]*>"));
        finalBody.replace("&nbsp;", " ");
        finalBody.replace("&quot;", "\"");
        finalBody.replace("&amp;", "&");

        // Убираем точку протокола в конце
        if (finalBody.endsWith("\r\n.")) finalBody.chop(3);
    }

    msg.text = finalBody.trimmed();

    // Если всё еще пусто, берем первые 200 символов сырого тела для отладки
    if (msg.text.isEmpty()) {
        msg.text = "(Сложное содержимое: вложения или графика)";
    }

    msg.dateTime = QDateTime::currentDateTime();
    msg.to = email;

    messages.append(msg);
    return messages;
}
