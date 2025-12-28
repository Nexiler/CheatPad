#include "CsvHandler.h"
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>

CsvHandler::CsvHandler(QObject *parent)
    : QObject(parent)
{
}

void CsvHandler::setError(const QString &error)
{
    m_lastError = error;
    emit errorOccurred(error);
}

void CsvHandler::clearError()
{
    m_lastError.clear();
}

// ============================================================================
// PARSING
// ============================================================================

CsvParseResult CsvHandler::parseFile(const QString &filePath, const CsvParseOptions &options)
{
    CsvParseResult result;
    clearError();
    
    // Validate file exists
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        result.addError(tr("File not found: %1").arg(filePath));
        setError(result.errorMessage);
        return result;
    }
    
    if (!fileInfo.isReadable()) {
        result.addError(tr("File is not readable: %1").arg(filePath));
        setError(result.errorMessage);
        return result;
    }
    
    // Check file size (warn for very large files)
    qint64 fileSize = fileInfo.size();
    if (fileSize > 50 * 1024 * 1024) {  // 50 MB
        qWarning() << "⚠️ Large CSV file:" << fileSize / (1024 * 1024) << "MB";
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.addError(tr("Failed to open file: %1").arg(file.errorString()));
        setError(result.errorMessage);
        return result;
    }
    
    // Read file content with proper encoding
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    QString content = stream.readAll();
    file.close();
    
    return parseString(content, options);
}

CsvParseResult CsvHandler::parseString(const QString &content, const CsvParseOptions &options)
{
    CsvParseResult result;
    clearError();
    m_isProcessing = true;
    emit processingChanged();
    
    if (content.isEmpty()) {
        result.addError(tr("CSV content is empty"));
        m_isProcessing = false;
        emit processingChanged();
        return result;
    }
    
    // Normalize line endings
    QString normalizedContent = content;
    normalizedContent.replace("\r\n", "\n");
    normalizedContent.replace("\r", "\n");
    
    // Split into lines (handling quoted multiline fields)
    QStringList rawLines = normalizedContent.split('\n');
    
    int lineNumber = 0;
    bool inQuotedField = false;
    QString pendingField;
    QStringList currentRow;
    int expectedColumns = -1;
    
    for (const QString &rawLine : rawLines) {
        lineNumber++;
        
        // Skip empty lines if configured (but not if we're in a quoted field)
        if (options.skipEmptyRows && rawLine.trimmed().isEmpty() && !inQuotedField) {
            result.skippedRows++;
            continue;
        }
        
        // Parse the line
        QStringList fields = parseLine(rawLine, options, inQuotedField, pendingField);
        
        if (inQuotedField) {
            // We're in the middle of a multi-line quoted field
            // The parseLine function handles accumulating the pending field
            continue;
        }
        
        // Combine with any pending fields from previous line
        if (!currentRow.isEmpty()) {
            // Append the last field from current row with the accumulated content
            if (!fields.isEmpty()) {
                currentRow.append(fields);
            }
        } else {
            currentRow = fields;
        }
        
        // Process header row
        if (options.hasHeader && result.headers.isEmpty()) {
            for (const QString &header : currentRow) {
                QString cleanHeader = header.toLower().trimmed();
                // Remove BOM if present at start
                if (cleanHeader.startsWith(QChar(0xFEFF))) {
                    cleanHeader = cleanHeader.mid(1);
                }
                result.headers.append(cleanHeader);
            }
            expectedColumns = result.headers.size();
            currentRow.clear();
            continue;
        }
        
        // Set expected columns from first data row if no header
        if (expectedColumns < 0) {
            expectedColumns = currentRow.size();
        }
        
        // Validate column count
        if (currentRow.size() != expectedColumns) {
            // Try to handle gracefully - pad or truncate
            while (currentRow.size() < expectedColumns) {
                currentRow.append("");
            }
            if (currentRow.size() > expectedColumns) {
                qWarning() << "⚠️ Line" << lineNumber << "has extra columns, truncating";
                currentRow = currentRow.mid(0, expectedColumns);
            }
        }
        
        // Create row with column mapping
        CsvRow row;
        row.lineNumber = lineNumber;
        
        if (options.hasHeader) {
            for (int i = 0; i < result.headers.size() && i < currentRow.size(); ++i) {
                QString value = currentRow[i];
                if (options.trimFields) {
                    value = value.trimmed();
                }
                row.columns[result.headers[i]] = value;
            }
        } else {
            // Use column indices as keys
            for (int i = 0; i < currentRow.size(); ++i) {
                QString value = currentRow[i];
                if (options.trimFields) {
                    value = value.trimmed();
                }
                row.columns[QString::number(i)] = value;
            }
        }
        
        result.rows.append(row);
        result.validRows++;
        currentRow.clear();
        
        // Check row limit
        if (options.maxRows > 0 && result.validRows >= options.maxRows) {
            break;
        }
        
        // Progress update
        if (lineNumber % 1000 == 0) {
            emit progressUpdated(lineNumber, rawLines.size());
        }
    }
    
    // Check for unclosed quoted field
    if (inQuotedField) {
        result.addError(tr("Unclosed quoted field at end of file"), lineNumber);
        setError(result.errorMessage);
        m_isProcessing = false;
        emit processingChanged();
        return result;
    }
    
    result.totalRows = result.validRows + result.skippedRows;
    result.success = true;
    
    m_isProcessing = false;
    emit processingChanged();
    emit parseCompleted(true);
    
    qDebug() << "✅ CSV parsed:" << result.validRows << "rows," 
             << result.headers.size() << "columns";
    
    return result;
}

QStringList CsvHandler::parseLine(const QString &line, const CsvParseOptions &options,
                                   bool &inQuotedField, QString &pendingField)
{
    QStringList fields;
    QString currentField;
    bool isQuoted = false;
    int i = 0;
    
    // If we're continuing a quoted field from previous line
    if (inQuotedField) {
        currentField = pendingField + "\n";  // Add the newline that was the line break
    }
    
    while (i < line.length()) {
        QChar ch = line[i];
        QChar nextCh = (i + 1 < line.length()) ? line[i + 1] : QChar();
        
        if (inQuotedField || isQuoted) {
            // Inside a quoted field
            if (ch == options.quoteChar) {
                if (nextCh == options.quoteChar) {
                    // Escaped quote (doubled)
                    currentField += options.quoteChar;
                    i += 2;
                    continue;
                } else {
                    // End of quoted field
                    isQuoted = false;
                    inQuotedField = false;
                    i++;
                    continue;
                }
            } else if (ch == options.escapeChar && nextCh == options.quoteChar) {
                // Backslash-escaped quote
                currentField += options.quoteChar;
                i += 2;
                continue;
            } else {
                currentField += ch;
                i++;
            }
        } else {
            // Outside quoted field
            if (ch == options.quoteChar && currentField.isEmpty()) {
                // Start of quoted field
                isQuoted = true;
                i++;
                continue;
            } else if (ch == options.delimiter) {
                // End of field
                fields.append(currentField);
                currentField.clear();
                i++;
                continue;
            } else {
                currentField += ch;
                i++;
            }
        }
    }
    
    // Handle end of line
    if (isQuoted) {
        // Field continues on next line
        inQuotedField = true;
        pendingField = currentField;
    } else {
        fields.append(currentField);
        inQuotedField = false;
        pendingField.clear();
    }
    
    return fields;
}

QString CsvHandler::unescapeField(const QString &field, const CsvParseOptions &options)
{
    QString result = field;
    
    // Remove surrounding quotes if present
    if (result.startsWith(options.quoteChar) && result.endsWith(options.quoteChar)) {
        result = result.mid(1, result.length() - 2);
    }
    
    // Unescape doubled quotes
    QString doubledQuote = QString(options.quoteChar) + QString(options.quoteChar);
    result.replace(doubledQuote, QString(options.quoteChar));
    
    // Unescape backslash-escaped quotes
    QString escapedQuote = QString(options.escapeChar) + QString(options.quoteChar);
    result.replace(escapedQuote, QString(options.quoteChar));
    
    return result;
}

QString CsvHandler::escapeField(const QString &field, QChar delimiter, QChar quoteChar)
{
    // Check if quoting is needed
    bool needsQuoting = field.contains(delimiter) || 
                        field.contains(quoteChar) ||
                        field.contains('\n') ||
                        field.contains('\r');
    
    if (!needsQuoting) {
        return field;
    }
    
    // Escape quotes by doubling them
    QString escaped = field;
    escaped.replace(quoteChar, QString(quoteChar) + QString(quoteChar));
    
    // Wrap in quotes
    return quoteChar + escaped + quoteChar;
}

bool CsvHandler::isFieldQuoted(const QString &field, QChar quoteChar)
{
    return field.startsWith(quoteChar) && field.endsWith(quoteChar);
}

// ============================================================================
// VALIDATION
// ============================================================================

QStringList CsvHandler::validateForImport(const CsvParseResult &result,
                                           const ColumnMapping &mapping)
{
    QStringList errors;
    
    if (!result.success) {
        errors.append(tr("CSV parsing failed: %1").arg(result.errorMessage));
        return errors;
    }
    
    if (result.rows.isEmpty()) {
        errors.append(tr("CSV file contains no data rows"));
        return errors;
    }
    
    // Check for required columns
    QStringList headerLower;
    for (const QString &h : result.headers) {
        headerLower.append(h.toLower());
    }
    
    // At minimum, we need command column
    QStringList commandVariants = {"command", "cmd", "shortcut", "key", "keys", "hotkey"};
    bool hasCommand = false;
    for (const QString &variant : commandVariants) {
        if (headerLower.contains(variant)) {
            hasCommand = true;
            break;
        }
    }
    
    if (!hasCommand) {
        errors.append(tr("Missing required column: 'command' (or 'cmd', 'shortcut', 'key')"));
    }
    
    // Validate row data
    int emptyCommands = 0;
    for (const CsvRow &row : result.rows) {
        QString command = row.value(mapping.commandColumn);
        if (command.isEmpty()) {
            // Try alternate column names
            for (const QString &variant : commandVariants) {
                command = row.value(variant);
                if (!command.isEmpty()) break;
            }
        }
        
        if (command.trimmed().isEmpty()) {
            emptyCommands++;
        }
    }
    
    if (emptyCommands > 0) {
        errors.append(tr("%1 rows have empty command fields").arg(emptyCommands));
    }
    
    return errors;
}

ColumnMapping CsvHandler::autoDetectMapping(const QStringList &headers)
{
    ColumnMapping mapping;
    QStringList headersLower;
    for (const QString &h : headers) {
        headersLower.append(h.toLower().trimmed());
    }
    
    // Category detection
    QStringList categoryVariants = {"category", "cat", "group", "section", "type", "tag"};
    for (const QString &variant : categoryVariants) {
        if (headersLower.contains(variant)) {
            mapping.categoryColumn = variant;
            break;
        }
    }
    
    // Command detection
    QStringList commandVariants = {"command", "cmd", "shortcut", "key", "keys", 
                                    "hotkey", "keybinding", "action"};
    for (const QString &variant : commandVariants) {
        if (headersLower.contains(variant)) {
            mapping.commandColumn = variant;
            break;
        }
    }
    
    // Description detection
    QStringList descVariants = {"description", "desc", "info", "details", "text",
                                 "explanation", "note", "notes"};
    for (const QString &variant : descVariants) {
        if (headersLower.contains(variant)) {
            mapping.descriptionColumn = variant;
            break;
        }
    }
    
    return mapping;
}

// ============================================================================
// UTILITIES
// ============================================================================

QChar CsvHandler::detectDelimiter(const QString &content)
{
    // Count occurrences of common delimiters in first few lines
    QStringList lines = content.split('\n').mid(0, 5);
    QString sample = lines.join("");
    
    QMap<QChar, int> counts;
    counts[','] = sample.count(',');
    counts[';'] = sample.count(';');
    counts['\t'] = sample.count('\t');
    counts['|'] = sample.count('|');
    
    // Find the most common delimiter
    QChar bestDelimiter = ',';
    int maxCount = 0;
    
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            bestDelimiter = it.key();
        }
    }
    
    // Validate: delimiter should appear consistently across lines
    if (maxCount > 0) {
        return bestDelimiter;
    }
    
    return ',';  // Default to comma
}

bool CsvHandler::isValidCsvFile(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    
    // Check extension
    QString ext = fileInfo.suffix().toLower();
    if (ext != "csv" && ext != "tsv" && ext != "txt") {
        return false;
    }
    
    // Check file is readable
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        return false;
    }
    
    // Quick content check
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    QString firstLine = stream.readLine();
    file.close();
    
    // Check if first line looks like CSV (has delimiters)
    return firstLine.contains(',') || firstLine.contains(';') || 
           firstLine.contains('\t') || firstLine.contains('|');
}

CsvParseResult CsvHandler::previewFile(const QString &filePath, int maxRows)
{
    CsvParseOptions options;
    options.maxRows = maxRows;
    return parseFile(filePath, options);
}

// ============================================================================
// EXPORT
// ============================================================================

QString CsvHandler::exportToCsv(const QStringList &headers,
                                 const QVector<QStringList> &rows,
                                 QChar delimiter)
{
    QStringList lines;
    
    // Header row
    QStringList escapedHeaders;
    for (const QString &header : headers) {
        escapedHeaders.append(escapeField(header, delimiter, '"'));
    }
    lines.append(escapedHeaders.join(delimiter));
    
    // Data rows
    for (const QStringList &row : rows) {
        QStringList escapedRow;
        for (const QString &field : row) {
            escapedRow.append(escapeField(field, delimiter, '"'));
        }
        lines.append(escapedRow.join(delimiter));
    }
    
    return lines.join("\n");
}

bool CsvHandler::writeToFile(const QString &filePath, const QString &content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(tr("Failed to open file for writing: %1").arg(file.errorString()));
        return false;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    file.close();
    
    return true;
}
