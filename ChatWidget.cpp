#include "ChatWidget.h"
#include "SmtpClient.h"
#include "Pop3Client.h"
#include "MessageStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDateTime>
#include <QUuid>
#include <QInputDialog>
#include <QMessageBox>

ChatWidget::ChatWidget(const QString &email, const QString &password,
                       MessageStore *store, QWidget *parent)
    : QWidget(parent), myEmail(email), myPassword(password), store(store)
{
    smtp = new SmtpClient(this);
    smtp->setCredentials(email, password);
    connect(smtp, &SmtpClient::errorOccurred, this, &ChatWidget::onSmtpError);

    pop3 = new Pop3Client(this);
    pop3->setCredentials(email, password);
    connect(pop3, &Pop3Client::newMessages, this, &ChatWidget::onNewMessages);
    connect(pop3, &Pop3Client::errorOccurred, this, &ChatWidget::onPop3Error);
    pop3->startChecking(10000);

    connect(store, &MessageStore::messageAdded, this, &ChatWidget::onMessageAdded);

    addContactButton = new QPushButton("➕ Добавить контакт", this);
    addContactButton->setStyleSheet("padding: 8px; font-weight: bold;");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *toLayout = new QHBoxLayout;
    QLabel *toLabel = new QLabel("Кому:");
    toEdit = new QLineEdit;
    toEdit->setPlaceholderText("email@example.com");
    startChatButton = new QPushButton("Начать чат");
    toLayout->addWidget(toLabel);
    toLayout->addWidget(toEdit);
    toLayout->addWidget(startChatButton);

    currentContactLabel = new QLabel("Выберите контакт или введите адрес");
    chatView = new QTextEdit;
    chatView->setReadOnly(true);

    QHBoxLayout *sendLayout = new QHBoxLayout;
    messageEdit = new QLineEdit;
    messageEdit->setPlaceholderText("Введите сообщение...");
    sendButton = new QPushButton("Отправить");
    sendLayout->addWidget(messageEdit);
    sendLayout->addWidget(sendButton);

    QLabel *contactsLabel = new QLabel("Контакты");
    contactList = new QListWidget;

    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(contactsLabel);
    leftLayout->addWidget(addContactButton); // Кнопка сверху
    leftLayout->addWidget(contactList);      // Список под ней

    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addLayout(toLayout);
    rightLayout->addWidget(currentContactLabel);
    rightLayout->addWidget(chatView);
    rightLayout->addLayout(sendLayout);
    rightWidget->setLayout(rightLayout);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter);

    connect(contactList, &QListWidget::itemClicked, this, &ChatWidget::onContactSelected);
    connect(startChatButton, &QPushButton::clicked, this, &ChatWidget::onStartChat);
    connect(toEdit, &QLineEdit::returnPressed, this, &ChatWidget::onStartChat);
    connect(sendButton, &QPushButton::clicked, this, &ChatWidget::sendMessage);
    connect(messageEdit, &QLineEdit::returnPressed, this, &ChatWidget::sendMessage);
    connect(addContactButton, &QPushButton::clicked, this, &ChatWidget::onAddContactClicked);

    updateContactList();
}

ChatWidget::~ChatWidget()
{
    pop3->stopChecking();
}

void ChatWidget::onContactSelected(QListWidgetItem *item)
{
    QString address = item->text();
    loadChatWith(address);
    toEdit->setText(address);
}

void ChatWidget::onStartChat()
{
    QString address = toEdit->text().trimmed();
    if (address.isEmpty() || !address.contains('@') || address.contains('\r') || address.contains('\n'))
        return;
    loadChatWith(address);
    updateContactList();
}

void ChatWidget::loadChatWith(const QString &address)
{
    activeContact = address;
    currentContactLabel->setText("Чат с " + address);
    updateChatView(address);
}

void ChatWidget::updateChatView(const QString &address)
{
    chatView->clear();
    QList<Message> msgs = store->getConversation(address);
    for (const Message &msg : msgs) {
        addMessageToChat(msg);
    }
}

void ChatWidget::sendMessage()
{
    if (activeContact.isEmpty()) {
        QString addr = toEdit->text().trimmed();
        if (addr.contains('@') && !addr.contains('\r') && !addr.contains('\n'))
            onStartChat();
        else
            return;
    }
    QString text = messageEdit->text().trimmed();
    if (text.isEmpty()) return;

    store->addOutgoingMessage(myEmail, activeContact, text,
                              QUuid::createUuid().toString(QUuid::WithoutBraces),
                              QDateTime::currentDateTime());

    smtp->sendMessage(activeContact, "Chat", text);
    messageEdit->clear();
}

void ChatWidget::onNewMessages(const QList<Message> &messages)
{
    for (const Message &msg : messages) {
        // Игнорируем свои же письма, чтобы не дублировать отправленные
        if (msg.from == myEmail)
            continue;
        store->addIncomingMessage(msg.from, myEmail, msg.text, msg.messageId, msg.dateTime);
    }
}

void ChatWidget::onMessageAdded(const QString &address)
{
    updateContactList();
    if (activeContact == address)
        updateChatView(address);
}

void ChatWidget::addMessageToChat(const Message &msg)
{
    QString prefix = msg.incoming ? (msg.from + ": ") : (myEmail + ": ");
    chatView->append("[" + msg.dateTime.toString("hh:mm") + "] " + prefix + msg.text);
}

void ChatWidget::updateContactList()
{
    // Важно: берем контакты из БД + из памяти
    QStringList contacts = store->getFullContactList();

    QString current = contactList->currentItem() ? contactList->currentItem()->text() : "";
    contactList->clear();

    for (const QString &addr : contacts) {
        if (!addr.isEmpty())
            contactList->addItem(addr);
    }

    // Восстанавливаем выделение
    if (!current.isEmpty()) {
        QList<QListWidgetItem*> items = contactList->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty())
            contactList->setCurrentItem(items.first());
    }
}

void ChatWidget::onSmtpError(const QString &error)
{
    chatView->append("Ошибка отправки: " + error);
}

void ChatWidget::onPop3Error(const QString &error)
{
    chatView->append("Ошибка получения: " + error);
}

void ChatWidget::onAddContactClicked()
{
    bool ok;
    QString email = QInputDialog::getText(this, "Новый контакт",
                                          "Введите Email пользователя:", QLineEdit::Normal,
                                          "", &ok);
    if (ok && !email.isEmpty()) {
        email = email.trimmed().toLower();

        if (!email.contains('@')) {
            QMessageBox::warning(this, "Ошибка", "Введите корректный Email адрес.");
            return;
        }

        // Сохраняем в БД
        store->addContact(email);

        // Сразу обновляем список, чтобы увидеть новый контакт
        updateContactList();

        QMessageBox::information(this, "Успех", "Контакт добавлен!");
    }
}
