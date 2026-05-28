#include "LoginWidget.h"
#include <QVBoxLayout>

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *title = new QLabel("Вход в почтовый мессенджер");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 18px; font-weight: bold;");

    emailEdit = new QLineEdit;
    emailEdit->setPlaceholderText("Email");
    passwordEdit = new QLineEdit;
    passwordEdit->setPlaceholderText("Пароль");
    passwordEdit->setEchoMode(QLineEdit::Password);

    loginButton = new QPushButton("Войти");
    statusLabel = new QLabel;

    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(emailEdit);
    layout->addWidget(passwordEdit);
    layout->addWidget(loginButton);
    layout->addWidget(statusLabel);
    layout->addStretch();

    connect(loginButton, &QPushButton::clicked, this, &LoginWidget::onLoginClicked);
}

void LoginWidget::onLoginClicked()
{
    QString email = emailEdit->text().trimmed();
    QString pass = passwordEdit->text();

    if (email.isEmpty() || pass.isEmpty()) {
        statusLabel->setText("Заполните все поля");
        return;
    }
    if (!email.contains('@') || email.contains('\r') || email.contains('\n') ||
        pass.contains('\r') || pass.contains('\n')) {
        statusLabel->setText("Некорректные символы в email или пароле");
        return;
    }

    emit loginSucceeded(email, pass);
}
