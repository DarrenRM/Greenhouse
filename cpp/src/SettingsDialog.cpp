#include "../include/SettingsDialog.h"
#include "../include/SettingsManager.h"

#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDesktopServices> // For opening file/folder
#include <QUrl>
#include <QMessageBox> // For error messages
#include <QDebug>
#include <QDir> // Include for QDir

SettingsDialog::SettingsDialog(SettingsManager *settingsManager, QWidget *parent)
    : QDialog(parent),
      m_settingsManager(settingsManager)
{
    Q_ASSERT(m_settingsManager != nullptr);

    setupUi();
    loadSettings();

    setWindowTitle(tr("Settings"));
    setMinimumSize(350, 200);
}

SettingsDialog::~SettingsDialog() {
    qInfo() << "SettingsDialog destroyed";
}

void SettingsDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- Startup Option --- 
    m_startupCheckBox = new QCheckBox(tr("Start Greenhouse with Windows"), this);
    mainLayout->addWidget(m_startupCheckBox);

    mainLayout->addSpacing(10);

    // --- File Viewing Section --- 
    QGroupBox *fileGroup = new QGroupBox(tr("View Data File"), this);
    QVBoxLayout *fileLayout = new QVBoxLayout(fileGroup);
    
    m_viewFileButton = new QPushButton(tr("View Saved Positions File"), fileGroup);
    fileLayout->addWidget(m_viewFileButton);

    m_filePathLabel = new QLabel(fileGroup);
    m_filePathLabel->setWordWrap(true);
    fileLayout->addWidget(m_filePathLabel);

    mainLayout->addWidget(fileGroup);
    mainLayout->addStretch();

    // --- Dialog Buttons --- 
    QDialogButtonBox *buttonBox = new QDialogButtonBox(this);
    m_saveButton = buttonBox->addButton(QDialogButtonBox::Save);
    m_cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    // --- Connections --- 
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsDialog::onSaveClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsDialog::reject); // Standard reject action
    connect(m_viewFileButton, &QPushButton::clicked, this, &SettingsDialog::onViewFileClicked);
}

void SettingsDialog::loadSettings() {
    // Load startup setting
    m_startupCheckBox->setChecked(m_settingsManager->getStartup());

    // Show file path
    QString path = m_settingsManager->getSettingsFilePath();
    QString dirPath = QFileInfo(path).absolutePath();
    m_filePathLabel->setText(tr("Location: %1").arg(QDir::toNativeSeparators(dirPath)));
}

// --- Slots --- 

void SettingsDialog::onSaveClicked() {
    qInfo() << "Save button clicked in SettingsDialog";
    // Save startup setting
    m_settingsManager->setStartup(m_startupCheckBox->isChecked());
    
    // Optionally show confirmation?

    accept(); // Standard accept action (closes dialog with Accepted status)
}

void SettingsDialog::onViewFileClicked() {
    QString filePath = m_settingsManager->getSettingsFilePath();
    QFileInfo fileInfo(filePath);

    // Open the directory containing the file
    QString dirPath = fileInfo.absolutePath();
    if (QDir(dirPath).exists()) {
        bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
        if (!success) {
            qWarning() << "Failed to open directory:" << dirPath;
            QMessageBox::warning(this, tr("Error"), tr("Could not open the directory containing the settings file."));
        }
    } else {
         QMessageBox::information(this, tr("Directory Not Found"), 
                                  tr("The settings directory hasn't been created yet. It will be created automatically when needed."));
    }
} 