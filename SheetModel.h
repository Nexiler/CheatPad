#ifndef SHEETMODEL_H
#define SHEETMODEL_H

#include <QAbstractListModel>
#include <QObject>
#include <QList>
#include <QString>
#include <QVector>
#include <QMap>
#include <QSqlDatabase>
#include <QDateTime>

// Forward declaration
class CsvHandler;

// Represents a Cheatsheet (parent)
struct Cheatsheet {
    int id;
    QString name;
    QString description;
    QString icon;
    QDateTime createdAt;
    QDateTime updatedAt;
    int itemCount;  // Computed field
};

// Represents an item within a Cheatsheet (child)
struct CheatsheetItem {
    int id;
    int cheatsheetId;
    QString category;
    QString command;
    QString description;
    int sortOrder;
    QDateTime createdAt;
    QDateTime updatedAt;
};

class SheetModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorOccurred)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorOccurred)
    Q_PROPERTY(int totalEntries READ totalEntries NOTIFY dataChanged)
    Q_PROPERTY(QString currentSheetName READ currentSheet NOTIFY dataChanged)
    Q_PROPERTY(int currentSheetId READ currentSheetId NOTIFY dataChanged)

public:
    enum SheetRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        IconRole,
        CategoryRole,
        CommandRole,
        EntryCountRole,
        SortOrderRole,
        CreatedAtRole,
        UpdatedAtRole
    };
    Q_ENUM(SheetRoles)

    enum DisplayMode {
        ModeLibrary = 0,
        ModeDetail = 1
    };
    Q_ENUM(DisplayMode)

    explicit SheetModel(QObject *parent = nullptr);
    ~SheetModel();

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Properties
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasError() const { return !m_errorMessage.isEmpty(); }
    int totalEntries() const { return m_items.size(); }
    int currentSheetId() const { return m_currentSheetId; }

    // --- NAVIGATION ---
    Q_INVOKABLE void setMode(int mode, const QString &sheetName = "");
    Q_INVOKABLE QString currentSheet() const { return m_currentSheetName; }
    Q_INVOKABLE int currentMode() const { return static_cast<int>(m_currentMode); }

    // --- SHEET OPERATIONS ---
    Q_INVOKABLE bool createSheet(const QString &name, const QString &description = "", const QString &icon = "📋");
    Q_INVOKABLE bool updateSheet(int sheetId, const QString &name, const QString &description = "", const QString &icon = "");
    Q_INVOKABLE bool deleteSheet(int sheetId);
    Q_INVOKABLE bool renameSheet(const QString &oldName, const QString &newName);

    // --- ITEM OPERATIONS ---
    Q_INVOKABLE bool addEntry(const QString &category, const QString &command, const QString &description);
    Q_INVOKABLE bool updateEntry(int viewIndex, const QString &category, const QString &command, const QString &description);
    Q_INVOKABLE bool deleteEntry(int viewIndex);
    Q_INVOKABLE bool remove(int viewIndex);  // Universal remove (sheet or item based on mode)

    // --- FILTERING ---
    Q_INVOKABLE void applyFilter(const QString &searchText);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void clearStatus();

    // --- DATA ACCESS ---
    Q_INVOKABLE int getEntryId(int viewIndex) const;
    Q_INVOKABLE QString getEntryCategory(int viewIndex) const;
    Q_INVOKABLE QString getEntryCommand(int viewIndex) const;
    Q_INVOKABLE QString getEntryDescription(int viewIndex) const;
    Q_INVOKABLE QStringList getAllCategories() const;
    Q_INVOKABLE QStringList getAllSheetNames() const;

    // --- EXPORT/IMPORT ---
    Q_INVOKABLE QString exportToJson() const;
    Q_INVOKABLE bool importFromJson(const QString &jsonData);
    Q_INVOKABLE QString exportSheetToJson(const QString &sheetName) const;
    
    // --- CSV IMPORT/EXPORT ---
    Q_INVOKABLE bool importFromCsvFile(const QString &filePath, const QString &targetSheetName = "");
    Q_INVOKABLE bool importFromCsvString(const QString &csvContent, const QString &targetSheetName = "");
    Q_INVOKABLE QString exportToCsv(const QString &sheetName = "") const;
    Q_INVOKABLE QString exportAllToCsv() const;
    Q_INVOKABLE QStringList previewCsvFile(const QString &filePath, int maxRows = 5) const;
    Q_INVOKABLE QStringList getCsvHeaders(const QString &filePath) const;

signals:
    void errorOccurred(const QString &error);
    void statusChanged(const QString &status);
    void dataChanged();

private:
    bool initDatabase();
    bool migrateDatabase();
    void loadSheets();
    void loadItems();
    void internalFilter();
    void refresh();
    void setError(const QString &error);
    void setStatus(const QString &status);
    int getSheetIdByName(const QString &name) const;

    QSqlDatabase m_database;
    CsvHandler *m_csvHandler;
    
    // Data storage
    QList<Cheatsheet> m_sheets;
    QList<CheatsheetItem> m_items;
    QVector<int> m_filteredIndices;
    
    // State
    DisplayMode m_currentMode;
    QString m_currentSheetName;
    int m_currentSheetId;
    QString m_currentSearchText;
    QString m_errorMessage;
    QString m_statusMessage;
    bool m_dbInitialized;

    static const int DB_VERSION = 2;
};

#endif // SHEETMODEL_H
