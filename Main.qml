import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
import CheatPad 1.0

Window {
    id: root
    width: 400
    height: 500
    title: "CheatPad"
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    // ============== PROPERTIES ==============
    property bool isLibraryMode: true
    property bool isEditing: false
    property int editingIndex: -1  // -1 means adding new, >= 0 means editing existing
    property string currentCategory: ""
    property string currentCommand: ""
    property string currentDescription: ""

    // Theme colors
    readonly property color bgColor: "#1e1e2e"
    readonly property color bgColorTransparent: "#F01e1e2e"
    readonly property color accentColor: "#89b4fa"
    readonly property color textColor: "#cdd6f4"
    readonly property color subtextColor: "#a6adc8"
    readonly property color greenColor: "#a6e3a1"
    readonly property color redColor: "#f38ba8"
    readonly property color yellowColor: "#f9e2af"
    readonly property color surfaceColor: "#313244"
    readonly property color overlayColor: "#45475a"

    // No window animations - instant resize
    
    // ============== WINDOW CENTERING ==============
    function centerWindow(targetW, targetH) {
        var screenW = Screen.desktopAvailableWidth
        var screenH = Screen.desktopAvailableHeight
        
        var newX = Math.round((screenW - targetW) / 2)
        var newY = Math.round((screenH - targetH) / 2)
        
        // Set all instantly
        root.x = newX
        root.y = newY
        root.width = targetW
        root.height = targetH
    }

    Component.onCompleted: {
        centerWindow(400, 500)
        // Window starts hidden, toggle with D-Bus: gdbus call --session --dest org.cheatpad.service --object-path / --method org.cheatpad.controller.toggleWindow
    }

    onVisibleChanged: {
        if (visible) {
            resetToLibrary()
        }
    }

    function resetToLibrary() {
        isLibraryMode = true
        isEditing = false
        editingIndex = -1
        sheetModel.setMode(0, "")
        mainStack.currentIndex = 0
        centerWindow(400, 500)
        libSearch.text = ""
        libSearch.forceActiveFocus()
    }

    // ============== D-BUS CONNECTION ==============
    Connections {
        target: appController
        function onRequestToggle() {
            if (root.visible) {
                root.hide()
            } else {
                root.show()
                root.raise()
                root.requestActivate()
                if (mainStack.currentIndex === 0) {
                    libSearch.forceActiveFocus()
                } else {
                    detailSearch.forceActiveFocus()
                }
            }
        }
        function onClipboardCopied(text) {
            showToast("Copied: " + text.substring(0, 30) + (text.length > 30 ? "..." : ""))
        }
    }

    // ============== MODEL ==============
    SheetModel { id: sheetModel }

    // ============== CONFIG PROPERTIES ==============
    property bool silentMode: appController.silentMode
    property bool autoSave: appController.autoSave

    // ============== HELPER FUNCTIONS ==============
    function anyDialogVisible() {
        return nameDialog.visible || renameDialog.visible || deleteDialog.visible || 
               quitDialog.visible || discardDialog.visible || importDialog.visible
    }
    
    function showImportDialog() {
        importPathInput.text = ""
        importDialog.visible = true
        importPathInput.forceActiveFocus()
    }
    
    function doImportCsv(inputPath) {
        if (inputPath.trim() === "") {
            importDialog.visible = false
            return
        }
        
        var filePath = inputPath.trim()
        
        // If just a filename (no path separators), search in common locations
        if (!filePath.includes("/")) {
            // Try current directory, home, Downloads, Documents
            var searchPaths = [
                filePath,
                Qt.resolvedUrl("file://" + filePath).toString(),
                StandardPaths.writableLocation(StandardPaths.HomeLocation) + "/" + filePath,
                StandardPaths.writableLocation(StandardPaths.DownloadLocation) + "/" + filePath,
                StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + filePath
            ]
            
            // For now, just use the input as-is and let the backend handle it
            filePath = inputPath.trim()
        }
        
        // Add file:// prefix if it's an absolute path without it
        if (filePath.startsWith("/") && !filePath.startsWith("file://")) {
            filePath = "file://" + filePath
        }
        
        console.log("Importing CSV from:", filePath)
        
        if (sheetModel.importFromCsvFile(filePath, sheetModel.currentSheet())) {
            importDialog.visible = false
            showToast("✅ " + sheetModel.statusMessage)
            // Restore focus to detail search after import
            detailSearch.forceActiveFocus()
        } else {
            showToast("❌ " + sheetModel.errorMessage, true)
        }
    }

    // ============== KEYBOARD SHORTCUTS ==============
    // Global shortcuts (work everywhere)
    Shortcut { 
        sequence: "Ctrl+Q"
        onActivated: handleQuit()
    }
    Shortcut { sequence: "Esc"; onActivated: handleEscape() }
    
    // Context-aware shortcuts
    Shortcut { 
        sequence: "Ctrl+N"
        enabled: !anyDialogVisible()
        onActivated: {
            if (isLibraryMode) {
                // In library: Create new sheet
                newSheetName.text = ""
                nameDialog.visible = true
                newSheetName.forceActiveFocus()
            } else {
                // In detail: Add new entry
                startNewEntry()
            }
        }
    }
    
    Shortcut { 
        sequence: "Ctrl+D"
        enabled: !anyDialogVisible()
        onActivated: {
            if (!isEditing) {
                handleDelete()
            }
        }
    }
    
    Shortcut { 
        sequence: "Ctrl+S"
        onActivated: {
            if (isEditing) {
                handleSave()
            }
        }
    }
    

    
    Shortcut { 
        sequence: "Ctrl+E"
        enabled: !anyDialogVisible()
        onActivated: {
            if (isLibraryMode) {
                handleRenameSheet()
            } else if (!isEditing) {
                handleEdit()
            }
        }
    }
    
    Shortcut { 
        sequence: "Ctrl+F"
        enabled: !anyDialogVisible()
        onActivated: {
            // Focus search field
            if (isLibraryMode) {
                libSearch.forceActiveFocus()
                libSearch.selectAll()
            } else if (!isEditing) {
                detailSearch.forceActiveFocus()
                detailSearch.selectAll()
            }
        }
    }
    
    Shortcut { 
        sequence: "Ctrl+B"
        enabled: !anyDialogVisible()
        onActivated: {
            // Back to library from detail
            if (!isLibraryMode && !isEditing) {
                morphToLibrary()
            }
        }
    }
    
    // Import CSV (only in detail view)
    Shortcut {
        sequence: "Ctrl+I"
        enabled: !anyDialogVisible() && !isEditing && !isLibraryMode
        onActivated: showImportDialog()
    }
    
    // Enter key
    Shortcut { sequence: "Return"; onActivated: handleEnter() }
    
    // Navigation with arrow keys (only when not editing)
    Shortcut { 
        sequence: "Up"
        enabled: !isEditing && !anyDialogVisible()
        onActivated: navigateUp()
    }
    Shortcut { 
        sequence: "Down"
        enabled: !isEditing && !anyDialogVisible()
        onActivated: navigateDown()
    }
    Shortcut { 
        sequence: "Ctrl+Up"
        enabled: !isEditing
        onActivated: navigateFirst()
    }
    Shortcut { 
        sequence: "Ctrl+Down"
        enabled: !isEditing
        onActivated: navigateLast()
    }
    
    // Ctrl+Tab to cycle through form fields when editing
    Shortcut {
        sequence: "Ctrl+Tab"
        enabled: isEditing
        onActivated: cycleFormFields(1)
    }
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        enabled: isEditing
        onActivated: cycleFormFields(-1)
    }
    
    function cycleFormFields(direction) {
        var fields = [inCat, inCmd, inDesc]
        var currentIdx = -1
        
        for (var i = 0; i < fields.length; i++) {
            if (fields[i].activeFocus) {
                currentIdx = i
                break
            }
        }
        
        if (currentIdx === -1) {
            fields[0].forceActiveFocus()
        } else {
            var nextIdx = (currentIdx + direction + fields.length) % fields.length
            fields[nextIdx].forceActiveFocus()
        }
    }

    // ============== NAVIGATION FUNCTIONS ==============
    function navigateUp() {
        var list = isLibraryMode ? libList : detailList
        if (list.currentIndex > 0) {
            list.currentIndex--
        }
    }

    function navigateDown() {
        var list = isLibraryMode ? libList : detailList
        if (list.currentIndex < list.count - 1) {
            list.currentIndex++
        }
    }

    function navigateFirst() {
        var list = isLibraryMode ? libList : detailList
        list.currentIndex = 0
    }

    function navigateLast() {
        var list = isLibraryMode ? libList : detailList
        list.currentIndex = list.count - 1
    }

    function handleEnter() {
        // Handle import dialog
        if (importDialog.visible) {
            doImportCsv(importPathInput.text)
            return
        }
        
        // Handle new sheet dialog
        if (nameDialog.visible) {
            if (newSheetName.text.trim() !== "") {
                createNewSheet(newSheetName.text.trim())
            }
            return
        }
        
        // Handle rename dialog
        if (renameDialog.visible) {
            if (renameSheetInput.text.trim() !== "") {
                performRenameSheet()
            }
            return
        }
        
        // Handle delete dialog
        if (deleteDialog.visible) {
            confirmDelete()
            return
        }
        
        if (isEditing) {
            handleSave()
            return
        }
        
        if (isLibraryMode) {
            // In library: open selected sheet
            if (libList.count > 0 && libList.currentIndex >= 0) {
                var sheetName = sheetModel.data(sheetModel.index(libList.currentIndex, 0), SheetModel.NameRole)
                morphToDetail(sheetName)
            }
        } else {
            // In detail view: copy command and optionally hide
            if (detailList.count > 0 && detailList.currentIndex >= 0) {
                handleCopy()
            }
        }
    }

    function handleEscape() {
        if (importDialog.visible) {
            importDialog.visible = false
            detailSearch.forceActiveFocus()
        } else if (quitDialog.visible) {
            quitDialog.visible = false
        } else if (discardDialog.visible) {
            discardDialog.visible = false
        } else if (nameDialog.visible) {
            nameDialog.visible = false
            libSearch.forceActiveFocus()
        } else if (deleteDialog.visible) {
            deleteDialog.visible = false
        } else if (renameDialog.visible) {
            renameDialog.visible = false
            libList.forceActiveFocus()
        } else if (isEditing) {
            // Check for unsaved changes
            handleCancelEdit()
        } else if (!isLibraryMode) {
            morphToLibrary()
        } else {
            root.hide()
        }
    }

    function handleNew() {
        if (isLibraryMode) {
            newSheetName.text = ""
            nameDialog.visible = true
            newSheetName.forceActiveFocus()
        } else {
            startNewEntry()
        }
    }

    function handleQuit() {
        // Quit always shows confirmation (even in silent mode for safety)
        quitDialog.visible = true
    }

    function confirmQuit() {
        quitDialog.visible = false
        Qt.quit()
    }

    function handleCancelEdit() {
        // Check if there are unsaved changes
        var hasChanges = inCat.text.trim() !== "" || inCmd.text.trim() !== "" || inDesc.text.trim() !== ""
        
        if (editingIndex >= 0) {
            // When editing existing, compare with original values
            var origCat = sheetModel.getEntryCategory(editingIndex)
            var origCmd = sheetModel.getEntryCommand(editingIndex)
            var origDesc = sheetModel.getEntryDescription(editingIndex)
            hasChanges = (inCat.text.trim() !== origCat || 
                         inCmd.text.trim() !== origCmd || 
                         inDesc.text.trim() !== origDesc)
        }
        
        if (!hasChanges) {
            cancelEdit()
            return
        }
        
        // AutoSave mode: save automatically
        if (autoSave) {
            if (inCmd.text.trim() !== "") {
                handleSave()
            } else {
                cancelEdit()
            }
            return
        }
        
        // Silent mode: discard without confirmation
        if (silentMode) {
            cancelEdit()
            return
        }
        
        // Show discard confirmation
        discardDialog.visible = true
    }

    function confirmDiscard() {
        discardDialog.visible = false
        cancelEdit()
    }

    function handleDelete() {
        if (isEditing) return
        
        var list = isLibraryMode ? libList : detailList
        if (list.count === 0 || list.currentIndex < 0) return
        
        // In silent mode, delete immediately without confirmation
        if (silentMode) {
            var success = sheetModel.remove(list.currentIndex)
            if (success) {
                showToast("Deleted successfully")
                if (!isLibraryMode) detailSearch.forceActiveFocus()
            }
            return
        }
        
        // Show confirmation dialog
        deleteDialog.itemName = isLibraryMode 
            ? sheetModel.data(sheetModel.index(list.currentIndex, 0), SheetModel.NameRole)
            : sheetModel.getEntryCommand(list.currentIndex)
        deleteDialog.itemIndex = list.currentIndex
        deleteDialog.visible = true
    }

    function handleEdit() {
        if (isEditing || isLibraryMode) return
        if (detailList.count === 0 || detailList.currentIndex < 0) return
        
        startEdit(detailList.currentIndex)
    }
    
    function handleRenameSheet() {
        if (!isLibraryMode) return
        if (libList.count === 0 || libList.currentIndex < 0) return
        
        var currentName = sheetModel.data(sheetModel.index(libList.currentIndex, 0), SheetModel.NameRole)
        renameSheetInput.text = currentName
        renameDialog.originalName = currentName
        renameDialog.visible = true
        renameSheetInput.forceActiveFocus()
        renameSheetInput.selectAll()
    }
    
    function performRenameSheet() {
        var newName = renameSheetInput.text.trim()
        if (newName === "" || newName === renameDialog.originalName) {
            renameDialog.visible = false
            return
        }
        
        if (sheetModel.renameSheet(renameDialog.originalName, newName)) {
            renameDialog.visible = false
            showToast("Renamed to: " + newName)
        } else {
            showToast(sheetModel.errorMessage, true)
        }
    }

    function handleCopy() {
        if (isLibraryMode || detailList.count === 0 || detailList.currentIndex < 0) return
        
        var cmd = sheetModel.getEntryCommand(detailList.currentIndex)
        if (cmd !== "") {
            appController.copyToClipboard(cmd)
        }
    }

    function handleSave() {
        if (!isEditing) return
        if (inCmd.text.trim() === "") {
            showToast("Command cannot be empty", true)
            inCmd.forceActiveFocus()
            return
        }
        
        var cat = inCat.text.trim() || "General"
        var cmd = inCmd.text.trim()
        var desc = inDesc.text.trim()
        
        var success = false
        if (editingIndex >= 0) {
            success = sheetModel.updateEntry(editingIndex, cat, cmd, desc)
        } else {
            success = sheetModel.addEntry(cat, cmd, desc)
        }
        
        if (success) {
            cancelEdit()
            showToast(editingIndex >= 0 ? "Entry updated" : "Entry added")
        }
    }

    // ============== TRANSITIONS ==============
    function morphToDetail(name) {
        sheetModel.setMode(1, name)
        isLibraryMode = false
        isEditing = false
        editingIndex = -1
        mainStack.currentIndex = 1
        centerWindow(950, 650)
        detailSearch.text = ""
        detailList.currentIndex = 0
        detailSearch.forceActiveFocus()
    }

    function morphToLibrary() {
        sheetModel.setMode(0, "")
        isLibraryMode = true
        isEditing = false
        editingIndex = -1
        mainStack.currentIndex = 0
        centerWindow(400, 500)
        libSearch.text = ""
        libList.currentIndex = 0
        libSearch.forceActiveFocus()
    }

    function createNewSheet(name) {
        if (sheetModel.createSheet(name)) {
            nameDialog.visible = false
            morphToDetail(name)
        } else {
            showToast(sheetModel.errorMessage, true)
        }
    }

    function startNewEntry() {
        editingIndex = -1
        inCat.text = ""
        inCmd.text = ""
        inDesc.text = ""
        isEditing = true
        inCat.forceActiveFocus()
    }

    function startEdit(index) {
        editingIndex = index
        inCat.text = sheetModel.getEntryCategory(index)
        inCmd.text = sheetModel.getEntryCommand(index)
        inDesc.text = sheetModel.getEntryDescription(index)
        isEditing = true
        inCat.forceActiveFocus()
    }

    function cancelEdit() {
        isEditing = false
        editingIndex = -1
        inCat.text = ""
        inCmd.text = ""
        inDesc.text = ""
        detailSearch.forceActiveFocus()
    }

    function confirmDelete() {
        var success = sheetModel.remove(deleteDialog.itemIndex)
        deleteDialog.visible = false
        if (success) {
            showToast("Deleted successfully")
            if (!isLibraryMode) detailSearch.forceActiveFocus()
        }
    }

    // ============== TOAST NOTIFICATION ==============
    function showToast(message, isError) {
        toastText.text = message
        toastBg.color = isError ? redColor : greenColor
        toastAnimation.restart()
    }

    // ============== UI COMPONENTS ==============
    Rectangle {
        id: background
        anchors.fill: parent
        color: bgColorTransparent
        radius: 16
        border.color: Qt.rgba(1, 1, 1, 0.1)
        border.width: 1

        // Drop shadow effect simulation
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            z: -1
            radius: parent.radius + 1
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.3)
            border.width: 2
        }

        StackLayout {
            id: mainStack
            anchors.fill: parent
            anchors.margins: 1
            currentIndex: 0

            // ========== VIEW 1: LIBRARY ==========
            ColumnLayout {
                spacing: 0

                // Header with title and search
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        anchors.topMargin: 16
                        anchors.bottomMargin: 12
                        spacing: 12

                        Text {
                            text: "CheatPad"
                            color: textColor
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                            Layout.alignment: Qt.AlignHCenter
                        }

                        TextField {
                            id: libSearch
                            Layout.fillWidth: true
                            placeholderText: "Search sheets..."
                            placeholderTextColor: subtextColor
                            color: textColor
                            font.pixelSize: 14
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 8
                            bottomPadding: 8
                            
                            background: Rectangle {
                                color: surfaceColor
                                radius: 8
                                border.color: libSearch.activeFocus ? accentColor : "transparent"
                                border.width: 2
                            }

                            onTextChanged: sheetModel.applyFilter(text)
                            
                            Keys.onReturnPressed: {
                                // Move focus to list and open first sheet
                                if (libList.count > 0) {
                                    libList.currentIndex = 0
                                    var sheetName = sheetModel.data(sheetModel.index(0, 0), SheetModel.NameRole)
                                    morphToDetail(sheetName)
                                }
                            }
                            
                            Keys.onDownPressed: {
                                // Quick navigation to list
                                libList.forceActiveFocus()
                            }
                        }
                    }
                }

                // Separator
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: overlayColor
                }

                // Sheet List
                ListView {
                    id: libList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 8
                    clip: true
                    model: sheetModel
                    currentIndex: 0
                    spacing: 4
                    
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        width: libList.width
                        height: 52
                        radius: 8
                        color: libList.currentIndex === index ? Qt.rgba(137, 180, 250, 0.12) : "transparent"
                        border.color: libList.currentIndex === index ? Qt.rgba(137, 180, 250, 0.3) : "transparent"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: model.sheetName || ""
                                    color: libList.currentIndex === index ? textColor : subtextColor
                                    font.pixelSize: 14
                                    font.weight: libList.currentIndex === index ? Font.Medium : Font.Normal
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: (model.entryCount || 0) + " entries"
                                    color: Qt.rgba(166, 173, 186, 0.7)
                                    font.pixelSize: 11
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                libList.currentIndex = index
                                morphToDetail(model.sheetName)
                            }
                            onEntered: if (libList.currentIndex !== index) parent.color = Qt.rgba(137, 180, 250, 0.06)
                            onExited: parent.color = libList.currentIndex === index ? Qt.rgba(137, 180, 250, 0.12) : "transparent"
                        }
                    }

                    // Empty state
                    Rectangle {
                        anchors.centerIn: parent
                        visible: libList.count === 0
                        width: parent.width - 40
                        height: 100
                        color: "transparent"

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 8

                            Text {
                                text: libSearch.text === "" ? "No cheat sheets yet" : "No matches found"
                                color: subtextColor
                                font.pixelSize: 14
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Text {
                                text: "Press Ctrl+N to create one"
                                color: Qt.rgba(137, 180, 250, 0.8)
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                                visible: libSearch.text === ""
                            }
                        }
                    }
                }

                // Footer with shortcuts
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: surfaceColor
                    radius: 16
                    
                    // Cover top corners to make only bottom rounded
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: parent.radius
                        color: parent.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16

                        Text {
                            text: "Ctrl+N New"
                            color: subtextColor
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Ctrl+E Rename"
                            color: subtextColor
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Ctrl+D Delete"
                            color: subtextColor
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Enter Open"
                            color: subtextColor
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Esc Close"
                            color: subtextColor
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ========== VIEW 2: DETAIL ==========
            ColumnLayout {
                spacing: 0

                // Header with back button and title
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 16

                        // Back button
                        Rectangle {
                            width: 36
                            height: 36
                            radius: 8
                            color: backMouseArea.containsMouse ? overlayColor : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "←"
                                color: textColor
                                font.pixelSize: 18
                            }

                            MouseArea {
                                id: backMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: morphToLibrary()
                            }
                        }

                        // Title
                        Text {
                            text: sheetModel.currentSheet()
                            color: accentColor
                            font.pixelSize: 22
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        // Search
                        TextField {
                            id: detailSearch
                            Layout.preferredWidth: 200
                            placeholderText: "🔍 Filter..."
                            placeholderTextColor: subtextColor
                            color: textColor
                            font.pixelSize: 13
                            leftPadding: 10
                            rightPadding: 10
                            topPadding: 6
                            bottomPadding: 6
                            
                            background: Rectangle {
                                color: surfaceColor
                                radius: 6
                                border.color: detailSearch.activeFocus ? accentColor : "transparent"
                                border.width: 2
                            }

                            onTextChanged: sheetModel.applyFilter(text)
                            
                            Keys.onReturnPressed: {
                                // Move focus to list and select first item
                                detailList.forceActiveFocus()
                                if (detailList.count > 0) {
                                    detailList.currentIndex = 0
                                }
                            }
                            
                            Keys.onDownPressed: {
                                // Quick navigation to list
                                detailList.forceActiveFocus()
                            }
                        }

                        // Entry count badge
                        Rectangle {
                            width: countText.width + 16
                            height: 24
                            radius: 12
                            color: surfaceColor

                            Text {
                                id: countText
                                anchors.centerIn: parent
                                text: detailList.count + " items"
                                color: subtextColor
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // Column headers
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: surfaceColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 16

                        Text {
                            text: "COMMAND"
                            color: accentColor
                            font.pixelSize: 11
                            font.bold: true
                            Layout.preferredWidth: 280
                        }

                        Text {
                            text: "DESCRIPTION"
                            color: accentColor
                            font.pixelSize: 11
                            font.bold: true
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "CATEGORY"
                            color: accentColor
                            font.pixelSize: 11
                            font.bold: true
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                // Entry List
                ListView {
                    id: detailList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sheetModel
                    currentIndex: 0

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        width: detailList.width
                        height: 44
                        color: detailList.currentIndex === index ? Qt.rgba(137, 180, 250, 0.12) : "transparent"
                        
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: accentColor
                            visible: detailList.currentIndex === index
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 16

                            // Command
                            Text {
                                text: model.command || ""
                                color: greenColor
                                font.family: "JetBrains Mono, Fira Code, Consolas, monospace"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.preferredWidth: 280
                            }

                            // Description
                            Text {
                                text: model.description || ""
                                color: textColor
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Category badge
                            Rectangle {
                                Layout.preferredWidth: Math.min(catText.width + 16, 120)
                                height: 22
                                radius: 4
                                color: overlayColor

                                Text {
                                    id: catText
                                    anchors.centerIn: parent
                                    text: model.category || "General"
                                    color: subtextColor
                                    font.pixelSize: 10
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                detailList.currentIndex = index
                            }
                            onDoubleClicked: {
                                detailList.currentIndex = index
                                startEdit(index)
                            }
                            onEntered: parent.color = Qt.rgba(137, 180, 250, 0.06)
                            onExited: parent.color = detailList.currentIndex === index ? Qt.rgba(137, 180, 250, 0.12) : "transparent"
                        }
                    }

                    // Empty state
                    Rectangle {
                        anchors.centerIn: parent
                        visible: detailList.count === 0
                        width: parent.width - 40
                        height: 120
                        color: "transparent"

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 12

                            Text {
                                text: "📝"
                                font.pixelSize: 40
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Text {
                                text: detailSearch.text === "" ? "No entries yet" : "No matches found"
                                color: subtextColor
                                font.pixelSize: 14
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Text {
                                text: "Press Ctrl+N to add one"
                                color: accentColor
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignHCenter
                                visible: detailSearch.text === ""
                            }
                        }
                    }
                }

                // Editor Panel (slides in from bottom)
                Rectangle {
                    id: editorPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: isEditing ? 160 : 0
                    color: surfaceColor
                    clip: true

                    Behavior on Layout.preferredHeight {
                        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        visible: isEditing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: editingIndex >= 0 ? "✏️ Edit Entry" : "➕ New Entry"
                                color: accentColor
                                font.bold: true
                                font.pixelSize: 14
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                text: "Ctrl+S Save | Esc Cancel"
                                color: subtextColor
                                font.pixelSize: 11
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            TextField {
                                id: inCat
                                Layout.preferredWidth: 150
                                placeholderText: "Category"
                                placeholderTextColor: subtextColor
                                color: textColor
                                font.pixelSize: 13
                                leftPadding: 10
                                rightPadding: 10
                                topPadding: 8
                                bottomPadding: 8
                                
                                KeyNavigation.tab: inCmd
                                KeyNavigation.backtab: inDesc
                                Keys.onTabPressed: { inCmd.forceActiveFocus(); event.accepted = true }
                                Keys.onBacktabPressed: { inDesc.forceActiveFocus(); event.accepted = true }

                                background: Rectangle {
                                    color: bgColor
                                    radius: 6
                                    border.color: inCat.activeFocus ? accentColor : overlayColor
                                    border.width: 1
                                }
                            }

                            TextField {
                                id: inCmd
                                Layout.fillWidth: true
                                placeholderText: "Command *"
                                placeholderTextColor: subtextColor
                                color: greenColor
                                font.family: "JetBrains Mono, Fira Code, Consolas, monospace"
                                font.pixelSize: 13
                                leftPadding: 10
                                rightPadding: 10
                                topPadding: 8
                                bottomPadding: 8
                                
                                KeyNavigation.tab: inDesc
                                KeyNavigation.backtab: inCat
                                Keys.onTabPressed: { inDesc.forceActiveFocus(); event.accepted = true }
                                Keys.onBacktabPressed: { inCat.forceActiveFocus(); event.accepted = true }

                                background: Rectangle {
                                    color: bgColor
                                    radius: 6
                                    border.color: inCmd.activeFocus ? accentColor : overlayColor
                                    border.width: 1
                                }
                            }
                        }

                        TextField {
                            id: inDesc
                            Layout.fillWidth: true
                            placeholderText: "Description (optional)"
                            placeholderTextColor: subtextColor
                            color: textColor
                            font.pixelSize: 13
                            leftPadding: 10
                            rightPadding: 10
                            topPadding: 8
                            bottomPadding: 8
                            
                            KeyNavigation.tab: inCat
                            KeyNavigation.backtab: inCmd
                            Keys.onTabPressed: { inCat.forceActiveFocus(); event.accepted = true }
                            Keys.onBacktabPressed: { inCmd.forceActiveFocus(); event.accepted = true }

                            background: Rectangle {
                                color: bgColor
                                radius: 6
                                border.color: inDesc.activeFocus ? accentColor : overlayColor
                                border.width: 1
                            }
                        }
                    }
                }

                // Footer with shortcuts
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: surfaceColor
                    radius: 16
                    
                    // Cover top corners to make only bottom rounded
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: parent.radius
                        color: parent.color
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 16

                        // Show different shortcuts based on editing mode
                        Text { 
                            text: isEditing ? "Ctrl+S Save" : "Ctrl+N Add"
                            color: subtextColor
                            font.pixelSize: 10 
                        }
                        Text { 
                            text: isEditing ? "Ctrl+Tab Next Field" : "Ctrl+E Edit"
                            color: subtextColor
                            font.pixelSize: 10 
                        }
                        Text { 
                            text: isEditing ? "Esc Cancel" : "Enter Copy"
                            color: subtextColor
                            font.pixelSize: 10 
                            visible: !isEditing || isEditing
                        }
                        Text { 
                            text: "Ctrl+D Delete"
                            color: subtextColor
                            font.pixelSize: 10
                            visible: !isEditing
                        }
                        Text { 
                            text: "Ctrl+I Import"
                            color: subtextColor
                            font.pixelSize: 10
                            visible: !isEditing
                        }
                        Item { Layout.fillWidth: true }
                        Text { 
                            text: "↑↓ Navigate"
                            color: subtextColor
                            font.pixelSize: 10
                            visible: !isEditing
                        }
                        Text { 
                            text: "Ctrl+B Back"
                            color: subtextColor
                            font.pixelSize: 10
                            visible: !isEditing
                        }
                    }
                }
            }
        }

        // ========== DIALOGS ==========
        
        // New Sheet Dialog
        FocusScope {
            id: nameDialog
            visible: false
            anchors.fill: parent
            focus: visible

            onVisibleChanged: if (visible) newSheetName.forceActiveFocus()

            Keys.onEscapePressed: {
                nameDialog.visible = false
                libSearch.forceActiveFocus()
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 320
                height: 160
                radius: 12
                color: bgColor
                border.color: accentColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text {
                        text: "📋 Create New Sheet"
                        color: accentColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    TextField {
                        id: newSheetName
                        Layout.fillWidth: true
                        placeholderText: "Enter sheet name..."
                        placeholderTextColor: subtextColor
                        color: textColor
                        font.pixelSize: 14
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10

                        background: Rectangle {
                            color: surfaceColor
                            radius: 8
                            border.color: newSheetName.activeFocus ? accentColor : overlayColor
                            border.width: 2
                        }

                        Keys.onReturnPressed: {
                            if (text.trim() !== "") {
                                createNewSheet(text.trim())
                            }
                        }

                        Keys.onEscapePressed: {
                            nameDialog.visible = false
                            libSearch.forceActiveFocus()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Esc] Cancel"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: { nameDialog.visible = false; libSearch.forceActiveFocus() }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: accentColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Enter] Create"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (newSheetName.text.trim() !== "") {
                                        createNewSheet(newSheetName.text.trim())
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // CSV Import Dialog
        FocusScope {
            id: importDialog
            visible: false
            anchors.fill: parent
            focus: visible

            onVisibleChanged: if (visible) importPathInput.forceActiveFocus()

            Keys.onEscapePressed: {
                importDialog.visible = false
                detailSearch.forceActiveFocus()
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 450
                height: 200
                radius: 12
                color: bgColor
                border.color: accentColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 12

                    Text {
                        text: "📥 Import CSV to " + sheetModel.currentSheet()
                        color: accentColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Enter full path or filename (searches in ~, ~/Downloads, ~/Documents)"
                        color: subtextColor
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    TextField {
                        id: importPathInput
                        Layout.fillWidth: true
                        placeholderText: "/path/to/file.csv or filename.csv"
                        placeholderTextColor: subtextColor
                        color: textColor
                        font.pixelSize: 14
                        font.family: "monospace"
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 10
                        bottomPadding: 10

                        background: Rectangle {
                            color: surfaceColor
                            radius: 8
                            border.color: importPathInput.activeFocus ? accentColor : overlayColor
                            border.width: 2
                        }

                        Keys.onReturnPressed: {
                            doImportCsv(text)
                        }

                        Keys.onEscapePressed: {
                            importDialog.visible = false
                            detailSearch.forceActiveFocus()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Esc] Cancel"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: { 
                                    importDialog.visible = false
                                    detailSearch.forceActiveFocus() 
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: accentColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Enter] Import"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    doImportCsv(importPathInput.text)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Delete Confirmation Dialog
        FocusScope {
            id: deleteDialog
            visible: false
            anchors.fill: parent
            focus: visible

            property string itemName: ""
            property int itemIndex: -1

            onVisibleChanged: if (visible) forceActiveFocus()

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Y) { confirmDelete(); event.accepted = true }
                else if (event.key === Qt.Key_N || event.key === Qt.Key_Escape) { deleteDialog.visible = false; event.accepted = true }
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 320
                height: 160
                radius: 12
                color: bgColor
                border.color: redColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text {
                        text: "⚠️ Delete Confirmation"
                        color: redColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Delete \"" + deleteDialog.itemName + "\"?"
                        color: textColor
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[N] Cancel"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: deleteDialog.visible = false
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: redColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Y] Delete"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: confirmDelete()
                            }
                        }
                    }
                }
            }
        }

        // Rename Sheet Dialog
        FocusScope {
            id: renameDialog
            visible: false
            anchors.fill: parent
            focus: visible

            property string originalName: ""

            onVisibleChanged: if (visible) renameSheetInput.forceActiveFocus()

            Keys.onEscapePressed: {
                renameDialog.visible = false
                libSearch.forceActiveFocus()
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 360
                height: 180
                radius: 12
                color: bgColor
                border.color: accentColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text {
                        text: "📝 Rename Cheatsheet"
                        color: accentColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    TextField {
                        id: renameSheetInput
                        Layout.fillWidth: true
                        placeholderText: "Enter new name..."
                        placeholderTextColor: subtextColor
                        color: textColor
                        font.pixelSize: 14
                        background: Rectangle {
                            color: overlayColor
                            radius: 6
                            border.color: renameSheetInput.activeFocus ? accentColor : "transparent"
                            border.width: 1
                        }
                        padding: 10

                        Keys.onReturnPressed: {
                            if (text.trim() !== "") {
                                performRenameSheet()
                            }
                        }
                        Keys.onEscapePressed: {
                            renameDialog.visible = false
                            libSearch.forceActiveFocus()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Esc] Cancel"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    renameDialog.visible = false
                                    libSearch.forceActiveFocus()
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: accentColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Enter] Rename"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (renameSheetInput.text.trim() !== "") {
                                        performRenameSheet()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Quit Confirmation Dialog
        FocusScope {
            id: quitDialog
            visible: false
            anchors.fill: parent
            focus: visible

            onVisibleChanged: if (visible) forceActiveFocus()

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Y) { confirmQuit(); event.accepted = true }
                else if (event.key === Qt.Key_N || event.key === Qt.Key_Escape) { quitDialog.visible = false; event.accepted = true }
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 320
                height: 160
                radius: 12
                color: bgColor
                border.color: yellowColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text {
                        text: "🚪 Quit CheatPad"
                        color: yellowColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Are you sure you want to quit?"
                        color: textColor
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[N] Cancel"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: quitDialog.visible = false
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: yellowColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Y] Quit"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: confirmQuit()
                            }
                        }
                    }
                }
            }
        }

        // Discard Changes Confirmation Dialog
        FocusScope {
            id: discardDialog
            visible: false
            anchors.fill: parent
            focus: visible

            onVisibleChanged: if (visible) forceActiveFocus()

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Y) { confirmDiscard(); event.accepted = true }
                else if (event.key === Qt.Key_N || event.key === Qt.Key_Escape) { discardDialog.visible = false; event.accepted = true }
            }

            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                radius: background.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 340
                height: 180
                radius: 12
                color: bgColor
                border.color: yellowColor
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    Text {
                        text: "⚠️ Unsaved Changes"
                        color: yellowColor
                        font.pixelSize: 16
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "You have unsaved changes.\nDiscard them?"
                        color: textColor
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: overlayColor

                            Text {
                                anchors.centerIn: parent
                                text: "[N] Keep Editing"
                                color: textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: discardDialog.visible = false
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 6
                            color: redColor

                            Text {
                                anchors.centerIn: parent
                                text: "[Y] Discard"
                                color: bgColor
                                font.bold: true
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: confirmDiscard()
                            }
                        }
                    }
                }
            }
        }

        // Toast Notification
        Rectangle {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 60
            width: toastText.width + 32
            height: 36
            radius: 18
            opacity: 0
            z: 100

            Rectangle {
                id: toastBg
                anchors.fill: parent
                radius: parent.radius
                color: greenColor
            }

            Text {
                id: toastText
                anchors.centerIn: parent
                text: ""
                color: bgColor
                font.pixelSize: 12
                font.bold: true
            }

            SequentialAnimation {
                id: toastAnimation
                
                NumberAnimation {
                    target: toast
                    property: "opacity"
                    to: 1
                    duration: 200
                }
                
                PauseAnimation { duration: 2000 }
                
                NumberAnimation {
                    target: toast
                    property: "opacity"
                    to: 0
                    duration: 300
                }
            }
        }
    }
}
