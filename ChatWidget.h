#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include "Message.h"

class SmtpClient;
class Pop3Client;
class MessageStore;

class ChatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChatWidget(const QString &email, const QString &password,
                        MessageStore *store, QWidget *parent = nullptr);
    ~ChatWidget();

private slots:
    void onContactSelected(QListWidgetItem *item);
    void onStartChat();
    void sendMessage();
    void onNewMessages(const QList<Message> &messages);
    void onMessageAdded(const QString &address);
    void onSmtpError(const QString &error);
    void onPop3Error(const QString &error);
    void onAddContactClicked();

private:
    void loadChatWith(const QString &address);
    void addMessageToChat(const Message &msg);
    void updateContactList();
    void updateChatView(const QString &address);

    QString myEmail;
    QString myPassword;
    SmtpClient *smtp;
    Pop3Client *pop3;
    MessageStore *store;

    QListWidget *contactList;
    QLineEdit *toEdit;
    QTextEdit *chatView;
    QLineEdit *messageEdit;
    QPushButton *sendButton;
    QPushButton *startChatButton;
    QLabel *currentContactLabel;

    QString activeContact;

    QPushButton *addContactButton;
};

#endif // CHATWIDGET_H
