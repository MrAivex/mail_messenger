#ifndef MESSAGESTORE_H
#define MESSAGESTORE_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QList>
#include <QSqlDatabase> // Добавить
#include "Message.h"


class MessageStore : public QObject
{
    Q_OBJECT
public:
    explicit MessageStore(QObject *parent = nullptr);

    // Добавление исходящего сообщения
    void addOutgoingMessage(const QString &from, const QString &to,
                            const QString &text, const QString &messageId,
                            const QDateTime &dateTime = QDateTime::currentDateTime());

    // Добавление входящего сообщения
    void addIncomingMessage(const QString &from, const QString &to,
                            const QString &text, const QString &messageId,
                            const QDateTime &dateTime);

    // Получить всю переписку с указанным адресом (в хронологическом порядке)
    QList<Message> getConversation(const QString &address) const;

    // Список всех контактов (адресов), с которыми велась переписка,
    // отсортированный по времени последней активности (сначала недавние)
    QStringList contactAddresses() const;

    void saveCredentials(const QString &email, const QString &pass);  // База данных
    QPair<QString, QString> loadCredentials();
    void loadMessagesFromDb(const QString &myEmail);

    void addContact(const QString &email, const QString &name = "");
    bool isContact(const QString &email) const;
    QStringList getFullContactList() const; // Чтобы загрузить их при старте

    void clearCredentials();
    bool hasMessageId(const QString &id) const;

signals:
    void messageAdded(const QString &address);

private:
    void initDb(); // Метод инициализации
    QMap<QString, QList<Message>> m_conversations;
    QSqlDatabase db;
};

#endif // MESSAGESTORE_H
