#pragma once
#include <VoxelDeploymentAdapter.h>
#include <QWidget>
#include <QPointer>
#include <functional>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace vsgraysim::gui {
class VsgQtWidget;

class SmartDeploymentPanel final : public QWidget {
public:
    struct Host {
        std::function<earth::VoxelScene*()> runtime;
        std::function<nlohmann::json&()> topic;
        std::function<QString()> path;
        std::function<QString()> selected;
        std::function<void()> changed;
    };
    SmartDeploymentPanel(Host,VsgQtWidget*,QWidget* parent);
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    earth::VoxelScene& scene() const;
    std::optional<vsg::dvec3> selectedPoint() const;
    void invalidate();
    void requestPlan();
    void finishPlan(QNetworkReply*);
    void preview();
    void apply();
    void finishApply();
    void commit(earth::VoxelSceneSnapshotPtr prepared={});
    void undo();
    void reportError(const std::exception&);
    void updateButtons();

    Host host_;
    QPointer<VsgQtWidget> view_;
    std::optional<earth::deployment::Adapter> adapter_;
    std::optional<earth::deployment::Edit> staged_;
    std::optional<earth::deployment::Undo> undo_;
    std::optional<earth::VoxelScene::ImportPreparation> preparing_;
    std::optional<vsg::dvec3> point_;
    std::weak_ptr<const earth::VoxelSceneSnapshot> requested_;
    const earth::VoxelScene* pointScene_=nullptr;
    QString pointTopic_;
    QString requestTopic_;
    QString stagedTopic_;
    QString undoTopic_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    QByteArray response_;
    bool oversized_=false;
    QPlainTextEdit *instruction_,*plan_;
    QWidget* review_;
    QLineEdit *endpoint_,*model_;
    QPushButton *pick_,*generate_,*preview_,*apply_,*undoButton_,*example_;
    QLabel *pointLabel_,*status_;
    QTimer* poll_;
};
}
