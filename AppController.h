#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSettings>
#include <QClipboard>
#include <QGuiApplication>

class AppController : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cheatpad.controller")

    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY configChanged)
    Q_PROPERTY(bool silentMode READ silentMode WRITE setSilentMode NOTIFY configChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY configChanged)

public:
    explicit AppController(const QString &dbPath, QObject *parent = nullptr);

    bool autoSave() const { return m_autoSave; }
    void setAutoSave(bool value);

    bool silentMode() const { return m_silentMode; }
    void setSilentMode(bool value);

    QString theme() const { return m_theme; }
    void setTheme(const QString &value);

public slots:
    void toggleWindow();
    void reloadConfig();
    
    // Clipboard operations
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE QString getFromClipboard() const;

signals:
    void requestToggle();
    void configChanged();
    void clipboardCopied(const QString &text);

private:
    void loadConfig();
    void saveConfig();
    
    QSqlDatabase m_database;
    bool m_autoSave;
    bool m_silentMode;
    QString m_theme;
};

#endif // APPCONTROLLER_H
