#include "MessageStore.h"
#include <algorithm>
#include <QDateTime>

#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>

#include <QDebug>
#include <QStandardPaths>

MessageStore::MessageStore(QObject *parent)
    : QObject(parent)
{
    initDb();
}

void MessageStore::initDb()
{
    // Создаем БД в папке с программой
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("messenger.db");

    if (!db.open()) {
        qDebug() << "DB Error:" << db.lastError().text();
        return;
    }

    QSqlQuery query;
    // Таблица для хранения логина/пароля
    query.exec("CREATE TABLE IF NOT EXISTS auth ("
               "id INTEGER PRIMARY KEY CHECK (id = 1), "
               "email TEXT, "
               "password TEXT)");

    // Таблица для сообщений (чтобы они не пропадали)
    query.exec("CREATE TABLE IF NOT EXISTS messages ("
               "msg_id TEXT PRIMARY KEY, "
               "sender TEXT, "
               "receiver TEXT, "
               "body TEXT, "
               "timestamp TEXT)");

    query.exec("CREATE TABLE IF NOT EXISTS contacts ("
               "email TEXT PRIMARY KEY, "
               "name TEXT)");
}

void MessageStore::saveCredentials(const QString &email, const QString &pass)
{
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO auth (id, email, password) VALUES (1, ?, ?)");
    query.addBindValue(email);
    query.addBindValue(pass);
    if(!query.exec()) qDebug() << "Save error:" << query.lastError().text();
}

QPair<QString, QString> MessageStore::loadCredentials()
{
    QSqlQuery query("SELECT email, password FROM auth WHERE id = 1");
    if (query.next()) {
        return {query.value(0).toString(), query.value(1).toString()};
    }
    return {"", ""};
}

void MessageStore::addOutgoingMessage(const QString &from, const QString &to,
                                      const QString &text, const QString &messageId,
                                      const QDateTime &dateTime)
{
    if (hasMessageId(messageId)) return;

    Message msg;
    msg.from = from;
    msg.to = to;
    msg.text = text;
    msg.dateTime = dateTime.isValid() ? dateTime : QDateTime::currentDateTime();
    msg.incoming = false;
    msg.messageId = messageId;

    m_conversations[to].append(msg);

    // СОХРАНЕНИЕ В БД
    QSqlQuery query;
    query.prepare("INSERT INTO messages (msg_id, sender, receiver, body, timestamp) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(messageId);
    query.addBindValue(from);
    query.addBindValue(to);
    query.addBindValue(text);
    query.addBindValue(msg.dateTime.toString(Qt::ISODate));
    query.exec();

    emit messageAdded(to);
}

void MessageStore::addIncomingMessage(const QString &from, const QString &to,
                                      const QString &text, const QString &messageId,
                                      const QDateTime &dateTime)
{
    if (!isContact(from)) return;

    if (hasMessageId(messageId)) return;

    Message msg;
    msg.from = from;
    msg.to = to;
    msg.text = text;
    msg.dateTime = dateTime.isValid() ? dateTime : QDateTime::currentDateTime();
    msg.incoming = true;
    msg.messageId = messageId;

    m_conversations[from].append(msg);
    emit messageAdded(from);
}

QList<Message> MessageStore::getConversation(const QString &address) const
{
    return m_conversations.value(address);
}

QStringList MessageStore::contactAddresses() const
{
    QStringList keys = m_conversations.keys();
    std::sort(keys.begin(), keys.end(), [this](const QString &a, const QString &b) {
        const QList<Message> &listA = m_conversations.value(a);
        const QList<Message> &listB = m_conversations.value(b);
        if (listA.isEmpty() || listB.isEmpty()) return !listA.isEmpty();
        return listA.last().dateTime > listB.last().dateTime;
    });
    return keys;
}

bool MessageStore::hasMessageId(const QString &id) const
{
    if (id.isEmpty()) return false;
    // Используем итератор для быстрого поиска
    for (auto it = m_conversations.constBegin(); it != m_conversations.constEnd(); ++it) {
        for (const Message &m : it.value()) {
            if (m.messageId == id) return true;
        }
    }
    return false;
}

void MessageStore::addContact(const QString &email, const QString &name) {
    QSqlQuery query;
    query.prepare("INSERT OR REPLACE INTO contacts (email, name) VALUES (?, ?)");
    query.addBindValue(email.toLower().trimmed());
    query.addBindValue(name);
    query.exec();
}

bool MessageStore::isContact(const QString &email) const {
    QSqlQuery query;
    query.prepare("SELECT email FROM contacts WHERE email = ?");
    query.addBindValue(email.toLower().trimmed());
    query.exec();
    return query.next(); // Если есть строка в результате — значит контакт наш
}

void MessageStore::loadMessagesFromDb(const QString &myEmail) {
    QSqlQuery contactQuery("SELECT email FROM contacts");
    while (contactQuery.next()) {
        QString email = contactQuery.value(0).toString().toLower().trimmed();
        if (!m_conversations.contains(email)) {
            m_conversations[email] = QList<Message>(); // Резервируем место для истории
        }
    }

    QSqlQuery query("SELECT sender, receiver, body, timestamp, msg_id FROM messages");
    while (query.next()) {
        Message msg;
        msg.from = query.value(0).toString();
        msg.to = query.value(1).toString();
        msg.text = query.value(2).toString();
        msg.dateTime = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
        msg.messageId = query.value(4).toString();

        // ПРАВИЛЬНАЯ ЛОГИКА:
        // Если отправитель — это я, значит сообщение исходящее (incoming = false)
        msg.incoming = (msg.from != myEmail);

        // Определяем, в чью переписку положить это сообщение
        // Если я отправитель, кладем в папку получателя. Если я получатель — в папку отправителя.
        QString partner = (msg.from == myEmail) ? msg.to : msg.from;

        if (!hasMessageId(msg.messageId)) {
            m_conversations[partner].append(msg);
        }
    }
}

QStringList MessageStore::getFullContactList() const {
    QStringList contacts;

    // 1. Сначала берем тех, кто сохранен в таблице контактов
    QSqlQuery query("SELECT email FROM contacts");
    while (query.next()) {
        QString email = query.value(0).toString().toLower().trimmed();
        if (!contacts.contains(email)) contacts << email;
    }

    // 2. Добавляем тех, от кого есть сообщения в памяти (из таблицы messages)
    QStringList keys = m_conversations.keys();
    for (const QString &email : keys) {
        if (!contacts.contains(email)) contacts << email;
    }

    return contacts;
}

void MessageStore::clearCredentials()
{
    QSqlQuery query;
    query.exec("DELETE FROM auth"); // Удаляем единственную запись с логином/паролем
}
