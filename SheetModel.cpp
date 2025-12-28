#include "SheetModel.h"
#include "CsvHandler.h"
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <QUrl>

SheetModel::SheetModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_currentMode(ModeLibrary)
    , m_currentSheetId(-1)
    , m_dbInitialized(false)
    , m_csvHandler(new CsvHandler(this))
{
    m_dbInitialized = initDatabase();
    if (m_dbInitialized) {
        refresh();
    }
}

SheetModel::~SheetModel()
{
    QString connName = m_database.connectionName();
    m_database = QSqlDatabase();  // Release the connection reference
    if (!connName.isEmpty()) {
        QSqlDatabase::removeDatabase(connName);
    }
}

bool SheetModel::initDatabase()
{
    const QString connName = "CheatPadConnection";
    
    // Remove existing connection if any
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    
    m_database = QSqlDatabase::addDatabase("QSQLITE", connName);
    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dbPath);
    QString fullPath = dbPath + "/cheatpad.db";
    qDebug() << "📂 Database path:" << fullPath;
    m_database.setDatabaseName(fullPath);

    if (!m_database.open()) {
        setError("Failed to open database: " + m_database.lastError().text());
        qCritical() << "❌ DB ERROR:" << m_database.lastError().text();
        return false;
    }

    // Enable foreign keys
    QSqlQuery pragma(m_database);
    pragma.exec("PRAGMA foreign_keys = ON");

    // Create tables with proper schema
    QSqlQuery query(m_database);
    
    // Table 1: Cheatsheets (parent table)
    const QString createSheetsTable = R"(
        CREATE TABLE IF NOT EXISTS cheatsheets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            description TEXT DEFAULT '',
            icon TEXT DEFAULT '📋',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    if (!query.exec(createSheetsTable)) {
        setError("Failed to create cheatsheets table: " + query.lastError().text());
        qCritical() << "❌ Table creation failed:" << query.lastError().text();
        return false;
    }

    // Table 2: Cheatsheet Items (child table with foreign key)
    const QString createItemsTable = R"(
        CREATE TABLE IF NOT EXISTS cheatsheet_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            cheatsheet_id INTEGER NOT NULL,
            category TEXT DEFAULT 'General',
            command TEXT NOT NULL,
            description TEXT DEFAULT '',
            sort_order INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (cheatsheet_id) REFERENCES cheatsheets(id) ON DELETE CASCADE
        )
    )";
    
    if (!query.exec(createItemsTable)) {
        setError("Failed to create cheatsheet_items table: " + query.lastError().text());
        qCritical() << "❌ Table creation failed:" << query.lastError().text();
        return false;
    }

    // Create indexes for performance
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_cheatsheet ON cheatsheet_items(cheatsheet_id)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_cheatsheet_order ON cheatsheet_items(cheatsheet_id, sort_order)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_items_category ON cheatsheet_items(category)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_sheets_name ON cheatsheets(name)");

    // Migrate old data if exists
    migrateDatabase();

    qDebug() << "✅ Database initialized successfully";
    return true;
}

bool SheetModel::migrateDatabase()
{
    // Check if old 'sheets' table exists and migrate data
    QSqlQuery checkQuery(m_database);
    checkQuery.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='sheets'");
    
    if (checkQuery.next()) {
        qDebug() << "🔄 Migrating from old database schema...";
        
        // Get all unique sheet names from old table
        QSqlQuery oldSheets(m_database);
        oldSheets.exec("SELECT DISTINCT sheet FROM sheets");
        
        while (oldSheets.next()) {
            QString sheetName = oldSheets.value(0).toString();
            
            // Check if already migrated
            QSqlQuery checkExists(m_database);
            checkExists.prepare("SELECT id FROM cheatsheets WHERE name = ?");
            checkExists.addBindValue(sheetName);
            checkExists.exec();
            
            if (!checkExists.next()) {
                // Create new cheatsheet
                QSqlQuery insertSheet(m_database);
                insertSheet.prepare("INSERT INTO cheatsheets (name) VALUES (?)");
                insertSheet.addBindValue(sheetName);
                
                if (insertSheet.exec()) {
                    int newSheetId = insertSheet.lastInsertId().toInt();
                    
                    // Migrate items with sequential sort_order
                    QSqlQuery oldItems(m_database);
                    oldItems.prepare("SELECT category, command, description FROM sheets WHERE sheet = ?");
                    oldItems.addBindValue(sheetName);
                    oldItems.exec();
                    
                    int sortOrder = 0;
                    while (oldItems.next()) {
                        QSqlQuery insertItem(m_database);
                        insertItem.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
                        insertItem.addBindValue(newSheetId);
                        insertItem.addBindValue(oldItems.value(0).toString());
                        insertItem.addBindValue(oldItems.value(1).toString());
                        insertItem.addBindValue(oldItems.value(2).toString());
                        insertItem.addBindValue(sortOrder++);
                        insertItem.exec();
                    }
                }
            }
        }
        
        // Optionally drop old table after migration
        // query.exec("DROP TABLE IF EXISTS sheets");
        qDebug() << "✅ Migration complete";
    }
    
    return true;
}

// --- HELPER FUNCTIONS ---

void SheetModel::setError(const QString &error)
{
    m_errorMessage = error;
    emit errorOccurred(error);
    qWarning() << "❌" << error;
}

void SheetModel::setStatus(const QString &status)
{
    m_statusMessage = status;
    emit statusChanged(status);
    qDebug() << "ℹ️" << status;
}

void SheetModel::clearError()
{
    m_errorMessage.clear();
    emit errorOccurred("");
}

void SheetModel::clearStatus()
{
    m_statusMessage.clear();
    emit statusChanged("");
}

int SheetModel::getSheetIdByName(const QString &name) const
{
    for (const Cheatsheet &sheet : m_sheets) {
        if (sheet.name == name) {
            return sheet.id;
        }
    }
    return -1;
}

// --- DATA LOADING ---

void SheetModel::loadSheets()
{
    m_sheets.clear();
    
    if (!m_dbInitialized) return;

    QSqlQuery query(m_database);
    query.prepare(R"(
        SELECT 
            c.id, c.name, c.description, c.icon, c.created_at, c.updated_at,
            COUNT(i.id) as item_count
        FROM cheatsheets c
        LEFT JOIN cheatsheet_items i ON c.id = i.cheatsheet_id
        GROUP BY c.id
        ORDER BY c.name ASC
    )");
    
    if (!query.exec()) {
        setError("Failed to load sheets: " + query.lastError().text());
        return;
    }
    
    while (query.next()) {
        Cheatsheet sheet;
        sheet.id = query.value("id").toInt();
        sheet.name = query.value("name").toString();
        sheet.description = query.value("description").toString();
        sheet.icon = query.value("icon").toString();
        sheet.createdAt = query.value("created_at").toDateTime();
        sheet.updatedAt = query.value("updated_at").toDateTime();
        sheet.itemCount = query.value("item_count").toInt();
        m_sheets.append(sheet);
    }
    
    qDebug() << "📚 Loaded" << m_sheets.size() << "sheets";
}

void SheetModel::loadItems()
{
    m_items.clear();
    
    if (!m_dbInitialized || m_currentSheetId < 0) return;

    QSqlQuery query(m_database);
    query.prepare(R"(
        SELECT id, cheatsheet_id, category, command, description, sort_order, created_at, updated_at
        FROM cheatsheet_items
        WHERE cheatsheet_id = ?
        ORDER BY sort_order ASC
    )");
    query.addBindValue(m_currentSheetId);
    
    if (!query.exec()) {
        setError("Failed to load items: " + query.lastError().text());
        return;
    }
    
    while (query.next()) {
        CheatsheetItem item;
        item.id = query.value("id").toInt();
        item.cheatsheetId = query.value("cheatsheet_id").toInt();
        item.category = query.value("category").toString();
        item.command = query.value("command").toString();
        item.description = query.value("description").toString();
        item.sortOrder = query.value("sort_order").toInt();
        item.createdAt = query.value("created_at").toDateTime();
        item.updatedAt = query.value("updated_at").toDateTime();
        m_items.append(item);
    }
    
    qDebug() << "📝 Loaded" << m_items.size() << "items for sheet ID" << m_currentSheetId;
}

void SheetModel::internalFilter()
{
    m_filteredIndices.clear();
    
    if (m_currentMode == ModeLibrary) {
        for (int i = 0; i < m_sheets.size(); ++i) {
            if (m_currentSearchText.isEmpty() || 
                m_sheets[i].name.contains(m_currentSearchText, Qt::CaseInsensitive) ||
                m_sheets[i].description.contains(m_currentSearchText, Qt::CaseInsensitive)) {
                m_filteredIndices.append(i);
            }
        }
    } else {
        for (int i = 0; i < m_items.size(); ++i) {
            const CheatsheetItem &item = m_items[i];
            if (m_currentSearchText.isEmpty() ||
                item.command.contains(m_currentSearchText, Qt::CaseInsensitive) ||
                item.description.contains(m_currentSearchText, Qt::CaseInsensitive) ||
                item.category.contains(m_currentSearchText, Qt::CaseInsensitive)) {
                m_filteredIndices.append(i);
            }
        }
    }
}

void SheetModel::refresh()
{
    beginResetModel();
    
    if (m_currentMode == ModeLibrary) {
        loadSheets();
    } else {
        loadItems();
    }
    internalFilter();
    
    endResetModel();
    emit dataChanged();
}

void SheetModel::setMode(int mode, const QString &sheetName)
{
    DisplayMode newMode = static_cast<DisplayMode>(mode);
    
    beginResetModel();
    
    m_currentMode = newMode;
    m_currentSheetName = sheetName;
    m_currentSearchText.clear();
    
    if (newMode == ModeLibrary) {
        m_currentSheetId = -1;
        loadSheets();
    } else {
        // First load sheets to get ID
        loadSheets();
        m_currentSheetId = getSheetIdByName(sheetName);
        loadItems();
    }
    
    internalFilter();
    endResetModel();
    emit dataChanged();
}

void SheetModel::applyFilter(const QString &searchText)
{
    beginResetModel();
    m_currentSearchText = searchText;
    internalFilter();
    endResetModel();
}

// --- MODEL INTERFACE ---

int SheetModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_filteredIndices.size();
}

QVariant SheetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_filteredIndices.size()) 
        return QVariant();
    
    int realIndex = m_filteredIndices[index.row()];

    if (m_currentMode == ModeLibrary) {
        if (realIndex >= m_sheets.size()) return QVariant();
        const Cheatsheet &sheet = m_sheets[realIndex];
        
        switch (role) {
        case IdRole: return sheet.id;
        case NameRole: return sheet.name;
        case DescriptionRole: return sheet.description;
        case IconRole: return sheet.icon;
        case EntryCountRole: return sheet.itemCount;
        case CreatedAtRole: return sheet.createdAt;
        case UpdatedAtRole: return sheet.updatedAt;
        default: return QVariant();
        }
    } else {
        if (realIndex >= m_items.size()) return QVariant();
        const CheatsheetItem &item = m_items[realIndex];
        
        switch (role) {
        case IdRole: return item.id;
        case CategoryRole: return item.category;
        case CommandRole: return item.command;
        case DescriptionRole: return item.description;
        case SortOrderRole: return item.sortOrder;
        case CreatedAtRole: return item.createdAt;
        case UpdatedAtRole: return item.updatedAt;
        default: return QVariant();
        }
    }
}

QHash<int, QByteArray> SheetModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "entryId";
    roles[NameRole] = "sheetName";
    roles[DescriptionRole] = "description";
    roles[IconRole] = "icon";
    roles[CategoryRole] = "category";
    roles[CommandRole] = "command";
    roles[EntryCountRole] = "entryCount";
    roles[SortOrderRole] = "sortOrder";
    roles[CreatedAtRole] = "createdAt";
    roles[UpdatedAtRole] = "updatedAt";
    return roles;
}

// --- SHEET OPERATIONS ---

bool SheetModel::createSheet(const QString &name, const QString &description, const QString &icon)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        setError("Sheet name cannot be empty");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO cheatsheets (name, description, icon) VALUES (?, ?, ?)");
    query.addBindValue(trimmedName);
    query.addBindValue(description.trimmed());
    query.addBindValue(icon.isEmpty() ? "📋" : icon);

    if (!query.exec()) {
        if (query.lastError().text().contains("UNIQUE constraint")) {
            setError("Sheet '" + trimmedName + "' already exists");
        } else {
            setError("Create sheet failed: " + query.lastError().text());
        }
        return false;
    }

    int newId = query.lastInsertId().toInt();
    setStatus("Created sheet: " + trimmedName);
    qDebug() << "✅ Created sheet:" << trimmedName << "with ID:" << newId;
    
    // Switch to the new sheet
    setMode(ModeDetail, trimmedName);
    return true;
}

bool SheetModel::updateSheet(int sheetId, const QString &name, const QString &description, const QString &icon)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare("UPDATE cheatsheets SET name = ?, description = ?, icon = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    query.addBindValue(name.trimmed());
    query.addBindValue(description.trimmed());
    query.addBindValue(icon.isEmpty() ? "📋" : icon);
    query.addBindValue(sheetId);

    if (!query.exec()) {
        setError("Update sheet failed: " + query.lastError().text());
        return false;
    }

    setStatus("Updated sheet");
    refresh();
    return true;
}

bool SheetModel::deleteSheet(int sheetId)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare("DELETE FROM cheatsheets WHERE id = ?");
    query.addBindValue(sheetId);

    if (!query.exec()) {
        setError("Delete sheet failed: " + query.lastError().text());
        return false;
    }

    setStatus("Sheet deleted");
    refresh();
    return true;
}

bool SheetModel::renameSheet(const QString &oldName, const QString &newName)
{
    int sheetId = getSheetIdByName(oldName);
    if (sheetId < 0) {
        setError("Sheet not found");
        return false;
    }
    
    return updateSheet(sheetId, newName, "", "");
}

// --- ITEM OPERATIONS ---

bool SheetModel::addEntry(const QString &category, const QString &command, const QString &description)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    if (m_currentSheetId < 0) {
        setError("No sheet selected");
        return false;
    }
    
    QString trimmedCmd = command.trimmed();
    if (trimmedCmd.isEmpty()) {
        setError("Command cannot be empty");
        return false;
    }
    
    QString cat = category.trimmed().isEmpty() ? "General" : category.trimmed();

    // Get the next sort_order for this sheet
    QSqlQuery maxQuery(m_database);
    maxQuery.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM cheatsheet_items WHERE cheatsheet_id = ?");
    maxQuery.addBindValue(m_currentSheetId);
    int nextSortOrder = 0;
    if (maxQuery.exec() && maxQuery.next()) {
        nextSortOrder = maxQuery.value(0).toInt();
    }

    QSqlQuery query(m_database);
    query.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(m_currentSheetId);
    query.addBindValue(cat);
    query.addBindValue(trimmedCmd);
    query.addBindValue(description.trimmed());
    query.addBindValue(nextSortOrder);

    if (!query.exec()) {
        setError("Add entry failed: " + query.lastError().text());
        return false;
    }

    setStatus("Added: " + trimmedCmd);
    qDebug() << "✅ Added entry:" << trimmedCmd;
    
    refresh();
    return true;
}

bool SheetModel::updateEntry(int viewIndex, const QString &category, const QString &command, const QString &description)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) {
        setError("Invalid entry index");
        return false;
    }
    
    QString trimmedCmd = command.trimmed();
    if (trimmedCmd.isEmpty()) {
        setError("Command cannot be empty");
        return false;
    }
    
    int realIndex = m_filteredIndices[viewIndex];
    if (realIndex >= m_items.size()) {
        setError("Entry not found");
        return false;
    }
    
    int id = m_items[realIndex].id;
    QString cat = category.trimmed().isEmpty() ? "General" : category.trimmed();

    QSqlQuery query(m_database);
    query.prepare("UPDATE cheatsheet_items SET category = ?, command = ?, description = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?");
    query.addBindValue(cat);
    query.addBindValue(trimmedCmd);
    query.addBindValue(description.trimmed());
    query.addBindValue(id);

    if (!query.exec()) {
        setError("Update entry failed: " + query.lastError().text());
        return false;
    }

    setStatus("Updated: " + trimmedCmd);
    refresh();
    return true;
}

bool SheetModel::deleteEntry(int viewIndex)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) {
        setError("Invalid index");
        return false;
    }
    
    int realIndex = m_filteredIndices[viewIndex];
    if (realIndex >= m_items.size()) {
        setError("Entry not found");
        return false;
    }
    
    int id = m_items[realIndex].id;

    QSqlQuery query(m_database);
    query.prepare("DELETE FROM cheatsheet_items WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        setError("Delete entry failed: " + query.lastError().text());
        return false;
    }

    setStatus("Entry deleted");
    refresh();
    return true;
}

bool SheetModel::remove(int viewIndex)
{
    if (m_currentMode == ModeLibrary) {
        if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) {
            setError("Invalid index");
            return false;
        }
        int realIndex = m_filteredIndices[viewIndex];
        if (realIndex >= m_sheets.size()) {
            setError("Sheet not found");
            return false;
        }
        return deleteSheet(m_sheets[realIndex].id);
    } else {
        return deleteEntry(viewIndex);
    }
}

// --- DATA ACCESS HELPERS ---

int SheetModel::getEntryId(int viewIndex) const
{
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) return -1;
    int realIndex = m_filteredIndices[viewIndex];
    if (m_currentMode == ModeLibrary) {
        if (realIndex >= m_sheets.size()) return -1;
        return m_sheets[realIndex].id;
    } else {
        if (realIndex >= m_items.size()) return -1;
        return m_items[realIndex].id;
    }
}

QString SheetModel::getEntryCategory(int viewIndex) const
{
    if (m_currentMode == ModeLibrary) return "";
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) return "";
    int realIndex = m_filteredIndices[viewIndex];
    if (realIndex >= m_items.size()) return "";
    return m_items[realIndex].category;
}

QString SheetModel::getEntryCommand(int viewIndex) const
{
    if (m_currentMode == ModeLibrary) return "";
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) return "";
    int realIndex = m_filteredIndices[viewIndex];
    if (realIndex >= m_items.size()) return "";
    return m_items[realIndex].command;
}

QString SheetModel::getEntryDescription(int viewIndex) const
{
    if (m_currentMode == ModeLibrary) return "";
    if (viewIndex < 0 || viewIndex >= m_filteredIndices.size()) return "";
    int realIndex = m_filteredIndices[viewIndex];
    if (realIndex >= m_items.size()) return "";
    return m_items[realIndex].description;
}

QStringList SheetModel::getAllCategories() const
{
    QSet<QString> categories;
    for (const CheatsheetItem &item : m_items) {
        categories.insert(item.category);
    }
    QStringList result = categories.values();
    result.sort();
    return result;
}

QStringList SheetModel::getAllSheetNames() const
{
    QStringList names;
    for (const Cheatsheet &sheet : m_sheets) {
        names.append(sheet.name);
    }
    return names;
}

// --- EXPORT/IMPORT ---

QString SheetModel::exportToJson() const
{
    if (!m_dbInitialized) return "{}";
    
    QJsonObject root;
    QJsonArray sheetsArray;
    
    QSqlQuery sheetQuery(const_cast<QSqlDatabase&>(m_database));
    sheetQuery.exec("SELECT id, name, description, icon FROM cheatsheets ORDER BY name");
    
    while (sheetQuery.next()) {
        QJsonObject sheetObj;
        int sheetId = sheetQuery.value("id").toInt();
        sheetObj["name"] = sheetQuery.value("name").toString();
        sheetObj["description"] = sheetQuery.value("description").toString();
        sheetObj["icon"] = sheetQuery.value("icon").toString();
        
        QJsonArray itemsArray;
        QSqlQuery itemQuery(const_cast<QSqlDatabase&>(m_database));
        itemQuery.prepare("SELECT category, command, description FROM cheatsheet_items WHERE cheatsheet_id = ? ORDER BY category, command");
        itemQuery.addBindValue(sheetId);
        itemQuery.exec();
        
        while (itemQuery.next()) {
            QJsonObject itemObj;
            itemObj["category"] = itemQuery.value("category").toString();
            itemObj["command"] = itemQuery.value("command").toString();
            itemObj["description"] = itemQuery.value("description").toString();
            itemsArray.append(itemObj);
        }
        
        sheetObj["items"] = itemsArray;
        sheetsArray.append(sheetObj);
    }
    
    root["sheets"] = sheetsArray;
    root["version"] = "2.0";
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString SheetModel::exportSheetToJson(const QString &sheetName) const
{
    if (!m_dbInitialized) return "{}";
    
    int sheetId = -1;
    for (const Cheatsheet &s : m_sheets) {
        if (s.name == sheetName) {
            sheetId = s.id;
            break;
        }
    }
    if (sheetId < 0) return "{}";
    
    QJsonObject root;
    root["name"] = sheetName;
    
    QJsonArray itemsArray;
    QSqlQuery query(const_cast<QSqlDatabase&>(m_database));
    query.prepare("SELECT category, command, description FROM cheatsheet_items WHERE cheatsheet_id = ? ORDER BY category, command");
    query.addBindValue(sheetId);
    query.exec();
    
    while (query.next()) {
        QJsonObject itemObj;
        itemObj["category"] = query.value("category").toString();
        itemObj["command"] = query.value("command").toString();
        itemObj["description"] = query.value("description").toString();
        itemsArray.append(itemObj);
    }
    
    root["items"] = itemsArray;
    root["version"] = "2.0";
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

bool SheetModel::importFromJson(const QString &jsonData)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        setError("Invalid JSON: " + parseError.errorString());
        return false;
    }
    
    QJsonObject root = doc.object();
    int importedSheets = 0;
    int importedItems = 0;
    
    // Handle full export with sheets array
    if (root.contains("sheets")) {
        QJsonArray sheetsArray = root["sheets"].toArray();
        
        for (const QJsonValue &sheetVal : sheetsArray) {
            QJsonObject sheetObj = sheetVal.toObject();
            QString sheetName = sheetObj["name"].toString();
            QString sheetDesc = sheetObj["description"].toString();
            QString sheetIcon = sheetObj["icon"].toString();
            
            // Insert sheet
            QSqlQuery sheetQuery(m_database);
            sheetQuery.prepare("INSERT OR IGNORE INTO cheatsheets (name, description, icon) VALUES (?, ?, ?)");
            sheetQuery.addBindValue(sheetName);
            sheetQuery.addBindValue(sheetDesc);
            sheetQuery.addBindValue(sheetIcon.isEmpty() ? "📋" : sheetIcon);
            
            if (sheetQuery.exec()) {
                int sheetId = sheetQuery.lastInsertId().toInt();
                if (sheetId == 0) {
                    // Sheet already existed, get its ID
                    QSqlQuery getId(m_database);
                    getId.prepare("SELECT id FROM cheatsheets WHERE name = ?");
                    getId.addBindValue(sheetName);
                    getId.exec();
                    if (getId.next()) sheetId = getId.value(0).toInt();
                } else {
                    importedSheets++;
                }
                
                // Insert items with sequential sort_order
                QJsonArray itemsArray = sheetObj["items"].toArray();
                
                // Get starting sort_order for this sheet
                QSqlQuery maxQuery(m_database);
                maxQuery.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM cheatsheet_items WHERE cheatsheet_id = ?");
                maxQuery.addBindValue(sheetId);
                int nextSortOrder = 0;
                if (maxQuery.exec() && maxQuery.next()) {
                    nextSortOrder = maxQuery.value(0).toInt();
                }
                
                for (const QJsonValue &itemVal : itemsArray) {
                    QJsonObject itemObj = itemVal.toObject();
                    
                    QSqlQuery itemQuery(m_database);
                    itemQuery.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
                    itemQuery.addBindValue(sheetId);
                    itemQuery.addBindValue(itemObj["category"].toString());
                    itemQuery.addBindValue(itemObj["command"].toString());
                    itemQuery.addBindValue(itemObj["description"].toString());
                    itemQuery.addBindValue(nextSortOrder++);
                    
                    if (itemQuery.exec()) {
                        importedItems++;
                    }
                }
            }
        }
    }
    // Handle single sheet export
    else if (root.contains("name") && root.contains("items")) {
        QString sheetName = root["name"].toString();
        
        QSqlQuery sheetQuery(m_database);
        sheetQuery.prepare("INSERT OR IGNORE INTO cheatsheets (name) VALUES (?)");
        sheetQuery.addBindValue(sheetName);
        sheetQuery.exec();
        
        int sheetId = sheetQuery.lastInsertId().toInt();
        if (sheetId == 0) {
            QSqlQuery getId(m_database);
            getId.prepare("SELECT id FROM cheatsheets WHERE name = ?");
            getId.addBindValue(sheetName);
            getId.exec();
            if (getId.next()) sheetId = getId.value(0).toInt();
        } else {
            importedSheets++;
        }
        
        QJsonArray itemsArray = root["items"].toArray();
        
        // Get starting sort_order for this sheet
        QSqlQuery maxQuery(m_database);
        maxQuery.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM cheatsheet_items WHERE cheatsheet_id = ?");
        maxQuery.addBindValue(sheetId);
        int nextSortOrder = 0;
        if (maxQuery.exec() && maxQuery.next()) {
            nextSortOrder = maxQuery.value(0).toInt();
        }
        
        for (const QJsonValue &itemVal : itemsArray) {
            QJsonObject itemObj = itemVal.toObject();
            
            QSqlQuery itemQuery(m_database);
            itemQuery.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
            itemQuery.addBindValue(sheetId);
            itemQuery.addBindValue(itemObj["category"].toString());
            itemQuery.addBindValue(itemObj["command"].toString());
            itemQuery.addBindValue(itemObj["description"].toString());
            itemQuery.addBindValue(nextSortOrder++);
            
            if (itemQuery.exec()) {
                importedItems++;
            }
        }
    }
    
    setStatus(QString("Imported %1 sheets, %2 items").arg(importedSheets).arg(importedItems));
    refresh();
    return (importedSheets + importedItems) > 0;
}

// ============================================================================
// CSV IMPORT/EXPORT
// ============================================================================

bool SheetModel::importFromCsvFile(const QString &filePath, const QString &targetSheetName)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    // Target sheet is required
    if (targetSheetName.isEmpty()) {
        setError("Target sheet name is required");
        return false;
    }
    
    // Get sheet ID
    int sheetId = getSheetIdByName(targetSheetName);
    if (sheetId < 0) {
        setError(QString("Sheet '%1' not found").arg(targetSheetName));
        return false;
    }
    
    // Clean the file path (handle file:// URLs from QML)
    QString cleanPath = filePath;
    if (cleanPath.startsWith("file://")) {
        cleanPath = QUrl(cleanPath).toLocalFile();
    }
    
    qDebug() << "📥 Importing CSV from:" << cleanPath << "into sheet:" << targetSheetName;
    
    // Parse the CSV file
    CsvParseOptions options;
    options.hasHeader = true;
    options.trimFields = true;
    options.skipEmptyRows = true;
    
    // Auto-detect delimiter
    QFile file(cleanPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("Failed to open file: %1").arg(file.errorString()));
        return false;
    }
    
    QString preview = file.read(4096);
    file.close();
    
    options.delimiter = m_csvHandler->detectDelimiter(preview);
    qDebug() << "🔍 Detected delimiter:" << options.delimiter;
    
    CsvParseResult parseResult = m_csvHandler->parseFile(cleanPath, options);
    
    if (!parseResult.success) {
        setError(QString("CSV parse error: %1").arg(parseResult.errorMessage));
        return false;
    }
    
    if (parseResult.rows.isEmpty()) {
        setError("CSV file contains no data rows");
        return false;
    }
    
    qDebug() << "📊 Parsed" << parseResult.validRows << "rows with headers:" << parseResult.headers;
    
    // Auto-detect column mapping
    ColumnMapping mapping = m_csvHandler->autoDetectMapping(parseResult.headers);
    
    // Track import statistics
    int importedItems = 0;
    int skippedItems = 0;
    
    // Get the starting sort_order for this sheet
    QSqlQuery maxQuery(m_database);
    maxQuery.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM cheatsheet_items WHERE cheatsheet_id = ?");
    maxQuery.addBindValue(sheetId);
    int nextSortOrder = 0;
    if (maxQuery.exec() && maxQuery.next()) {
        nextSortOrder = maxQuery.value(0).toInt();
    }
    
    // Process each row - import into the target sheet
    for (const CsvRow &row : parseResult.rows) {
        // Get command field
        QString command = row.value(mapping.commandColumn);
        if (command.isEmpty()) {
            QStringList cmdVariants = {"command", "cmd", "shortcut", "key", "keys", "hotkey"};
            for (const QString &variant : cmdVariants) {
                command = row.value(variant);
                if (!command.isEmpty()) break;
            }
        }
        
        if (command.trimmed().isEmpty()) {
            skippedItems++;
            continue;
        }
        
        // Get category field
        QString category = row.value(mapping.categoryColumn);
        if (category.isEmpty()) {
            QStringList catVariants = {"category", "cat", "group", "section", "type"};
            for (const QString &variant : catVariants) {
                category = row.value(variant);
                if (!category.isEmpty()) break;
            }
        }
        if (category.isEmpty()) category = "General";
        
        // Get description field
        QString description = row.value(mapping.descriptionColumn);
        if (description.isEmpty()) {
            QStringList descVariants = {"description", "desc", "info", "details", "text"};
            for (const QString &variant : descVariants) {
                description = row.value(variant);
                if (!description.isEmpty()) break;
            }
        }
        
        // Insert item into the target sheet with sequential sort_order
        QSqlQuery insertItem(m_database);
        insertItem.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
        insertItem.addBindValue(sheetId);
        insertItem.addBindValue(category);
        insertItem.addBindValue(command);
        insertItem.addBindValue(description);
        insertItem.addBindValue(nextSortOrder++);
        
        if (insertItem.exec()) {
            importedItems++;
        } else {
            qWarning() << "❌ Failed to insert item:" << insertItem.lastError().text();
            skippedItems++;
        }
    }
    
    setStatus(QString("Imported %1 items (%2 skipped)").arg(importedItems).arg(skippedItems));
    
    qDebug() << "✅ CSV import complete:" << importedItems << "items," << skippedItems << "skipped";
    
    refresh();
    return importedItems > 0;
}

bool SheetModel::importFromCsvString(const QString &csvContent, const QString &targetSheetName)
{
    if (!m_dbInitialized) {
        setError("Database not initialized");
        return false;
    }
    
    if (csvContent.trimmed().isEmpty()) {
        setError("CSV content is empty");
        return false;
    }
    
    // Target sheet is required
    if (targetSheetName.isEmpty()) {
        setError("Target sheet name is required");
        return false;
    }
    
    // Get sheet ID
    int sheetId = getSheetIdByName(targetSheetName);
    if (sheetId < 0) {
        setError(QString("Sheet '%1' not found").arg(targetSheetName));
        return false;
    }
    
    // Parse the CSV content
    CsvParseOptions options;
    options.hasHeader = true;
    options.trimFields = true;
    options.skipEmptyRows = true;
    options.delimiter = m_csvHandler->detectDelimiter(csvContent);
    
    CsvParseResult parseResult = m_csvHandler->parseString(csvContent, options);
    
    if (!parseResult.success) {
        setError(QString("CSV parse error: %1").arg(parseResult.errorMessage));
        return false;
    }
    
    if (parseResult.rows.isEmpty()) {
        setError("CSV content contains no data rows");
        return false;
    }
    
    ColumnMapping mapping = m_csvHandler->autoDetectMapping(parseResult.headers);
    
    int importedItems = 0;
    int skippedItems = 0;
    
    // Get the starting sort_order for this sheet
    QSqlQuery maxQuery(m_database);
    maxQuery.prepare("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM cheatsheet_items WHERE cheatsheet_id = ?");
    maxQuery.addBindValue(sheetId);
    int nextSortOrder = 0;
    if (maxQuery.exec() && maxQuery.next()) {
        nextSortOrder = maxQuery.value(0).toInt();
    }
    
    for (const CsvRow &row : parseResult.rows) {
        QString command = row.value(mapping.commandColumn);
        if (command.isEmpty()) {
            QStringList cmdVariants = {"command", "cmd", "shortcut", "key"};
            for (const QString &v : cmdVariants) {
                command = row.value(v);
                if (!command.isEmpty()) break;
            }
        }
        
        if (command.trimmed().isEmpty()) {
            skippedItems++;
            continue;
        }
        
        QString category = row.value(mapping.categoryColumn);
        if (category.isEmpty()) category = row.value("category");
        if (category.isEmpty()) category = "General";
        
        QString description = row.value(mapping.descriptionColumn);
        if (description.isEmpty()) description = row.value("description");
        
        QSqlQuery insertItem(m_database);
        insertItem.prepare("INSERT INTO cheatsheet_items (cheatsheet_id, category, command, description, sort_order) VALUES (?, ?, ?, ?, ?)");
        insertItem.addBindValue(sheetId);
        insertItem.addBindValue(category);
        insertItem.addBindValue(command);
        insertItem.addBindValue(description);
        insertItem.addBindValue(nextSortOrder++);
        
        if (insertItem.exec()) {
            importedItems++;
        } else {
            skippedItems++;
        }
    }
    
    setStatus(QString("Imported %1 items (%2 skipped)").arg(importedItems).arg(skippedItems));
    refresh();
    return importedItems > 0;
}

QString SheetModel::exportToCsv(const QString &sheetName) const
{
    QStringList headers = {"category", "command", "description"};
    QVector<QStringList> rows;
    
    if (sheetName.isEmpty()) {
        // Export current sheet
        if (m_currentMode != ModeDetail || m_currentSheetId < 0) {
            return QString();
        }
        
        for (const CheatsheetItem &item : m_items) {
            if (item.cheatsheetId == m_currentSheetId) {
                rows.append({item.category, item.command, item.description});
            }
        }
    } else {
        // Export specified sheet
        int sheetId = getSheetIdByName(sheetName);
        if (sheetId < 0) {
            return QString();
        }
        
        QSqlQuery query(m_database);
        query.prepare("SELECT category, command, description FROM cheatsheet_items WHERE cheatsheet_id = ?");
        query.addBindValue(sheetId);
        query.exec();
        
        while (query.next()) {
            rows.append({
                query.value(0).toString(),
                query.value(1).toString(),
                query.value(2).toString()
            });
        }
    }
    
    CsvHandler handler;
    return handler.exportToCsv(headers, rows, ',');
}

QString SheetModel::exportAllToCsv() const
{
    QStringList headers = {"sheet", "category", "command", "description", "icon"};
    QVector<QStringList> rows;
    
    // Get all sheets with their items
    QSqlQuery sheetsQuery(m_database);
    sheetsQuery.exec("SELECT id, name, icon FROM cheatsheets ORDER BY name");
    
    while (sheetsQuery.next()) {
        int sheetId = sheetsQuery.value(0).toInt();
        QString sheetName = sheetsQuery.value(1).toString();
        QString icon = sheetsQuery.value(2).toString();
        
        QSqlQuery itemsQuery(m_database);
        itemsQuery.prepare("SELECT category, command, description FROM cheatsheet_items WHERE cheatsheet_id = ? ORDER BY sort_order");
        itemsQuery.addBindValue(sheetId);
        itemsQuery.exec();
        
        while (itemsQuery.next()) {
            rows.append({
                sheetName,
                itemsQuery.value(0).toString(),
                itemsQuery.value(1).toString(),
                itemsQuery.value(2).toString(),
                icon
            });
        }
    }
    
    CsvHandler handler;
    return handler.exportToCsv(headers, rows, ',');
}

QStringList SheetModel::previewCsvFile(const QString &filePath, int maxRows) const
{
    QString cleanPath = filePath;
    if (cleanPath.startsWith("file://")) {
        cleanPath = QUrl(cleanPath).toLocalFile();
    }
    
    CsvHandler handler;
    CsvParseResult result = handler.previewFile(cleanPath, maxRows);
    
    QStringList preview;
    
    if (!result.success) {
        preview.append("Error: " + result.errorMessage);
        return preview;
    }
    
    // Add header info
    preview.append("Headers: " + result.headers.join(", "));
    preview.append("---");
    
    // Add row previews
    for (const CsvRow &row : result.rows) {
        QStringList values;
        for (const QString &header : result.headers) {
            values.append(row.value(header));
        }
        preview.append(values.join(" | "));
    }
    
    return preview;
}

QStringList SheetModel::getCsvHeaders(const QString &filePath) const
{
    QString cleanPath = filePath;
    if (cleanPath.startsWith("file://")) {
        cleanPath = QUrl(cleanPath).toLocalFile();
    }
    
    CsvHandler handler;
    CsvParseResult result = handler.previewFile(cleanPath, 1);
    
    if (!result.success) {
        return QStringList();
    }
    
    return result.headers;
}
