#pragma once

#include "core.hpp"
#include "metadata/metadata_models.hpp"
#include "storage/history_repository.hpp"

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QSettings>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTextEdit;
class QVBoxLayout;

namespace vdp {

class DownloadManager;
class MetadataService;
class ToolUpdater;
class ToolchainService;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr, bool initializeRuntime = true);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct TaskCard {
        QString id;
        QFrame* card = nullptr;
        QLabel* thumbnail = nullptr;
        QLabel* titleLabel = nullptr;
        QLabel* metadataLabel = nullptr;
        QLabel* statusLabel = nullptr;
        QLabel* speedLabel = nullptr;
        QProgressBar* progress = nullptr;
        QPushButton* cancelButton = nullptr;
        QPushButton* removeButton = nullptr;
    };

    AppPaths paths_;
    ToolchainManager toolchain_;
    QSettings settings_;
    ToolchainStatus toolStatus_;
    HistoryRepository history_;
    DownloadManager* downloads_ = nullptr;
    MetadataService* metadata_ = nullptr;
    ToolUpdater* updater_ = nullptr;
    ToolchainService* runtime_ = nullptr;

    QListWidget* navigation_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* urlEdit_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QComboBox* advancedCombo_ = nullptr;
    QLabel* previewTitle_ = nullptr;
    QLabel* previewDetails_ = nullptr;
    QLabel* previewArtwork_ = nullptr;
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
    QLabel* ffprobeStatus_ = nullptr;
    QProgressBar* toolProgress_ = nullptr;
    QPushButton* cancelUpdate_ = nullptr;
    QList<QPushButton*> toolButtons_;
    QTextEdit* toolLog_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QSpinBox* parallelSpin_ = nullptr;
    QCheckBox* autoOpenCheck_ = nullptr;
    QNetworkAccessManager* thumbnailNetwork_ = nullptr;

    QString checkedUrl_;
    VideoMetadata checkedMetadata_;
    QString previewRequestId_;
    QString lastError_;
    QStringList repairQueue_;

    QHash<QString, TaskCard*> cards_;

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
    void setupServices(bool initializeRuntime);
    void loadSettings();
    void saveSettings();
    void refreshToolStatus(bool versions = false);
    void runToolAction(const QString& action);
    void checkUrl();
    void addDownload();
    void createTaskCard(const QString& id);
    void refreshTaskCard(const QString& id);
    void removeTaskCard(const QString& id);
    void applyTaskMetadata(const QString& id, const VideoMetadata& metadata);
    void loadThumbnail(const QString& url, QLabel* target);
    void updateQueueStatus();
    void updateToolActions();
    void continueRepair();
    void loadHistory(const QString& query = {});
    void chooseOutputDirectory();
    void showMessage(const QString& text, bool error = false);
    void openPath(const QString& path);
};

} // namespace vdp
