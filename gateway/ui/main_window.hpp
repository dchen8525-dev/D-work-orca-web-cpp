#pragma once

#include "config_store.hpp"
#include "status_model.hpp"

#include <QMainWindow>

#include <filesystem>
#include <memory>

class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QWidget;

namespace orca::ui {

// Qt Widgets replacement for the old loopback HTML admin page. There is no
// HTTP listener, no CSP/origin checking and no self-update surface.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::filesystem::path configPath, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void refresh();
    void onToggleHubForm();
    void onSaveHub();
    void onTogglePairingForm();
    void onApplyPairing();
    void onToggleConfigFile();

private:
    void buildUi();
    QWidget* makeCard(const QString& title, const QString& tip, QLabel** chipOut);
    void render(const AdminStatus& status);

    static void setDot(QLabel* label, const char* color);
    static void setChip(QLabel* label, const QString& text, ChipLevel level);
    static void setNote(QLabel* label, const QString& text, bool bad);

    ConfigStore store_;
    std::unique_ptr<StatusProvider> provider_;
    QTimer* timer_ = nullptr;

    QLabel* verdictDot_ = nullptr;
    QLabel* verdictText_ = nullptr;

    QLabel* hubChip_ = nullptr;
    QLabel* hubDot_ = nullptr;
    QLabel* hubValue_ = nullptr;
    QLabel* hubUrl_ = nullptr;
    QFrame* hubForm_ = nullptr;
    QLineEdit* serverUrlEdit_ = nullptr;
    QLineEdit* gatewayKeyEdit_ = nullptr;
    QPushButton* hubEditButton_ = nullptr;
    QLabel* hubNote_ = nullptr;

    QLabel* orcaChip_ = nullptr;
    QLabel* pairDot_ = nullptr;
    QLabel* pairState_ = nullptr;
    QLabel* orcaValue_ = nullptr;
    QFrame* pairForm_ = nullptr;
    QPlainTextEdit* pairingEdit_ = nullptr;
    QPushButton* pairEditButton_ = nullptr;
    QLabel* pairNote_ = nullptr;

    QLabel* gatewayId_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QLabel* configPathLabel_ = nullptr;
    QLabel* configNote_ = nullptr;
    QPlainTextEdit* configView_ = nullptr;
    QPushButton* configToggle_ = nullptr;
};

}  // namespace orca::ui
