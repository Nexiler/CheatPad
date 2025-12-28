#include "AppController.h"
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>

AppController::AppController(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_autoSave(false)
    , m_silentMode(false)
    , m_theme("dark")
{
    loadConfig();
}

void AppController::loadConfig()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "CheatPad", "CheatPad");
    
    bool newAuto = settings.value("autosave", false).toBool();
    bool newSilent = settings.value("silentmode", false).toBool();
    QString newTheme = settings.value("theme", "dark").toString();

    bool changed = false;
    if (m_autoSave != newAuto) {
        m_autoSave = newAuto;
        changed = true;
    }
    if (m_silentMode != newSilent) {
        m_silentMode = newSilent;
        changed = true;
    }
    if (m_theme != newTheme) {
        m_theme = newTheme;
        changed = true;
    }
    
    if (changed) {
        emit configChanged();
    }
}

void AppController::saveConfig()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "CheatPad", "CheatPad");
    settings.setValue("autosave", m_autoSave);
    settings.setValue("silentmode", m_silentMode);
    settings.setValue("theme", m_theme);
    settings.sync();
}

void AppController::reloadConfig()
{
    loadConfig();
}

void AppController::setAutoSave(bool value)
{
    if (m_autoSave == value) return;
    m_autoSave = value;
    saveConfig();
    emit configChanged();
}

void AppController::setSilentMode(bool value)
{
    if (m_silentMode == value) return;
    m_silentMode = value;
    saveConfig();
    emit configChanged();
}

void AppController::setTheme(const QString &value)
{
    if (m_theme == value) return;
    m_theme = value;
    saveConfig();
    emit configChanged();
}

void AppController::toggleWindow()
{
    emit requestToggle();
}

void AppController::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
        emit clipboardCopied(text);
        qDebug() << "📋 Copied to clipboard:" << text.left(50) + (text.length() > 50 ? "..." : "");
    }
}

QString AppController::getFromClipboard() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text() : QString();
}
