#include "main_window.hpp"

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
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), paths_(AppPaths::defaults()), toolchain_(paths_),
      settings_(paths_.settingsFile, QSettings::IniFormat) {
    setWindowTitle(QString("%1 %2").arg(kAppTitle, VDP_VERSION));
    setMinimumSize(1100, 720);
    resize(1280, 820);
    setWindowIcon(QIcon(":/icon.ico"));
    setupUi();
    setupMenu();
    loadSettings();
    loadHistory();
    QTimer::singleShot(0, this, [this] {
        toolStatus_ = toolchain_.ensureRuntime();
        refreshToolStatus(true);
    });
}

MainWindow::~MainWindow() {
    qDeleteAll(tasks_);
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
    auto* brandEdition = new QLabel("PRO  •  VERSION 4.0");
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
    urlEdit_->setMinimumHeight(46);
    urlEdit_->setPlaceholderText("https://www.youtube.com/watch?v=...");
    auto* paste = createButton("Вставить");
    auto* check = createButton("Проверить");
    auto* add = createButton("Добавить в очередь", "PrimaryButton");
    formatCombo_ = new QComboBox;
    formatCombo_->setObjectName("Input");
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

    auto* preview = new QFrame;
    preview->setObjectName("PreviewCard");
    auto* previewLayout = new QHBoxLayout(preview);
    previewLayout->setContentsMargins(14, 14, 16, 14);
    previewLayout->setSpacing(16);
    auto* artwork = new QLabel("PREVIEW\nVIDEO");
    artwork->setObjectName("PreviewArtwork");
    artwork->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    artwork->setFixedSize(168, 92);
    artwork->setMargin(14);
    auto* previewText = new QVBoxLayout;
    previewText->setSpacing(5);
    previewTitle_ = heading("Ссылка ещё не проверена", "SectionTitle");
    previewDetails_ = createMutedLabel("Нажмите «Проверить», чтобы получить название, автора и длительность.");
    previewText->addWidget(previewTitle_);
    previewText->addWidget(previewDetails_);
    previewText->addStretch();
    previewLayout->addWidget(artwork);
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
        for (auto* task : tasks_) if (task->completed && task->card) task->card->hide();
    });
    connect(cancelAll, &QPushButton::clicked, this, [this] {
        const auto allTasks = tasks_.values();
        for (auto* task : allTasks) if (!task->completed) cancelTask(task);
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
    auto* buttons = new QHBoxLayout;
    auto* updateYtdlp = createButton("Обновить yt-dlp", "PrimaryButton");
    auto* updateDeno = createButton("Обновить Deno");
    auto* updateFfmpeg = createButton("Обновить ffmpeg");
    auto* repair = createButton("Восстановить runtime", "DangerButton");
    buttons->addWidget(updateYtdlp);
    buttons->addWidget(updateDeno);
    buttons->addWidget(updateFfmpeg);
    buttons->addWidget(repair);
    layout->addLayout(buttons);
    root->addWidget(card);
    toolLog_ = new QTextEdit;
    toolLog_->setReadOnly(true);
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
    connect(parallelSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { saveSettings(); startNextDownloads(); });
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
    aboutLayout->addWidget(heading(QString("Версия %1  •  C++20 / Qt 6").arg(VDP_VERSION), "SectionTitle"));
    auto* description = createMutedLabel("Быстрое нативное приложение для загрузки видео и аудио через yt-dlp.\nPython не требуется — весь интерфейс и управление процессами написаны на C++.");
    description->setObjectName("AboutText");
    aboutLayout->addWidget(description);
    auto* repo = createButton("Открыть GitHub", "PrimaryButton");
    connect(repo, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(VDP_REPOSITORY)); });
    aboutLayout->addSpacing(10);
    aboutLayout->addWidget(repo, 0, Qt::AlignLeft);
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
    file->addAction("Выход", this, &QWidget::close, QKeySequence("Ctrl+Q"));
    auto* downloads = menuBar()->addMenu("Загрузки");
    downloads->addAction("Добавить в очередь", this, &MainWindow::addDownload, QKeySequence("Ctrl+D"));
    downloads->addAction("Проверить ссылку", this, &MainWindow::checkUrl, QKeySequence("Ctrl+I"));
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
    toolStatus_ = toolchain_.status(versions);
    auto describe = [](const ToolInfo& tool) {
        return QString("%1: %2%3\n%4").arg(tool.name, tool.exists ? "найден" : "не найден",
            tool.version.isEmpty() ? "" : " • " + tool.version, tool.path);
    };
    ytdlpStatus_->setText(describe(toolStatus_.ytdlp));
    denoStatus_->setText(describe(toolStatus_.deno));
    ffmpegStatus_->setText(describe(toolStatus_.ffmpeg));
    ytdlpChip_->setText(QString("yt-dlp: %1").arg(toolStatus_.ytdlp.exists ? "готов" : "нет"));
    ffmpegChip_->setText(QString("runtime: %1").arg(toolStatus_.ready() ? "готов" : "требует внимания"));
    if (!toolStatus_.warning.isEmpty()) toolLog_->append(toolStatus_.warning);
}

void MainWindow::runToolAction(const QString& action) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    bool ok = false;
    if (action == "ytdlp") ok = toolchain_.updateYtdlp(&error);
    else if (action == "deno") ok = toolchain_.updateDeno(&error);
    else if (action == "ffmpeg") ok = toolchain_.updateFfmpeg(&error);
    else { toolStatus_ = toolchain_.repairRuntime(); ok = toolStatus_.ready(); }
    QApplication::restoreOverrideCursor();
    toolLog_->append(ok ? "Операция завершена успешно." : "Ошибка: " + error);
    refreshToolStatus(true);
    showMessage(ok ? "Runtime обновлён." : sanitizeError(error), !ok);
}

void MainWindow::checkUrl() {
    const QString url = urlEdit_->text().trimmed();
    if (!validUrl(url)) { showMessage("Введите корректную ссылку.", true); return; }
    if (!QFileInfo::exists(toolchain_.ytdlpPath())) { navigation_->setCurrentRow(2); showMessage("yt-dlp не найден.", true); return; }
    if (metadataProcess_) { metadataProcess_->kill(); metadataProcess_->deleteLater(); }
    previewTitle_->setText("Получение информации...");
    previewDetails_->setText(url);
    metadataProcess_ = new QProcess(this);
    connect(metadataProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        const QByteArray output = metadataProcess_->readAllStandardOutput();
        const QString errors = QString::fromUtf8(metadataProcess_->readAllStandardError());
        if (exitCode == 0) {
            const auto json = QJsonDocument::fromJson(output).object();
            previewTitle_->setText(json.value("title").toString("Без названия"));
            const int seconds = json.value("duration").toInt();
            previewDetails_->setText(QString("%1 • %2:%3")
                .arg(json.value("uploader").toString("Автор неизвестен"))
                .arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
        } else {
            previewTitle_->setText("Не удалось проверить ссылку");
            previewDetails_->setText(sanitizeError(errors));
        }
        metadataProcess_->deleteLater();
        metadataProcess_ = nullptr;
    });
    metadataProcess_->start(toolchain_.ytdlpPath(), buildMetadataArguments(url, toolchain_.denoPath()));
}

MainWindow::Task* MainWindow::createTask(const QString& url, const FormatPreset& preset) {
    auto* task = new Task;
    task->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task->url = url;
    task->preset = preset;
    task->title = previewTitle_->text() == "Ссылка ещё не проверена" ? url : previewTitle_->text();
    task->card = new QFrame;
    task->card->setObjectName("Card");
    auto* layout = new QVBoxLayout(task->card);
    auto* top = new QHBoxLayout;
    task->titleLabel = heading(task->title, "SectionTitle");
    task->cancelButton = createButton("Отменить", "DangerButton");
    top->addWidget(task->titleLabel, 1);
    top->addWidget(task->cancelButton);
    task->statusLabel = createMutedLabel(QString("В очереди • %1").arg(preset.label));
    task->progress = new QProgressBar;
    task->progress->setRange(0, 1000);
    task->progress->setValue(0);
    task->speedLabel = createMutedLabel({});
    layout->addLayout(top);
    layout->addWidget(task->statusLabel);
    layout->addWidget(task->progress);
    layout->addWidget(task->speedLabel);
    connect(task->cancelButton, &QPushButton::clicked, this, [this, task] { cancelTask(task); });
    queueLayout_->addWidget(task->card);
    emptyQueue_->hide();
    tasks_.insert(task->id, task);
    return task;
}

void MainWindow::addDownload() {
    const QString url = urlEdit_->text().trimmed();
    if (!validUrl(url)) { showMessage("Введите корректную ссылку.", true); return; }
    refreshToolStatus(false);
    if (!toolStatus_.ready()) { navigation_->setCurrentRow(2); showMessage("Runtime не готов. Нажмите «Восстановить runtime».", true); return; }
    saveSettings();
    auto* task = createTask(url, formatPreset(formatCombo_->currentData().toString()));
    pending_.append(task->id);
    urlEdit_->clear();
    updateQueueStatus();
    startNextDownloads();
}

void MainWindow::startNextDownloads() {
    while (runningCount_ < parallelSpin_->value() && !pending_.isEmpty()) {
        const QString id = pending_.takeFirst();
        if (auto* task = tasks_.value(id); task && !task->completed) startTask(task);
    }
    updateQueueStatus();
}

void MainWindow::startTask(Task* task) {
    QDir().mkpath(outputEdit_->text());
    task->running = true;
    ++runningCount_;
    task->statusLabel->setText("Загрузка...");
    task->process = new QProcess(this);
    connect(task->process, &QProcess::readyReadStandardOutput, this, [this, task] { consumeTaskOutput(task); });
    connect(task->process, &QProcess::readyReadStandardError, this, [task] {
        task->stderrText += QString::fromUtf8(task->process->readAllStandardError());
    });
    connect(task->process, &QProcess::finished, this, [this, task](int exitCode, QProcess::ExitStatus) { finishTask(task, exitCode); });
    task->process->start(toolchain_.ytdlpPath(), buildDownloadArguments(task->url, task->preset,
        outputEdit_->text(), toolchain_.ffmpegDirectory(), toolchain_.denoPath()));
    updateQueueStatus();
}

void MainWindow::consumeTaskOutput(Task* task, bool flush) {
    task->stdoutBuffer += task->process->readAllStandardOutput();
    while (true) {
        const qsizetype newline = task->stdoutBuffer.indexOf('\n');
        if (newline < 0 && !flush) break;
        QByteArray raw = newline < 0 ? task->stdoutBuffer : task->stdoutBuffer.left(newline);
        task->stdoutBuffer = newline < 0 ? QByteArray{} : task->stdoutBuffer.mid(newline + 1);
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith("vdppath:")) task->outputPath = line.mid(8).trimmed();
        if (line.startsWith("download:")) {
            const auto parts = line.mid(9).split('|');
            bool ok = false;
            const double percent = parts.value(0).remove('%').trimmed().toDouble(&ok);
            if (ok) task->progress->setValue(qBound(0, qRound(percent * 10), 1000));
            task->speedLabel->setText(QString("Скорость: %1 • Осталось: %2")
                .arg(parts.value(1).trimmed(), parts.value(2).trimmed()));
        }
        if (newline < 0) break;
    }
}

void MainWindow::finishTask(Task* task, int exitCode) {
    consumeTaskOutput(task, true);
    task->running = false;
    task->completed = true;
    runningCount_ = qMax(0, runningCount_ - 1);
    if (exitCode == 0) {
        task->progress->setValue(1000);
        task->statusLabel->setText("Загрузка завершена");
        task->cancelButton->setText("Открыть");
        disconnect(task->cancelButton, nullptr, this, nullptr);
        connect(task->cancelButton, &QPushButton::clicked, this, [this, task] { openPath(task->outputPath); });
        addHistory(task, "completed");
        if (autoOpenCheck_->isChecked()) openPath(task->outputPath);
    } else {
        const QString error = sanitizeError(task->stderrText);
        task->statusLabel->setText(error);
        task->cancelButton->setText("Удалить карточку");
        addHistory(task, "failed", error);
    }
    task->process->deleteLater();
    task->process = nullptr;
    updateQueueStatus();
    startNextDownloads();
}

void MainWindow::cancelTask(Task* task) {
    if (task->completed) { task->card->hide(); return; }
    pending_.removeAll(task->id);
    if (task->process) task->process->kill();
    else {
        task->completed = true;
        task->statusLabel->setText("Отменено");
        addHistory(task, "cancelled", "Отменено пользователем");
    }
    updateQueueStatus();
}

void MainWindow::updateQueueStatus() {
    queueStatus_->setText(QString("Активных: %1 • В очереди: %2").arg(runningCount_).arg(pending_.size()));
}

void MainWindow::addHistory(Task* task, const QString& status, const QString& error) {
    QJsonArray array;
    QFile input(paths_.historyFile);
    if (input.open(QIODevice::ReadOnly)) array = QJsonDocument::fromJson(input.readAll()).array();
    array.prepend(QJsonObject{{"id", task->id}, {"title", task->title}, {"url", task->url},
        {"path", task->outputPath}, {"format", task->preset.label}, {"status", status}, {"error", error},
        {"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    while (array.size() > 500) array.removeLast();
    QSaveFile output(paths_.historyFile);
    if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(array).toJson()); output.commit(); }
    loadHistory(historySearch_->text());
}

void MainWindow::loadHistory(const QString& query) {
    if (!historyList_) return;
    historyList_->clear();
    QFile input(paths_.historyFile);
    if (!input.open(QIODevice::ReadOnly)) return;
    for (const auto& value : QJsonDocument::fromJson(input.readAll()).array()) {
        const auto item = value.toObject();
        const QString haystack = item.value("title").toString() + " " + item.value("url").toString();
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        auto* row = new QListWidgetItem(QString("%1  •  %2  •  %3")
            .arg(item.value("title").toString(), item.value("format").toString(), item.value("status").toString()));
        row->setToolTip(item.value("url").toString());
        row->setData(Qt::UserRole, item.value("path").toString());
        historyList_->addItem(row);
    }
}

void MainWindow::chooseOutputDirectory() {
    const QString path = QFileDialog::getExistingDirectory(this, "Папка загрузок", outputEdit_->text());
    if (!path.isEmpty()) { outputEdit_->setText(path); saveSettings(); }
}

void MainWindow::showMessage(const QString& text, bool error) {
    statusBar()->showMessage(text, 8000);
    if (error) QMessageBox::warning(this, "Video Downloader Pro", text);
}

void MainWindow::openPath(const QString& path) {
    if (path.isEmpty()) return;
    const QFileInfo info(path);
    const QString target = info.exists() && info.isFile() ? info.absoluteFilePath() :
                           (info.exists() ? info.absoluteFilePath() : info.absolutePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (runningCount_ > 0 && QMessageBox::question(this, "Выход", "Есть активные загрузки. Отменить и выйти?") != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    for (auto* task : tasks_) if (task->process) task->process->kill();
    saveSettings();
    event->accept();
}

} // namespace vdp
