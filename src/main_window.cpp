#include "main_window.hpp"
#include "downloads/download_manager.hpp"
#include "metadata/metadata_service.hpp"
#include "runtime/tool_updater.hpp"
#include "runtime/toolchain_service.hpp"
#include "diagnostics/diagnostics.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>

#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>

#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QTimeZone>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QUuid>

namespace vdp {

namespace {
QLabel* heading(const QString& text, const QString& objectName = "PageTitle") {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    return label;
}

bool validUrl(const QString& value) {
    const QUrl url = QUrl::fromUserInput(value.trimmed());
    return url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty();
}

}

MainWindow::MainWindow(QWidget* parent, bool initializeRuntime)
    : QMainWindow(parent), paths_(AppPaths::defaults()), toolchain_(paths_),
      settings_(paths_.settingsFile, QSettings::IniFormat), history_(paths_.historyFile) {
    thumbnailNetwork_ = new QNetworkAccessManager(this);
    downloads_ = new DownloadManager(&toolchain_, this);
    metadata_ = new MetadataService(&toolchain_, this);
    updater_ = new ToolUpdater(paths_, this);
    runtime_ = new ToolchainService(paths_, this);
    setWindowTitle(QString("%1 %2").arg(kAppTitle, VDP_VERSION));
    setMinimumSize(1100, 720);
    resize(1280, 820);
    setWindowIcon(QIcon(":/icon.ico"));
    setupUi(); setupMenu(); loadSettings();
    QString historyError;
    if (!history_.load(&historyError)) showMessage(historyError, true);
    loadHistory(); setupServices(initializeRuntime);
}

MainWindow::~MainWindow() {
    downloads_->shutdown();
    // Metadata callbacks refer to ids; QObject connections vanish with their receiver.
    delete metadata_;
    qDeleteAll(cards_);
}

void MainWindow::setupServices(bool initializeRuntime) {
    downloads_->setParallelLimit(parallelSpin_->value());
    connect(parallelSpin_, &QSpinBox::valueChanged, downloads_, &DownloadManager::setParallelLimit);
    connect(downloads_, &DownloadManager::taskAdded, this, &MainWindow::createTaskCard);
    connect(downloads_, &DownloadManager::taskChanged, this, &MainWindow::refreshTaskCard);
    connect(downloads_, &DownloadManager::taskRemoved, this, &MainWindow::removeTaskCard);
    connect(downloads_, &DownloadManager::queueChanged, this, [this] { updateQueueStatus(); updateToolActions(); });
    connect(downloads_, &DownloadManager::storageError, this, [this](const QString& error) { showMessage(error, true); });
    connect(downloads_, &DownloadManager::taskFinished, this, [this](const QString& id) {
        const auto* task = downloads_->task(id); if (!task) return;
        QString error;
        if (!history_.append(*task, &error)) showMessage(error, true);
        appendBoundedLog(paths_.logsDir, "downloads", id + " " + taskStateKey(task->state) + " " + task->errorMessage);
        if (!task->errorMessage.isEmpty()) {
            lastError_ = redactSecrets(task->errorMessage);
            if (!task->failure.technicalMessage.isEmpty())
                lastError_ += "\nТехнические детали: " + redactSecrets(task->failure.technicalMessage);
        }
        loadHistory(historySearch_->text());
        if (task->state == TaskState::Completed && autoOpenCheck_->isChecked()) openPath(task->outputPath);
    });
    connect(metadata_, &MetadataService::ready, this, [this](const QString& id, const VideoMetadata& metadata) {
        updateToolActions();
        if (id.startsWith("task:")) { applyTaskMetadata(id.mid(5), metadata); return; }
        if (id != previewRequestId_) return;
        checkedUrl_ = urlEdit_->text().trimmed(); checkedMetadata_ = metadata;
        previewTitle_->setText(metadata.title);
        QString details = QString("%1 • %2 • %3:%4").arg(metadata.uploader, metadata.sourceHost)
            .arg(metadata.durationSeconds / 60).arg(metadata.durationSeconds % 60, 2, 10, QLatin1Char('0'));
        int top = 0; for (const auto& format : metadata.formats) top = qMax(top, format.height);
        if (top) details += QString(" • до %1p").arg(top);
        previewDetails_->setText(details);
        loadThumbnail(metadata.thumbnailUrl, previewArtwork_);
        advancedCombo_->clear(); advancedCombo_->addItem("Простой пресет", -1);
        for (int i = 0; i < metadata.formats.size(); ++i) advancedCombo_->addItem(metadata.formats[i].label, i);
        advancedCombo_->setVisible(!metadata.formats.isEmpty());
    });
    connect(metadata_, &MetadataService::failed, this, [this](const QString& id, const QString& error) {
        updateToolActions();
        if (id == previewRequestId_) {
            previewTitle_->setText("Не удалось проверить ссылку");
            previewDetails_->setText(error + " Можно добавить ссылку с простым пресетом.");
        }
    });
    connect(urlEdit_, &QLineEdit::textChanged, this, [this] {
        metadata_->cancel(previewRequestId_); previewRequestId_.clear();
        checkedUrl_.clear(); checkedMetadata_ = {}; advancedCombo_->hide();
    });
    connect(runtime_, &ToolchainService::ready, this, [this](const ToolchainStatus& status) {
        toolStatus_ = status; refreshToolStatus();
        if (!repairQueue_.isEmpty()) continueRepair();
        else if (toolStatus_.ready() && !updater_->isBusy()) {
            for (const auto& id : downloads_->taskIds()) {
                const auto* task = downloads_->task(id);
                if (task && !isTerminal(task->state)) metadata_->request("task:" + id, task->url);
            }
            downloads_->start();
        }
        updateToolActions();
    });
    connect(runtime_, &ToolchainService::failed, this, [this](const QString& error) {
        repairQueue_.clear(); showMessage(error, true); updateToolActions();
    });
    connect(updater_, &ToolUpdater::progress, this, [this](const QString&, qint64 received, qint64 total) {
        toolProgress_->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) toolProgress_->setValue(qRound(1000.0 * received / total));
        toolProgress_->setFormat(total > 0 ? "%p%" : QString("Получено %1 МБ").arg(received / 1048576.0, 0, 'f', 1));
    });
    connect(updater_, &ToolUpdater::phaseChanged, this, [this](const QString&, const QString& phase, bool cancellable) {
        toolLog_->append(redactSecrets(phase)); cancelUpdate_->setEnabled(cancellable); updateToolActions();
    });
    connect(updater_, &ToolUpdater::finished, this, [this](const QString& id) {
        toolLog_->append(id + ": обновление завершено.");
        appendBoundedLog(paths_.logsDir, "runtime", id + " updated");
        toolProgress_->setRange(0, 1000); toolProgress_->setValue(1000);
        runtime_->refresh(); updateToolActions();
    });
    connect(updater_, &ToolUpdater::failed, this, [this](const QString&, const QString& error) {
        repairQueue_.clear(); showMessage(error, true); runtime_->refresh(); updateToolActions();
    });
    connect(updater_, &ToolUpdater::cancelled, this, [this](const QString&) {
        repairQueue_.clear(); showMessage("Обновление отменено."); runtime_->refresh(); updateToolActions();
    });
    connect(cancelUpdate_, &QPushButton::clicked, updater_, &ToolUpdater::cancel);
    const int recovered = downloads_->restoreQueue();
    if (recovered) showMessage(QString("Восстановлено задач: %1").arg(recovered));
    if (initializeRuntime) QTimer::singleShot(0, runtime_, &ToolchainService::ensure);
    updateQueueStatus(); updateToolActions();
    appendBoundedLog(paths_.logsDir, "app", "Started " VDP_VERSION);
}

QLabel* MainWindow::createMutedLabel(const QString& text) const {
    auto* label = new QLabel(text);
    label->setObjectName("MutedText");
    label->setWordWrap(true);
    return label;
}

QPushButton* MainWindow::createButton(const QString& text, const QString& objectName) const {
    auto* button = new QPushButton(text);
    button->setObjectName(objectName);
    return button;
}

QWidget* MainWindow::createCard(const QString& title, QWidget* content) const {
    auto* card = new QFrame;
    card->setObjectName("Card");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    if (!title.isEmpty()) layout->addWidget(heading(title, "SectionTitle"));
    if (content) layout->addWidget(content);
    return card;
}

void MainWindow::setupUi() {
    QFile style(":/ui/styles/dark.qss");
    if (style.open(QIODevice::ReadOnly)) qApp->setStyleSheet(QString::fromUtf8(style.readAll()));

    setObjectName("AppWindow");
    auto* root = new QWidget;
    root->setObjectName("AppRoot");
    auto* layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* sidebar = new QFrame;
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(236);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 22, 18, 18);
    sidebarLayout->setSpacing(10);

    auto* brandRow = new QHBoxLayout;
    auto* brandMark = new QLabel("▶");
    brandMark->setObjectName("BrandMark");
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(42, 42);
    auto* brandText = new QVBoxLayout;
    brandText->setSpacing(1);
    auto* brandTitle = new QLabel("Video Downloader");
    brandTitle->setObjectName("BrandTitle");
    auto* brandEdition = new QLabel("PRO  •  ВЕРСИЯ " VDP_VERSION);
    brandEdition->setObjectName("BrandEdition");
    brandText->addWidget(brandTitle);
    brandText->addWidget(brandEdition);
    brandRow->addWidget(brandMark);
    brandRow->addLayout(brandText, 1);
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addSpacing(20);

    auto* navLabel = new QLabel("РАЗДЕЛЫ");
    navLabel->setObjectName("SidebarSection");
    sidebarLayout->addWidget(navLabel);
    navigation_ = new QListWidget;
    navigation_->setObjectName("SidebarNavigation");
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setFocusPolicy(Qt::NoFocus);
    navigation_->addItems({"  Загрузки", "  История", "  Инструменты", "  Настройки", "  О приложении"});
    for (int row = 0; row < navigation_->count(); ++row)
        navigation_->item(row)->setSizeHint(QSize(190, 46));
    navigation_->setCurrentRow(0);
    sidebarLayout->addWidget(navigation_, 1);

    auto* runtimeCard = new QFrame;
    runtimeCard->setObjectName("SidebarRuntimeCard");
    auto* runtimeLayout = new QVBoxLayout(runtimeCard);
    runtimeLayout->setContentsMargins(12, 10, 12, 10);
    runtimeLayout->setSpacing(3);
    auto* runtimeTitle = new QLabel("●  MANAGED RUNTIME");
    runtimeTitle->setObjectName("RuntimeTitle");
    auto* runtimeText = new QLabel("yt-dlp  •  Deno  •  FFmpeg");
    runtimeText->setObjectName("SidebarFooter");
    runtimeLayout->addWidget(runtimeTitle);
    runtimeLayout->addWidget(runtimeText);
    sidebarLayout->addWidget(runtimeCard);
    auto* author = new QLabel("Coded by Jacksony  •  v" VDP_VERSION);
    author->setObjectName("SidebarFooter");
    sidebarLayout->addWidget(author);

    pages_ = new QStackedWidget;
    pages_->addWidget(createDownloadsPage());
    pages_->addWidget(createHistoryPage());
    pages_->addWidget(createToolsPage());
    pages_->addWidget(createSettingsPage());
    pages_->addWidget(createAboutPage());
    connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);

    layout->addWidget(sidebar);
    layout->addWidget(pages_, 1);
    setCentralWidget(root);
}

QWidget* MainWindow::createDownloadsPage() {
    auto* page = new QWidget;
    page->setObjectName("Page");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(30, 26, 30, 20);
    root->setSpacing(16);

    auto* header = new QHBoxLayout;
    auto* headerText = new QVBoxLayout;
    headerText->setSpacing(3);
    headerText->addWidget(heading("Загрузки"));
    auto* subtitle = createMutedLabel("Вставьте ссылку, выберите качество и добавьте видео в очередь.");
    subtitle->setObjectName("PageSubtitle");
    headerText->addWidget(subtitle);
    header->addLayout(headerText, 1);
    ytdlpChip_ = new QLabel("yt-dlp   подготовка");
    ytdlpChip_->setObjectName("StatChip");
    ffmpegChip_ = new QLabel("runtime   подготовка");
    ffmpegChip_->setObjectName("StatChip");
    header->addWidget(ytdlpChip_);
    header->addWidget(ffmpegChip_);
    root->addLayout(header);

    auto* inputCard = new QFrame;
    inputCard->setObjectName("Card");
    auto* inputLayout = new QVBoxLayout(inputCard);
    inputLayout->setContentsMargins(20, 18, 20, 20);
    inputLayout->setSpacing(12);
    auto* inputGrid = new QGridLayout;
    inputGrid->setHorizontalSpacing(10);
    inputGrid->setVerticalSpacing(7);
    auto* urlLabel = new QLabel("ССЫЛКА НА ВИДЕО");
    urlLabel->setObjectName("InputLabel");
    auto* formatLabel = new QLabel("КАЧЕСТВО");
    formatLabel->setObjectName("InputLabel");
    urlEdit_ = new QLineEdit;
    urlEdit_->setObjectName("Input");
    urlEdit_->setAccessibleName("Ссылка на видео");
    urlEdit_->setMinimumHeight(46);
    urlEdit_->setPlaceholderText("https://www.youtube.com/watch?v=...");
    auto* paste = createButton("Вставить");
    auto* check = createButton("Проверить");
    auto* add = createButton("Добавить в очередь", "PrimaryButton");
    formatCombo_ = new QComboBox;
    formatCombo_->setObjectName("Input");
    formatCombo_->setAccessibleName("Простой выбор качества");
    formatCombo_->setMinimumHeight(46);
    for (const auto& preset : formatPresets()) formatCombo_->addItem(preset.label, preset.key);
    for (auto* button : {paste, check, add}) button->setMinimumHeight(46);
    inputGrid->addWidget(urlLabel, 0, 0);
    inputGrid->addWidget(formatLabel, 0, 1);
    inputGrid->addWidget(urlEdit_, 1, 0);
    inputGrid->addWidget(formatCombo_, 1, 1);
    inputGrid->addWidget(paste, 1, 2);
    inputGrid->addWidget(check, 1, 3);
    inputGrid->addWidget(add, 1, 4);
    inputGrid->setColumnStretch(0, 1);
    inputGrid->setColumnMinimumWidth(1, 130);
    inputLayout->addLayout(inputGrid);
    advancedCombo_ = new QComboBox;
    advancedCombo_->setObjectName("AdvancedFormats");
    advancedCombo_->setAccessibleName("Расширенный выбор качества");
    advancedCombo_->hide();
    inputLayout->addWidget(advancedCombo_);

    auto* preview = new QFrame;
    preview->setObjectName("PreviewCard");
    auto* previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(14, 14, 16, 14);
    previewLayout->setSpacing(16);
    previewArtwork_ = new QLabel("PREVIEW\nVIDEO");
    previewArtwork_->setObjectName("PreviewArtwork");
    previewArtwork_->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    previewArtwork_->setFixedSize(168, 92);
    previewArtwork_->setMargin(14);
    auto* previewText = new QVBoxLayout;
    previewText->setSpacing(5);
    previewTitle_ = heading("Ссылка ещё не проверена", "SectionTitle");
    previewDetails_ = createMutedLabel("Нажмите «Проверить», чтобы получить название, автора и длительность.");
    previewText->addWidget(previewTitle_);
    previewText->addWidget(previewDetails_);
    previewText->addStretch();
    previewLayout->addWidget(previewArtwork_);
    previewLayout->addLayout(previewText, 1);
    inputLayout->addWidget(preview);
    root->addWidget(inputCard);

    auto* statusRow = new QHBoxLayout;
    auto* queueTitle = heading("Очередь", "SectionTitle");
    queueStatus_ = createMutedLabel("Активных: 0 • В очереди: 0");
    auto* clearFinished = createButton("Очистить завершённые");
    auto* cancelAll = createButton("Отменить все", "DangerButton");
    statusRow->addWidget(queueTitle);
    statusRow->addWidget(queueStatus_);
    statusRow->addStretch();
    statusRow->addWidget(clearFinished);
    statusRow->addWidget(cancelAll);
    root->addLayout(statusRow);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* container = new QWidget;
    container->setObjectName("QueueContainer");
    queueLayout_ = new QVBoxLayout(container);
    queueLayout_->setAlignment(Qt::AlignTop);
    auto* emptyState = new QFrame;
    emptyState->setObjectName("EmptyState");
    auto* emptyLayout = new QVBoxLayout(emptyState);
    emptyLayout->setContentsMargins(24, 25, 24, 25);
    auto* emptyIcon = new QLabel("↓");
    emptyIcon->setObjectName("EmptyStateIcon");
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto* emptyTitle = heading("Очередь пока пуста", "EmptyStateTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto* emptyText = createMutedLabel("Добавленные видео и аудио появятся здесь.");
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyQueue_ = emptyState;
    queueLayout_->addWidget(emptyQueue_);
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    auto* help = new QFrame;
    help->setObjectName("NoticeCard");
    auto* helpLayout = new QHBoxLayout(help);
    helpLayout->setContentsMargins(16, 12, 16, 12);
    auto* helpText = new QVBoxLayout;
    helpText->setSpacing(3);
    auto* helpTitle = new QLabel("Видео не грузится?");
    helpTitle->setObjectName("NoticeTitle");
    auto* helpBody = new QLabel("Иногда причина в сетевых ограничениях. Попробуйте безопасный интернет-маршрут от onyshop.tech.");
    helpBody->setObjectName("NoticeText");
    helpText->addWidget(helpTitle);
    helpText->addWidget(helpBody);
    helpLayout->addLayout(helpText, 1);
    auto* site = createButton("Открыть onyshop.tech");
    helpLayout->addWidget(site);
    root->addWidget(help);

    connect(paste, &QPushButton::clicked, this, [this] { urlEdit_->setText(QApplication::clipboard()->text().trimmed()); });
    connect(check, &QPushButton::clicked, this, &MainWindow::checkUrl);
    connect(add, &QPushButton::clicked, this, &MainWindow::addDownload);
    connect(urlEdit_, &QLineEdit::returnPressed, this, &MainWindow::addDownload);
    connect(site, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://onyshop.tech")); });
    connect(clearFinished, &QPushButton::clicked, this, [this] {
        const auto ids = downloads_->taskIds();
        for (const auto& id : ids) downloads_->removeTerminal(id);
    });
    connect(cancelAll, &QPushButton::clicked, this, [this] {
        const auto ids = downloads_->taskIds();
        for (const auto& id : ids) downloads_->cancel(id);
    });
    return page;
}

QWidget* MainWindow::createHistoryPage() {
    auto* page = new QWidget;
    page->setObjectName("Page");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(30, 26, 30, 22);
    root->setSpacing(16);
    root->addWidget(heading("История загрузок"));
    auto* subtitle = createMutedLabel("Все завершённые, отменённые и неудачные задачи в одном месте.");
    subtitle->setObjectName("PageSubtitle");
    root->addWidget(subtitle);
    historySearch_ = new QLineEdit;
    historySearch_->setObjectName("Input");
    historySearch_->setMinimumHeight(46);
    historySearch_->setPlaceholderText("Поиск по названию или ссылке");
    historyList_ = new QListWidget;
    historyList_->setObjectName("HistoryList");
    root->addWidget(historySearch_);
    root->addWidget(historyList_, 1);
    connect(historySearch_, &QLineEdit::textChanged, this, &MainWindow::loadHistory);
    connect(historyList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) openPath(path);
    });
    historyList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyList_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = historyList_->itemAt(position); if (!item) return;
        const auto id = item->data(Qt::UserRole + 1).toString();
        HistoryRecord record;
        bool found = false;
        for (const auto& candidate : history_.newestFirst()) if (candidate.task.id == id) { record = candidate; found = true; break; }
        if (!found) return;
        QMenu menu(this);
        auto* open = menu.addAction("Открыть файл");
        auto* folder = menu.addAction("Показать в папке");
        auto* retry = menu.addAction("Скачать снова");
        auto* copy = menu.addAction("Скопировать ссылку");
        auto* remove = menu.addAction("Удалить запись");
        auto* action = menu.exec(historyList_->mapToGlobal(position));
        if (action == open) openPath(record.task.outputPath);
        else if (action == folder) openPath(QFileInfo(record.task.outputPath).absolutePath());
        else if (action == copy) QApplication::clipboard()->setText(record.task.url);
        else if (action == remove) { QString error; if (!history_.remove(id, &error)) showMessage(error, true); loadHistory(historySearch_->text()); }
        else if (action == retry) {
            FormatPreset preset{record.task.formatKey, record.task.formatLabel, record.task.formatSelector,
                record.task.extractAudio, record.task.extension};
            preset.audioQuality = record.task.audioQuality;
            preset.originalAudio = record.task.originalAudio;
            if (preset.selector.isEmpty()) preset = formatPreset(record.task.formatKey);
            downloads_->enqueue({record.task.url, record.task.title,
                record.task.outputDirectory.isEmpty() ? outputEdit_->text() : record.task.outputDirectory, preset});
            navigation_->setCurrentRow(0);
            if (toolStatus_.ready() && !updater_->isBusy() && !runtime_->isBusy()) downloads_->start();
        }
    });
    return page;
}

QWidget* MainWindow::createToolsPage() {
    auto* page = new QWidget;
    page->setObjectName("Page");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(30, 26, 30, 22);
    root->setSpacing(16);
    root->addWidget(heading("Инструменты"));
    auto* subtitle = createMutedLabel("Управляйте компонентами загрузки отдельно от основного приложения.");
    subtitle->setObjectName("PageSubtitle");
    root->addWidget(subtitle);
    auto* hero = new QFrame;
    hero->setObjectName("ToolsHero");
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(22, 18, 22, 18);
    auto* heroText = new QVBoxLayout;
    auto* heroTitle = new QLabel("Managed Runtime");
    heroTitle->setObjectName("HeroTitle");
    auto* heroBody = new QLabel("yt-dlp + Deno + FFmpeg  •  автономно  •  без Python");
    heroBody->setObjectName("HeroText");
    heroText->addWidget(heroTitle);
    heroText->addWidget(heroBody);
    auto* heroBadge = new QLabel("NATIVE");
    heroBadge->setObjectName("HeroBadge");
    heroLayout->addLayout(heroText, 1);
    heroLayout->addWidget(heroBadge);
    root->addWidget(hero);
    auto* card = new QFrame;
    card->setObjectName("Card");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    ytdlpStatus_ = createMutedLabel("yt-dlp: проверка...");
    denoStatus_ = createMutedLabel("Deno: проверка...");
    ffmpegStatus_ = createMutedLabel("ffmpeg: проверка...");
    ytdlpStatus_->setObjectName("ToolStatus");
    denoStatus_->setObjectName("ToolStatus");
    ffmpegStatus_->setObjectName("ToolStatus");
    layout->addWidget(ytdlpStatus_);
    layout->addWidget(denoStatus_);
    layout->addWidget(ffmpegStatus_);
    ffprobeStatus_ = createMutedLabel("ffprobe: проверка...");
    layout->addWidget(ffprobeStatus_);
    auto* buttons = new QHBoxLayout;
    auto* updateYtdlp = createButton("Обновить yt-dlp", "PrimaryButton");
    auto* updateDeno = createButton("Обновить Deno");
    auto* updateFfmpeg = createButton("Обновить ffmpeg");
    auto* repair = createButton("Восстановить runtime", "DangerButton");
    buttons->addWidget(updateYtdlp);
    buttons->addWidget(updateDeno);
    buttons->addWidget(updateFfmpeg);
    buttons->addWidget(repair);
    toolButtons_ = {updateYtdlp, updateDeno, updateFfmpeg, repair};
    cancelUpdate_ = createButton("Отменить обновление");
    cancelUpdate_->setEnabled(false);
    buttons->addWidget(cancelUpdate_);
    layout->addLayout(buttons);
    toolProgress_ = new QProgressBar;
    toolProgress_->setRange(0, 1000); toolProgress_->setValue(0);
    layout->addWidget(toolProgress_);
    root->addWidget(card);
    toolLog_ = new QTextEdit;
    toolLog_->setReadOnly(true);
    toolLog_->document()->setMaximumBlockCount(500);
    toolLog_->setObjectName("LogPanel");
    root->addWidget(toolLog_, 1);
    connect(updateYtdlp, &QPushButton::clicked, this, [this] { runToolAction("ytdlp"); });
    connect(updateDeno, &QPushButton::clicked, this, [this] { runToolAction("deno"); });
    connect(updateFfmpeg, &QPushButton::clicked, this, [this] { runToolAction("ffmpeg"); });
    connect(repair, &QPushButton::clicked, this, [this] { runToolAction("repair"); });
    return page;
}

QWidget* MainWindow::createSettingsPage() {
    auto* page = new QWidget;
    page->setObjectName("Page");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(30, 26, 30, 22);
    root->setSpacing(16);
    root->addWidget(heading("Настройки"));
    auto* subtitle = createMutedLabel("Настройте папку, параллельные загрузки и поведение после завершения.");
    subtitle->setObjectName("PageSubtitle");
    root->addWidget(subtitle);
    auto* card = new QFrame;
    card->setObjectName("Card");
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->setHorizontalSpacing(22);
    form->setVerticalSpacing(18);
    auto* outputRow = new QWidget;
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputEdit_ = new QLineEdit;
    outputEdit_->setObjectName("Input");
    outputEdit_->setMinimumHeight(44);
    auto* browse = createButton("Выбрать");
    outputLayout->addWidget(outputEdit_, 1);
    outputLayout->addWidget(browse);
    parallelSpin_ = new QSpinBox;
    parallelSpin_->setObjectName("Input");
    parallelSpin_->setMinimumHeight(44);
    parallelSpin_->setRange(1, 5);
    autoOpenCheck_ = new QCheckBox("Открывать готовый файл");
    form->addRow("Папка загрузок", outputRow);
    form->addRow("Параллельные задачи", parallelSpin_);
    form->addRow("После завершения", autoOpenCheck_);
    root->addWidget(card);
    root->addStretch();
    connect(browse, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);
    connect(outputEdit_, &QLineEdit::editingFinished, this, &MainWindow::saveSettings);
    connect(parallelSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { saveSettings(); });
    connect(autoOpenCheck_, &QCheckBox::toggled, this, &MainWindow::saveSettings);
    return page;
}

QWidget* MainWindow::createAboutPage() {
    auto* page = new QWidget;
    page->setObjectName("Page");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(30, 26, 30, 22);
    root->setSpacing(16);
    auto* aboutHero = new QFrame;
    aboutHero->setObjectName("AboutHero");
    auto* aboutLayout = new QVBoxLayout(aboutHero);
    aboutLayout->setContentsMargins(32, 30, 32, 30);
    auto* mark = new QLabel("▶");
    mark->setObjectName("AboutMark");
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(56, 56);
    aboutLayout->addWidget(mark);
    aboutLayout->addWidget(heading("Video Downloader Pro"));
    aboutLayout->addWidget(heading(QString("Версия %1").arg(VDP_VERSION), "SectionTitle"));
    auto* description = createMutedLabel("Современное приложение для быстрой загрузки видео и музыки.\nВставьте ссылку, выберите качество — всё остальное приложение сделает само.");
    description->setObjectName("AboutText");
    aboutLayout->addWidget(description);
    auto* repo = createButton("Проект на GitHub", "PrimaryButton");
    connect(repo, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(VDP_REPOSITORY)); });
    aboutLayout->addSpacing(10);
    aboutLayout->addWidget(repo, 0, Qt::AlignLeft);
    auto* diagnostics = createButton("Скопировать диагностику");
    connect(diagnostics, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(diagnosticReport(toolStatus_, lastError_));
        showMessage("Диагностика скопирована.");
    });
    aboutLayout->addWidget(diagnostics, 0, Qt::AlignLeft);
    root->addWidget(aboutHero);
    root->addStretch();
    return page;
}

void MainWindow::setupMenu() {
    auto* file = menuBar()->addMenu("Файл");
    auto* focus = file->addAction("Фокус на ссылке");
    focus->setShortcut(QKeySequence("Ctrl+L"));
    connect(focus, &QAction::triggered, this, [this] { navigation_->setCurrentRow(0); urlEdit_->setFocus(); });
    auto* folder = file->addAction("Открыть папку загрузок");
    folder->setShortcut(QKeySequence("Ctrl+O"));
    connect(folder, &QAction::triggered, this, [this] { openPath(outputEdit_->text()); });
    file->addSeparator();
    file->addAction("Выход", QKeySequence("Ctrl+Q"), this, &QWidget::close);
    auto* downloads = menuBar()->addMenu("Загрузки");
    downloads->addAction("Добавить в очередь", QKeySequence("Ctrl+D"), this, &MainWindow::addDownload);
    downloads->addAction("Проверить ссылку", QKeySequence("Ctrl+I"), this, &MainWindow::checkUrl);
}

void MainWindow::loadSettings() {
    const QString defaultOutput = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    outputEdit_->setText(settings_.value("output_dir", defaultOutput).toString());
    parallelSpin_->setValue(settings_.value("parallel_downloads", 2).toInt());
    autoOpenCheck_->setChecked(settings_.value("auto_open", false).toBool());
    const QString key = settings_.value("format", "best").toString();
    const int index = formatCombo_->findData(key);
    formatCombo_->setCurrentIndex(index < 0 ? 0 : index);
}

void MainWindow::saveSettings() {
    settings_.setValue("output_dir", outputEdit_->text());
    settings_.setValue("parallel_downloads", parallelSpin_->value());
    settings_.setValue("auto_open", autoOpenCheck_->isChecked());
    settings_.setValue("format", formatCombo_->currentData());
    settings_.sync();
}

void MainWindow::refreshToolStatus(bool versions) {
    if (versions) { runtime_->refresh(); return; }
    auto describe = [](const ToolInfo& tool) {
        return QString("%1: %2 • %3 • %4\n%5").arg(tool.name, tool.exists ? "найден" : "не найден",
            tool.version.isEmpty() ? "версия неизвестна" : tool.version,
            tool.verified ? "целостность проверена" : "целостность не подтверждена", tool.path);
    };
    ytdlpStatus_->setText(describe(toolStatus_.ytdlp));
    denoStatus_->setText(describe(toolStatus_.deno));
    ffmpegStatus_->setText(describe(toolStatus_.ffmpeg));
    ffprobeStatus_->setText(describe(toolStatus_.ffprobe));
    ytdlpChip_->setText(QString("yt-dlp: %1").arg(toolStatus_.ytdlp.exists ? "готов" : "нет"));
    ffmpegChip_->setText(QString("runtime: %1").arg(toolStatus_.ready() ? "готов" : "требует внимания"));
    if (!toolStatus_.warning.isEmpty()) toolLog_->append(redactSecrets(toolStatus_.warning));
}

void MainWindow::updateToolActions() {
    const bool busy = updater_->isBusy() || runtime_->isBusy();
    const bool active = downloads_->activeCount() > 0 || metadata_->isBusy();
    for (auto* button : toolButtons_) button->setEnabled(!busy && !active);
    if (!updater_->isBusy()) cancelUpdate_->setEnabled(false);
}

void MainWindow::runToolAction(const QString& action) {
    if (updater_->isBusy() || runtime_->isBusy() || downloads_->activeCount() > 0 || metadata_->isBusy()) {
        showMessage("Дождитесь завершения текущих операций."); return;
    }
    downloads_->setPaused(true);
    if (action == "repair") { repairQueue_ = {"yt-dlp", "deno", "ffmpeg"}; runtime_->repair(); }
    else updater_->start(action == "ytdlp" ? "yt-dlp" : action);
    updateToolActions();
}

void MainWindow::continueRepair() {
    const auto usable = [](const ToolInfo& tool) {
        return tool.exists && !tool.version.isEmpty() && (tool.sha256.isEmpty() || tool.verified);
    };
    while (!repairQueue_.isEmpty()) {
        const auto id = repairQueue_.takeFirst();
        if ((id == "yt-dlp" && usable(toolStatus_.ytdlp)) || (id == "deno" && usable(toolStatus_.deno)) ||
            (id == "ffmpeg" && usable(toolStatus_.ffmpeg) && usable(toolStatus_.ffprobe))) continue;
        updater_->start(id); return;
    }
    if (toolStatus_.ready()) { downloads_->start(); showMessage("Runtime готов."); }
    updateToolActions();
}

void MainWindow::checkUrl() {
    const QString url = urlEdit_->text().trimmed();
    if (!validUrl(url)) { showMessage("Введите корректную ссылку.", true); return; }
    if (updater_->isBusy() || runtime_->isBusy()) { showMessage("Дождитесь завершения обновления runtime."); return; }
    if (!toolStatus_.ytdlp.exists || toolStatus_.ytdlp.version.isEmpty()
        || (!toolStatus_.ytdlp.sha256.isEmpty() && !toolStatus_.ytdlp.verified)) {
        navigation_->setCurrentRow(2); showMessage("yt-dlp не готов. Восстановите runtime.", true); return;
    }
    metadata_->cancel(previewRequestId_);
    previewRequestId_ = "preview:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    checkedUrl_.clear(); checkedMetadata_ = {}; advancedCombo_->hide();
    previewTitle_->setText("Получение информации..."); previewDetails_->setText(url);
    metadata_->request(previewRequestId_, url);
    updateToolActions();
}

void MainWindow::createTaskCard(const QString& id) {
    const auto* model = downloads_->task(id); if (!model || cards_.contains(id)) return;
    auto* task = new TaskCard; task->id = id;
    task->card = new QFrame; task->card->setObjectName("Card"); task->card->setProperty("taskId", id);
    auto* layout = new QHBoxLayout(task->card); layout->setContentsMargins(14, 14, 14, 14); layout->setSpacing(16);
    task->thumbnail = new QLabel("ВИДЕО"); task->thumbnail->setObjectName("TaskThumbnail");
    task->thumbnail->setAlignment(Qt::AlignLeft | Qt::AlignBottom); task->thumbnail->setFixedSize(168, 94); task->thumbnail->setMargin(12);
    layout->addWidget(task->thumbnail);
    auto* details = new QVBoxLayout; details->setSpacing(7);
    auto* top = new QHBoxLayout;
    task->titleLabel = heading(model->title, "SectionTitle"); task->titleLabel->setWordWrap(true);
    task->cancelButton = createButton("Отменить", "DangerButton");
    task->removeButton = createButton("Убрать карточку"); task->removeButton->hide();
    top->addWidget(task->titleLabel, 1); top->addWidget(task->cancelButton); top->addWidget(task->removeButton);
    task->metadataLabel = createMutedLabel(model->formatLabel);
    task->statusLabel = createMutedLabel(taskStateLabel(model->state));
    task->progress = new QProgressBar; task->progress->setRange(0, 1000); task->progress->setValue(0);
    task->speedLabel = createMutedLabel({});
    details->addLayout(top); details->addWidget(task->metadataLabel); details->addWidget(task->statusLabel);
    details->addWidget(task->progress); details->addWidget(task->speedLabel); layout->addLayout(details, 1);
    connect(task->cancelButton, &QPushButton::clicked, this, [this, id] {
        const auto* model = downloads_->task(id); if (!model) return;
        if (model->state == TaskState::Completed) openPath(model->outputPath);
        else if (isTerminal(model->state)) {
            if (updater_->isBusy() || runtime_->isBusy()) { showMessage("Дождитесь завершения операции runtime."); return; }
            downloads_->retry(id);
            if (toolStatus_.ready()) downloads_->start();
        }
        else downloads_->cancel(id);
    });
    connect(task->removeButton, &QPushButton::clicked, this, [this, id] { downloads_->removeTerminal(id); });
    queueLayout_->addWidget(task->card); emptyQueue_->hide(); cards_.insert(id, task);
    if (checkedUrl_ == model->url) applyTaskMetadata(id, checkedMetadata_);
    else if (toolStatus_.ready() && !updater_->isBusy() && !runtime_->isBusy()) metadata_->request("task:" + id, model->url);
    updateToolActions();
    refreshTaskCard(id);
}

void MainWindow::refreshTaskCard(const QString& id) {
    const auto* model = downloads_->task(id); auto* card = cards_.value(id);
    if (!model || !card) return;
    card->titleLabel->setText(model->title);
    card->statusLabel->setText(taskStateLabel(model->state) + (model->errorMessage.isEmpty() ? "" : " • " + model->errorMessage));
    card->progress->setValue(qBound(0, qRound(model->progressPercent * 10), 1000));
    card->speedLabel->setText(model->speed.isEmpty() ? "" : QString("Скорость: %1 • Осталось: %2").arg(model->speed, model->eta));
    card->removeButton->setVisible(isTerminal(model->state));
    card->cancelButton->setText(model->state == TaskState::Completed ? "Открыть" : isTerminal(model->state) ? "Повторить" : "Отменить");
}

void MainWindow::removeTaskCard(const QString& id) {
    metadata_->cancel("task:" + id);
    auto* card = cards_.take(id); if (!card) return;
    queueLayout_->removeWidget(card->card); card->card->deleteLater(); delete card;
    emptyQueue_->setVisible(cards_.isEmpty()); updateQueueStatus();
}

void MainWindow::applyTaskMetadata(const QString& id, const VideoMetadata& metadata) {
    auto* card = cards_.value(id); const auto* model = downloads_->task(id); if (!card || !model) return;
    downloads_->setTitle(id, metadata.title);
    card->metadataLabel->setText(QString("%1 • %2 • %3:%4").arg(model->formatLabel, metadata.uploader)
        .arg(metadata.durationSeconds / 60).arg(metadata.durationSeconds % 60, 2, 10, QLatin1Char('0')));
    loadThumbnail(metadata.thumbnailUrl, card->thumbnail);
}

void MainWindow::loadThumbnail(const QString& url, QLabel* target) {
    if (url.isEmpty() || !target || !validUrl(url)) return;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, "VideoDownloaderPro/" VDP_VERSION);
    request.setTransferTimeout(15000);
    target->setProperty("thumbnailUrl", url);
    auto* reply = thumbnailNetwork_->get(request);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > 5 * 1024 * 1024) reply->abort();
    });
    const QPointer<QLabel> safeTarget(target);
    connect(reply, &QNetworkReply::finished, this, [reply, safeTarget, bytes, url] {
        if (safeTarget && safeTarget->property("thumbnailUrl").toString() == url && reply->error() == QNetworkReply::NoError) {
            QPixmap image;
            if (image.loadFromData(*bytes)) {
                const QSize size = safeTarget->size();
                const QPixmap scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                const int x = qMax(0, (scaled.width() - size.width()) / 2);
                const int y = qMax(0, (scaled.height() - size.height()) / 2);
                safeTarget->setPixmap(scaled.copy(x, y, size.width(), size.height()));
                safeTarget->setText({});
            }
        }
        reply->deleteLater();
    });
}

void MainWindow::addDownload() {
    const QString url = urlEdit_->text().trimmed();
    if (!validUrl(url)) { showMessage("Введите корректную ссылку.", true); return; }
    if (updater_->isBusy() || runtime_->isBusy()) { showMessage("Дождитесь завершения подготовки runtime."); return; }
    if (!toolStatus_.ready()) { navigation_->setCurrentRow(2); showMessage("Runtime не готов. Нажмите «Восстановить runtime».", true); return; }
    saveSettings();
    auto preset = formatPreset(formatCombo_->currentData().toString());
    const int advanced = advancedCombo_->currentData().toInt();
    if (advancedCombo_->isVisible() && checkedUrl_ == url && advanced >= 0 && advanced < checkedMetadata_.formats.size())
        preset = checkedMetadata_.formats[advanced].preset;
    downloads_->enqueue({url, checkedUrl_ == url ? checkedMetadata_.title : url, outputEdit_->text(), preset});
    urlEdit_->clear(); downloads_->start();
}

void MainWindow::updateQueueStatus() {
    queueStatus_->setText(QString("Активных: %1 • В очереди: %2").arg(downloads_->runningCount()).arg(downloads_->queuedCount()));
}

void MainWindow::loadHistory(const QString& query) {
    if (!historyList_) return;
    historyList_->clear();
    for (const auto& record : history_.newestFirst(query)) {
        const auto& task = record.task;
        const auto date = record.completedAt.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(3 * 3600));
        auto* row = new QListWidgetItem(QString("%1 • %2 • %3 • %4 MSK")
            .arg(task.title, task.formatLabel, taskStateLabel(task.state), date.toString("dd.MM.yyyy HH:mm")));
        row->setToolTip(redactSecrets(task.url + "\n" + task.errorMessage));
        row->setData(Qt::UserRole, task.outputPath); row->setData(Qt::UserRole + 1, task.id);
        historyList_->addItem(row);
    }
}

void MainWindow::chooseOutputDirectory() {
    const QString path = QFileDialog::getExistingDirectory(this, "Папка загрузок", outputEdit_->text());
    if (!path.isEmpty()) { outputEdit_->setText(path); saveSettings(); }
}

void MainWindow::showMessage(const QString& text, bool error) {
    statusBar()->showMessage(redactSecrets(text), 8000);
    if (error) { lastError_ = redactSecrets(text); appendBoundedLog(paths_.logsDir, "app", lastError_); }
    if (error && toolLog_) toolLog_->append(lastError_);
}

void MainWindow::openPath(const QString& path) {
    if (path.isEmpty()) return;
    const QFileInfo info(path);
    const QString target = info.exists() && info.isFile() ? info.absoluteFilePath() :
                           (info.exists() ? info.absoluteFilePath() : info.absolutePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (updater_->isBusy() || runtime_->isBusy()) {
        showMessage("Дождитесь завершения операции runtime или отмените загрузку обновления.");
        event->ignore(); return;
    }
    downloads_->shutdown(); saveSettings(); event->accept();
}

} // namespace vdp
