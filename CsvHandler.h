#ifndef CSVHANDLER_H
#define CSVHANDLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QFile>
#include <QTextStream>

/**
 * @brief Represents a single parsed CSV row with named columns
 */
struct CsvRow {
    QMap<QString, QString> columns;
    int lineNumber;
    
    QString value(const QString &columnName) const {
        return columns.value(columnName.toLower().trimmed());
    }
    
    bool hasColumn(const QString &columnName) const {
        return columns.contains(columnName.toLower().trimmed());
    }
};

/**
 * @brief Represents the result of a CSV parsing operation
 */
struct CsvParseResult {
    bool success = false;
    QString errorMessage;
    int errorLine = -1;
    QStringList headers;
    QVector<CsvRow> rows;
    int totalRows = 0;
    int validRows = 0;
    int skippedRows = 0;
    
    void addError(const QString &msg, int line = -1) {
        success = false;
        errorMessage = msg;
        errorLine = line;
    }
};

/**
 * @brief Represents the result of an import operation
 */
struct ImportResult {
    bool success = false;
    QString errorMessage;
    int importedSheets = 0;
    int importedItems = 0;
    int skippedItems = 0;
    QStringList warnings;
    
    void addWarning(const QString &warning) {
        warnings.append(warning);
    }
};

/**
 * @brief Configuration options for CSV parsing
 */
struct CsvParseOptions {
    QChar delimiter = ',';
    QChar quoteChar = '"';
    QChar escapeChar = '\\';
    bool hasHeader = true;
    bool trimFields = true;
    bool skipEmptyRows = true;
    QString encoding = "UTF-8";
    int maxRows = -1;  // -1 = unlimited
};

/**
 * @brief Column mapping configuration for import
 * Maps CSV column names to internal field names
 */
struct ColumnMapping {
    QString categoryColumn;
    QString commandColumn;
    QString descriptionColumn;
    
    ColumnMapping() 
        : categoryColumn("category")
        , commandColumn("command")
        , descriptionColumn("description")
    {}
};

/**
 * @brief Professional CSV handler for parsing and importing CSV files
 * 
 * This class provides robust CSV parsing that handles:
 * - Quoted fields with embedded delimiters
 * - Escaped quotes within quoted fields
 * - Multi-line fields (within quotes)
 * - Various delimiters (comma, semicolon, tab)
 * - UTF-8 and other encodings
 * - Header row detection
 * - Error recovery and reporting
 */
class CsvHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY processingChanged)

public:
    explicit CsvHandler(QObject *parent = nullptr);
    ~CsvHandler() = default;

    // --- PARSING ---
    
    /**
     * @brief Parse a CSV file and return structured data
     * @param filePath Path to the CSV file
     * @param options Parsing options
     * @return CsvParseResult containing parsed data or error information
     */
    Q_INVOKABLE CsvParseResult parseFile(const QString &filePath, 
                                          const CsvParseOptions &options = CsvParseOptions());
    
    /**
     * @brief Parse CSV content from a string
     * @param content CSV content as string
     * @param options Parsing options
     * @return CsvParseResult containing parsed data or error information
     */
    Q_INVOKABLE CsvParseResult parseString(const QString &content,
                                            const CsvParseOptions &options = CsvParseOptions());

    // --- VALIDATION ---
    
    /**
     * @brief Validate CSV structure for cheatsheet import
     * @param result Previously parsed CSV result
     * @param mapping Column mapping configuration
     * @return List of validation errors/warnings
     */
    Q_INVOKABLE QStringList validateForImport(const CsvParseResult &result,
                                               const ColumnMapping &mapping = ColumnMapping());
    
    /**
     * @brief Auto-detect column mapping from CSV headers
     * @param headers List of header names from CSV
     * @return Best-guess column mapping
     */
    Q_INVOKABLE ColumnMapping autoDetectMapping(const QStringList &headers);

    // --- UTILITIES ---
    
    /**
     * @brief Detect the delimiter used in a CSV file
     * @param content First few lines of the CSV content
     * @return Detected delimiter character
     */
    Q_INVOKABLE QChar detectDelimiter(const QString &content);
    
    /**
     * @brief Check if a file appears to be a valid CSV
     * @param filePath Path to the file
     * @return true if file appears to be valid CSV
     */
    Q_INVOKABLE bool isValidCsvFile(const QString &filePath);
    
    /**
     * @brief Get a preview of the CSV file (first N rows)
     * @param filePath Path to the CSV file
     * @param maxRows Maximum rows to preview
     * @return CsvParseResult with preview data
     */
    Q_INVOKABLE CsvParseResult previewFile(const QString &filePath, int maxRows = 5);

    // --- EXPORT ---
    
    /**
     * @brief Export data to CSV format
     * @param headers Column headers
     * @param rows Data rows
     * @param delimiter Delimiter to use
     * @return CSV formatted string
     */
    Q_INVOKABLE QString exportToCsv(const QStringList &headers,
                                     const QVector<QStringList> &rows,
                                     QChar delimiter = ',');
    
    /**
     * @brief Write CSV content to a file
     * @param filePath Path to output file
     * @param content CSV content
     * @return true on success
     */
    Q_INVOKABLE bool writeToFile(const QString &filePath, const QString &content);

    // --- PROPERTIES ---
    
    QString lastError() const { return m_lastError; }
    bool isProcessing() const { return m_isProcessing; }

signals:
    void errorOccurred(const QString &error);
    void processingChanged();
    void progressUpdated(int current, int total);
    void parseCompleted(bool success);

private:
    // Internal parsing methods
    QStringList parseLine(const QString &line, const CsvParseOptions &options, 
                          bool &inQuotedField, QString &pendingField);
    QString unescapeField(const QString &field, const CsvParseOptions &options);
    QString escapeField(const QString &field, QChar delimiter, QChar quoteChar);
    bool isFieldQuoted(const QString &field, QChar quoteChar);
    
    // State
    QString m_lastError;
    bool m_isProcessing = false;
    
    void setError(const QString &error);
    void clearError();
};

// Register types for QML if needed
Q_DECLARE_METATYPE(CsvParseResult)
Q_DECLARE_METATYPE(CsvRow)
Q_DECLARE_METATYPE(ImportResult)
Q_DECLARE_METATYPE(CsvParseOptions)
Q_DECLARE_METATYPE(ColumnMapping)

#endif // CSVHANDLER_H
