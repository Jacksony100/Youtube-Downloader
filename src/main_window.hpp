#pragma once

#include "core.hpp"

#include <QHash>
#include <QMainWindow>
#include <QSettings>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QVBoxLayout;

namespace vdp {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Task {
        QString id;
        QString url;
        FormatPreset preset;
        QString title;
        QString outputPath;
        QString stderrText;
        QByteArray stdoutBuffer;
        QFrame* card = nullptr;
        QLabel* titleLabel = nullptr;
        QLabel* statusLabel = nullptr;
        QLabel* speedLabel = nullptr;
        QProgressBar* progress = nullptr;
        QPushButton* cancelButton = nullptr;
        QProcess* process = nullptr;
        bool running = false;
        bool completed = false;
    };

    AppPaths paths_;
    ToolchainManager toolchain_;
    QSettings settings_;
    ToolchainStatus toolStatus_;

    QListWidget* navigation_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* urlEdit_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QLabel* previewTitle_ = nullptr;
    QLabel* previewDetails_ = nullptr;
    QLabel* queueStatus_ = nullptr;
    QLabel* ytdlpChip_ = nullptr;
    QLabel* ffmpegChip_ = nullptr;
    QVBoxLayout* queueLayout_ = nullptr;
    QWidget* emptyQueue_ = nullptr;
    QListWidget* historyList_ = nullptr;
    QLineEdit* historySearch_ = nullptr;
    QLabel* ytdlpStatus_ = nullptr;
    QLabel* denoStatus_ = nullptr;
    QLabel* ffmpegStatus_ = nullptr;
    QTextEdit* toolLog_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QSpinBox* parallelSpin_ = nullptr;
    QCheckBox* autoOpenCheck_ = nullptr;
    QProcess* metadataProcess_ = nullptr;

    QHash<QString, Task*> tasks_;
    QStringList pending_;
    int runningCount_ = 0;

    QWidget* createDownloadsPage();
    QWidget* createHistoryPage();
    QWidget* createToolsPage();
    QWidget* createSettingsPage();
    QWidget* createAboutPage();
    QWidget* createCard(const QString& title, QWidget* content = nullptr) const;
    QLabel* createMutedLabel(const QString& text) const;
    QPushButton* createButton(const QString& text, const QString& objectName = "SecondaryButton") const;
    void setupUi();
    void setupMenu();
    void loadSettings();
    void saveSettings();
    void refreshToolStatus(bool versions = false);
    void runToolAction(const QString& action);
    void checkUrl();
    void addDownload();
    Task* createTask(const QString& url, const FormatPreset& preset);
    void startNextDownloads();
    void startTask(Task* task);
    void consumeTaskOutput(Task* task, bool flush = false);
    void finishTask(Task* task, int exitCode);
    void cancelTask(Task* task);
    void updateQueueStatus();
    void addHistory(Task* task, const QString& status, const QString& error = {});
    void loadHistory(const QString& query = {});
    void chooseOutputDirectory();
    void showMessage(const QString& text, bool error = false);
    void openPath(const QString& path);
};

} // namespace vdp
