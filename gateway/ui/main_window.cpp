#include "main_window.hpp"

#include "pairing.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <exception>
#include <string>

namespace orca::ui {

namespace {

const char* dotColor(VerdictLevel level) {
    switch (level) {
        case VerdictLevel::kOk: return "#3f6b52";
        case VerdictLevel::kWarn: return "#b07d24";
        case VerdictLevel::kError: return "#8f2f22";
    }
    return "#8f2f22";
}

const char* chipColor(ChipLevel level) {
    switch (level) {
        case ChipLevel::kOn: return "#3f6b52";
        case ChipLevel::kWarn: return "#b07d24";
        case ChipLevel::kOff: return "#8f2f22";
    }
    return "#8f2f22";
}

QLabel* makeDot(QWidget* parent, bool visible) {
    auto* dot = new QLabel(parent);
    dot->setFixedSize(8, 8);
    dot->setVisible(visible);
    dot->setStyleSheet("QLabel { background: #a89e8e; border-radius: 4px; }");
    return dot;
}

QLabel* makeRowLabel(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("rowLabel");
    label->setMinimumWidth(84);
    return label;
}

QLabel* makeValue(QWidget* parent, const QString& text = QString()) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

}  // namespace

MainWindow::MainWindow(std::filesystem::path configPath, QWidget* parent)
    : MainWindow(configPath, nullptr, parent) {}

MainWindow::MainWindow(std::filesystem::path configPath, std::unique_ptr<StatusProvider> provider,
                       QWidget* parent)
    : QMainWindow(parent),
      store_(configPath),
      provider_(provider ? std::move(provider)
                          : std::make_unique<LocalStatusProvider>(configPath)) {
    buildUi();
    refresh();
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::refresh);
    timer_->start(3000);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    setWindowTitle(QString::fromUtf8("orca 网关管理"));
    resize(700, 820);
    setStyleSheet(QString::fromUtf8(R"qss(
        QWidget#central   { background: #faf7f1; }
        QLabel            { color: #1c1a17; font: 14px 'Microsoft YaHei', 'Segoe UI', sans-serif; }
        QLabel#kicker     { color: #a89e8e; font: 10px sans-serif; letter-spacing: 2px; }
        QLabel#title      { font: 28px Georgia, 'Songti SC', serif; }
        QLabel#dek        { color: #857c6e; font-size: 13px; }
        QLabel#hint       { color: #a89e8e; font-size: 12px; }
        QLabel#rowLabel   { color: #857c6e; font-size: 13px; }
        QLabel#fieldLabel { color: #a89e8e; font: 10px sans-serif; letter-spacing: 1px; }
        QLabel#formNote   { color: #857c6e; font-size: 12px; }
        QLabel#chip       { color: #857c6e; font: 10px sans-serif; padding: 3px 8px;
                            border: 1px solid #cfc5b4; }
        QFrame#card       { background: #fffdf9; border: 1px solid #cfc5b4; }
        QLineEdit, QPlainTextEdit {
            border: 0; border-bottom: 1px solid #cfc5b4; background: transparent;
            padding: 6px 2px; font-size: 13px; color: #1c1a17;
        }
        QPlainTextEdit#configView { border: 1px solid #e2dacd; border-left: 2px solid #cfc5b4;
            font: 11px Consolas, monospace; background: rgba(28,26,23,0.03); }
        QPushButton { background: #1c1a17; color: #faf7f1; border: 0; padding: 9px 16px;
                      font: 11px 'Microsoft YaHei', sans-serif; }
        QPushButton:hover { background: #3a352e; }
        QPushButton#ghost { background: transparent; color: #857c6e;
                            border: 1px solid #e2dacd; padding: 5px 9px; font-size: 11px; }
    )qss"));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* central = new QWidget(scroll);
    central->setObjectName("central");
    scroll->setWidget(central);
    setCentralWidget(scroll);

    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(28, 26, 28, 32);
    outer->setSpacing(14);

    auto* kicker = new QLabel(QString::fromUtf8("orca-web · local client"), central);
    kicker->setObjectName("kicker");
    auto* title = new QLabel(QString::fromUtf8("网关管理"), central);
    title->setObjectName("title");
    auto* dek = new QLabel(QString::fromUtf8("这台电脑上的 gateway 如何连 hub、如何连桌面 Orca —— 都在这里改，不需要再动文件。"), central);
    dek->setObjectName("dek");
    dek->setWordWrap(true);
    outer->addWidget(kicker);
    outer->addWidget(title);
    outer->addWidget(dek);

    // Verdict card -----------------------------------------------------------
    auto* verdictCard = new QFrame(central);
    verdictCard->setObjectName("card");
    auto* verdictLayout = new QHBoxLayout(verdictCard);
    verdictDot_ = makeDot(verdictCard, true);
    verdictText_ = makeValue(verdictCard, QString::fromUtf8("读状态中…"));
    verdictLayout->addWidget(verdictDot_);
    verdictLayout->addWidget(verdictText_, 1);
    outer->addWidget(verdictCard);

    // Hub card ---------------------------------------------------------------
    QWidget* hubCard = makeCard(QString::fromUtf8("hub 连接"),
        QString::fromUtf8("gateway ↔ 远端服务器：本机 gateway 主动拨向 VPS，手机网页的一切经它转发"),
        &hubChip_);
    auto* hubBox = qobject_cast<QVBoxLayout*>(hubCard->layout());

    auto* hubStateRow = new QHBoxLayout();
    hubStateRow->addWidget(makeRowLabel(hubCard, QString::fromUtf8("状态")));
    hubDot_ = makeDot(hubCard, true);
    hubStateRow->addWidget(hubDot_);
    hubValue_ = makeValue(hubCard, QString::fromUtf8("读状态中…"));
    hubStateRow->addWidget(hubValue_, 1);
    hubBox->addLayout(hubStateRow);

    auto* hubUrlRow = new QHBoxLayout();
    hubUrlRow->addWidget(makeRowLabel(hubCard, QString::fromUtf8("服务器")));
    hubUrl_ = makeValue(hubCard, QString::fromUtf8("—"));
    hubUrl_->setStyleSheet("QLabel { font: 12px Consolas, monospace; }");
    hubUrlRow->addWidget(hubUrl_, 1);
    hubBox->addLayout(hubUrlRow);

    hubForm_ = new QFrame(hubCard);
    auto* hubFormBox = new QVBoxLayout(hubForm_);
    hubFormBox->setContentsMargins(0, 6, 0, 0);
    auto* hubFormNote = new QLabel(QString::fromUtf8("换服务器或换 key。地址 + key 两样都填才会全部替换；只填一个就只改一个。保存后立刻重拨。"), hubForm_);
    hubFormNote->setObjectName("formNote");
    hubFormNote->setWordWrap(true);
    hubFormBox->addWidget(hubFormNote);
    serverUrlEdit_ = new QLineEdit(hubForm_);
    serverUrlEdit_->setPlaceholderText(QString::fromUtf8("wss://服务器域名/gateway"));
    gatewayKeyEdit_ = new QLineEdit(hubForm_);
    gatewayKeyEdit_->setPlaceholderText(QString::fromUtf8("gateway key（新 key）"));
    gatewayKeyEdit_->setEchoMode(QLineEdit::Password);
    hubFormBox->addWidget(serverUrlEdit_);
    hubFormBox->addWidget(gatewayKeyEdit_);
    auto* hubActions = new QHBoxLayout();
    auto* saveHub = new QPushButton(QString::fromUtf8("保存并重连"), hubForm_);
    auto* cancelHub = new QPushButton(QString::fromUtf8("取消"), hubForm_);
    cancelHub->setObjectName("ghost");
    hubActions->addWidget(saveHub);
    hubActions->addWidget(cancelHub);
    hubActions->addStretch(1);
    hubFormBox->addLayout(hubActions);
    hubNote_ = makeValue(hubForm_);
    hubFormBox->addWidget(hubNote_);
    hubBox->addWidget(hubForm_);
    hubForm_->hide();

    hubEditButton_ = new QPushButton(QString::fromUtf8("修改地址 / key…"), hubCard);
    hubEditButton_->setObjectName("ghost");
    hubBox->addWidget(hubEditButton_, 0, Qt::AlignLeft);
    connect(hubEditButton_, &QPushButton::clicked, this, &MainWindow::onToggleHubForm);
    connect(cancelHub, &QPushButton::clicked, this, &MainWindow::onToggleHubForm);
    connect(saveHub, &QPushButton::clicked, this, &MainWindow::onSaveHub);
    outer->addWidget(hubCard);

    // Orca pairing card ------------------------------------------------------
    QWidget* orcaCard = makeCard(QString::fromUtf8("桌面 Orca 配对"),
        QString::fromUtf8("gateway ↔ 这台电脑的 Orca：经本机回环口与桌面 Orca 通信，不出这台机器"),
        &orcaChip_);
    auto* orcaBox = qobject_cast<QVBoxLayout*>(orcaCard->layout());

    auto* pairStateRow = new QHBoxLayout();
    pairStateRow->addWidget(makeRowLabel(orcaCard, QString::fromUtf8("配对状态")));
    pairDot_ = makeDot(orcaCard, true);
    pairStateRow->addWidget(pairDot_);
    pairState_ = makeValue(orcaCard, QString::fromUtf8("读状态中…"));
    pairStateRow->addWidget(pairState_, 1);
    orcaBox->addLayout(pairStateRow);

    auto* orcaValueRow = new QHBoxLayout();
    orcaValueRow->addWidget(makeRowLabel(orcaCard, QString::fromUtf8("Orca 连接")));
    orcaValue_ = makeValue(orcaCard, QString::fromUtf8("读状态中…"));
    orcaValueRow->addWidget(orcaValue_, 1);
    orcaBox->addLayout(orcaValueRow);

    pairForm_ = new QFrame(orcaCard);
    auto* pairFormBox = new QVBoxLayout(pairForm_);
    pairFormBox->setContentsMargins(0, 6, 0, 0);
    auto* pairFormNote = new QLabel(QString::fromUtf8("粘贴新配对码 = 替换现有配对。旧设备可在桌面 Orca 的设置里撤销；正常在用就不需要重新配对。"), pairForm_);
    pairFormNote->setObjectName("formNote");
    pairFormNote->setWordWrap(true);
    pairFormBox->addWidget(pairFormNote);
    pairingEdit_ = new QPlainTextEdit(pairForm_);
    pairingEdit_->setPlaceholderText(QString::fromUtf8("orca://pair?code=……（桌面 Orca 设置 → 手机配对 生成，整段粘贴）"));
    pairingEdit_->setFixedHeight(84);
    pairFormBox->addWidget(pairingEdit_);
    auto* pairActions = new QHBoxLayout();
    auto* applyPairing = new QPushButton(QString::fromUtf8("完成配对"), pairForm_);
    auto* cancelPairing = new QPushButton(QString::fromUtf8("取消"), pairForm_);
    cancelPairing->setObjectName("ghost");
    pairActions->addWidget(applyPairing);
    pairActions->addWidget(cancelPairing);
    pairActions->addStretch(1);
    pairFormBox->addLayout(pairActions);
    pairNote_ = makeValue(pairForm_);
    pairFormBox->addWidget(pairNote_);
    orcaBox->addWidget(pairForm_);
    pairForm_->hide();

    pairEditButton_ = new QPushButton(QString::fromUtf8("重新配对…"), orcaCard);
    pairEditButton_->setObjectName("ghost");
    orcaBox->addWidget(pairEditButton_, 0, Qt::AlignLeft);
    connect(pairEditButton_, &QPushButton::clicked, this, &MainWindow::onTogglePairingForm);
    connect(cancelPairing, &QPushButton::clicked, this, &MainWindow::onTogglePairingForm);
    connect(applyPairing, &QPushButton::clicked, this, &MainWindow::onApplyPairing);
    outer->addWidget(orcaCard);

    // About card -------------------------------------------------------------
    QLabel* aboutChip = nullptr;
    QWidget* aboutCard = makeCard(QString::fromUtf8("本机信息"), QString(), &aboutChip);
    auto* aboutBox = qobject_cast<QVBoxLayout*>(aboutCard->layout());
    aboutChip->hide();

    auto addAboutRow = [&](const QString& labelText, QLabel** valueOut) {
        auto* row = new QHBoxLayout();
        row->addWidget(makeRowLabel(aboutCard, labelText));
        *valueOut = makeValue(aboutCard);
        row->addWidget(*valueOut, 1);
        aboutBox->addLayout(row);
    };
    addAboutRow(QString::fromUtf8("网关 id"), &gatewayId_);
    addAboutRow(QString::fromUtf8("网关版本"), &versionLabel_);
    auto* configRow = new QHBoxLayout();
    configRow->addWidget(makeRowLabel(aboutCard, QString::fromUtf8("配置文件")));
    configPathLabel_ = makeValue(aboutCard);
    configPathLabel_->setStyleSheet("QLabel { font: 11px Consolas, monospace; }");
    configRow->addWidget(configPathLabel_, 1);
    configToggle_ = new QPushButton(QString::fromUtf8("查看配置"), aboutCard);
    configToggle_->setObjectName("ghost");
    configRow->addWidget(configToggle_);
    aboutBox->addLayout(configRow);
    configView_ = new QPlainTextEdit(aboutCard);
    configView_->setObjectName("configView");
    configView_->setReadOnly(true);
    configView_->setFixedHeight(220);
    configView_->hide();
    aboutBox->addWidget(configView_);
    configNote_ = makeValue(aboutCard);
    aboutBox->addWidget(configNote_);
    connect(configToggle_, &QPushButton::clicked, this, &MainWindow::onToggleConfigFile);
    outer->addWidget(aboutCard);

    auto* hint = new QLabel(QString::fromUtf8(
        "状态每 3 秒刷新一次。配置与 key 只保存在这台电脑，不经过 VPS。\n"
        "本窗口是 gateway 本体：关闭窗口即停止网关，开机自启由启动项拉起。"), central);
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    outer->addWidget(hint);
    outer->addStretch(1);
}

QWidget* MainWindow::makeCard(const QString& title, const QString& tip, QLabel** chipOut) {
    auto* card = new QFrame();
    card->setObjectName("card");
    auto* box = new QVBoxLayout(card);
    box->setContentsMargins(16, 12, 16, 14);
    box->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* label = new QLabel(title, card);
    label->setObjectName("fieldLabel");
    if (!tip.isEmpty()) label->setToolTip(tip);
    head->addWidget(label);
    head->addStretch(1);
    auto* chip = new QLabel(QString::fromUtf8("读状态中…"), card);
    chip->setObjectName("chip");
    head->addWidget(chip);
    box->addLayout(head);
    *chipOut = chip;
    return card;
}

void MainWindow::setDot(QLabel* label, const char* color) {
    label->setStyleSheet(QString("QLabel { background: %1; border-radius: 4px; }")
                             .arg(QString::fromLatin1(color)));
    label->setVisible(true);
}

void MainWindow::setChip(QLabel* label, const QString& text, ChipLevel level) {
    const QString color = QString::fromLatin1(chipColor(level));
    label->setText(text);
    label->setStyleSheet(QString("QLabel { color: %1; border: 1px solid %1; "
                                 "background: transparent; padding: 3px 8px; "
                                 "font: 10px sans-serif; }").arg(color));
}

void MainWindow::setNote(QLabel* label, const QString& text, bool bad) {
    label->setText(text);
    label->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }")
                             .arg(bad ? QStringLiteral("#8f2f22") : QStringLiteral("#3f6b52")));
}

void MainWindow::render(const AdminStatus& status) {
    const Verdict verdict = evaluate(status);
    setDot(verdictDot_, dotColor(verdict.level));
    verdictText_->setText(QString::fromUtf8(verdict.text.c_str()));
    verdictText_->setStyleSheet(QString("QLabel { color: %1; font-weight: 500; }")
                                    .arg(QString::fromLatin1(dotColor(verdict.level))));

    setChip(hubChip_, QString::fromUtf8(status.hubConnected ? "已连接" : "重试中…"),
            status.hubConnected ? ChipLevel::kOn : ChipLevel::kOff);
    setDot(hubDot_, status.hubConnected ? "#3f6b52" : "#8f2f22");
    hubValue_->setText(QString::fromUtf8(status.hubConnected
        ? "正常，和服务器保持在线" : "在后台自动重拨，隔几秒自动变"));
    hubUrl_->setText(status.hubUrl.empty()
        ? QString::fromUtf8("—")
        : QString::fromStdString(status.hubUrl));

    const PairingState pairing = pairingState(status);
    setChip(orcaChip_, QString::fromUtf8(pairing.chip.c_str()), pairing.chipLevel);
    setDot(pairDot_, chipColor(pairing.chipLevel));
    pairState_->setText(QString::fromUtf8(pairing.note.c_str()));
    const std::string identity = orcaIdentity(status);
    orcaValue_->setText(identity.empty()
        ? QString::fromUtf8("还没有读到 Orca 的信息")
        : QString::fromStdString(identity));

    gatewayId_->setText(QString::fromStdString(status.gatewayId));
    versionLabel_->setText(QString::fromStdString(status.version));
    configPathLabel_->setText(QString::fromStdString(status.configPath));
    configNote_->setText(status.configNote.empty()
        ? QString()
        : QString::fromStdString(status.configNote));
    configNote_->setStyleSheet(QStringLiteral("QLabel { color: #b07d24; font-size: 12px; }"));
}

void MainWindow::refresh() {
    render(provider_->snapshot());
}

void MainWindow::onToggleHubForm() {
    const bool show = !hubForm_->isVisible();
    hubForm_->setVisible(show);
    hubEditButton_->setVisible(!show);
    if (!show) setNote(hubNote_, QString(), false);
}

void MainWindow::onSaveHub() {
    ConfigUpdate update;
    update.serverUrl = serverUrlEdit_->text().trimmed().toStdString();
    update.gatewayKey = gatewayKeyEdit_->text().toStdString();
    if (update.empty()) {
        setNote(hubNote_, QString::fromUtf8("两个都没填。"), true);
        return;
    }
    try {
        const std::string message = store_.apply(update);
        setNote(hubNote_, QString::fromStdString(message), false);
        gatewayKeyEdit_->clear();
        hubForm_->hide();
        hubEditButton_->show();
        refresh();
    } catch (const std::exception& error) {
        setNote(hubNote_, QString::fromUtf8(error.what()), true);
    }
}

void MainWindow::onTogglePairingForm() {
    const bool show = !pairForm_->isVisible();
    pairForm_->setVisible(show);
    pairEditButton_->setVisible(!show);
    if (!show) setNote(pairNote_, QString(), false);
}

void MainWindow::onApplyPairing() {
    const std::string code = pairingEdit_->toPlainText().toStdString();
    if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
        setNote(pairNote_, QString::fromUtf8("配对码为空。"), true);
        return;
    }
    try {
        const PairingOffer offer = decodePairingCode(code);
        ConfigUpdate update;
        update.orca = OrcaPairing{loopbackEndpoint(offer.endpoint),
                                  offer.deviceToken,
                                  offer.publicKeyB64};
        const std::string message = store_.apply(update);
        setNote(pairNote_, QString::fromStdString(message), false);
        pairingEdit_->clear();
        pairForm_->hide();
        pairEditButton_->show();
        refresh();
    } catch (const std::exception& error) {
        setNote(pairNote_, QString::fromUtf8(error.what()), true);
    }
}

void MainWindow::onToggleConfigFile() {
    if (configView_->isVisible()) {
        configView_->hide();
        configToggle_->setText(QString::fromUtf8("查看配置"));
        return;
    }
    const ConfigSnapshot snapshot = store_.read();
    QString text;
    if (!snapshot.error.empty()) {
        text = QString::fromUtf8("读失败了：") + QString::fromStdString(snapshot.error);
    } else if (!snapshot.exists) {
        text = QString::fromUtf8("配置文件还不存在；本页第一次保存时会创建。");
    } else {
        text = QString::fromUtf8("// ") + QString::fromStdString(snapshot.configPath) +
               QStringLiteral("\n") + QString::fromStdString(snapshot.raw);
    }
    configView_->setPlainText(text);
    configView_->show();
    configToggle_->setText(QString::fromUtf8("收起配置"));
}

}  // namespace orca::ui
