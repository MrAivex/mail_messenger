#include "MainWindow.h"
#include "LoginWidget.h"
#include "ChatWidget.h"
#include "MessageStore.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    chatPage = nullptr;
    setWindowTitle("Почтовый мессенджер");
    resize(900, 600);

    stack = new QStackedWidget(this);
    setCentralWidget(stack);

    loginPage = new LoginWidget;
    stack->addWidget(loginPage);

    MessageStore *store = new MessageStore(this);

    connect(loginPage, &LoginWidget::loginSucceeded, this,
            [this, store](const QString &email, const QString &password) {
                store->saveCredentials(email, password);
                store->loadMessagesFromDb(email);

                if (chatPage) delete chatPage;
                chatPage = new ChatWidget(email, password, store);

                connect(chatPage, &ChatWidget::logoutRequested, this, [this]() {
                    stack->setCurrentWidget(loginPage); // Переключаем на окно входа
                });

                stack->addWidget(chatPage);
                stack->setCurrentWidget(chatPage);
            });

    QPair<QString, QString> creds = store->loadCredentials();
    if (!creds.first.isEmpty() && !creds.second.isEmpty()) {
        qDebug() << "Авторизация найдена в БД для:" << creds.first;
        emit loginPage->loginSucceeded(creds.first, creds.second);
    } else {
        stack->setCurrentWidget(loginPage);
    }
}
