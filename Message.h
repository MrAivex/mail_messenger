#ifndef MESSAGE_H
#define MESSAGE_H

#include <QString>
#include <QDateTime>

struct Message {
    QString from;
    QString to;
    QString text;
    QDateTime dateTime;
    bool incoming;          // true – входящее, false – исходящее
    QString messageId;      // уникальный идентификатор письма (Message-ID)

    bool operator==(const Message &other) const {
        // Если ID сообщения совпадает, значит это одно и то же письмо
        return (messageId == other.messageId) && (!messageId.isEmpty());
    }
};

#endif // MESSAGE_H
