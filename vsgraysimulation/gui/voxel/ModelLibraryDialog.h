#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QPixmap>
#include <nlohmann/json.hpp>
#include <array>
#include <functional>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace vsgraysim::gui {
class ModelLibraryDialog final : public QDialog {
public:
    explicit ModelLibraryDialog(QWidget* parent = nullptr);
protected:
    void closeEvent(QCloseEvent*) override;

private:
    using Json = nlohmann::json;
    using Reply = std::function<void(const Json&)>;
    void jsonRequest(const QByteArray& method, const QString& path, const Json& body, Reply);
    QNetworkReply* request(const QByteArray& method, const QString& path, const QByteArray& body = {},
                           const QByteArray& type = {});
    void loadModels(const QString& select = {});
    void select(const Json&);
    void preview();
    void readFrame();
    void adjustCamera();
    void save();
    void importModel();
    void showFrame();
    void report(const QString&);

    QNetworkAccessManager network_;
    QUrl endpoint_;
    QByteArray session_, frameToken_;
    QString viewId_;
    Json entries_ = Json::array(), selected_;
    uint64_t scope_ = 0;
    double yaw_ = 0., zoom_ = 1.;
    bool fetching_ = false, busy_ = false;
    QPixmap pixels_;
    QSize lastImageSize_;
    QComboBox *models_, *representation_;
    QLabel *image_, *status_, *details_;
    QLineEdit* label_;
    std::array<QDoubleSpinBox*,3> angles_;
    std::array<QLineEdit*,5> materials_;
    QPushButton *saveButton_, *importButton_;
};
}
