#pragma once

#include <QDialog>

// Forward declarations
class QCheckBox;
class QPushButton;
class SettingsManager; // To interact with settings
class QLabel;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(SettingsManager *settingsManager, QWidget *parent = nullptr);
    ~SettingsDialog() override;

private slots:
    void onSaveClicked();
    void onViewFileClicked();

private:
    void setupUi();
    void loadSettings();

    // UI Elements
    QCheckBox *m_startupCheckBox = nullptr;
    QPushButton *m_viewFileButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QLabel *m_filePathLabel = nullptr;

    // Core components
    SettingsManager *m_settingsManager = nullptr; // Non-owning pointer
}; 