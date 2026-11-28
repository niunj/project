#pragma once

#include <QWidget>
#include <nlohmann/json_fwd.hpp>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace vsgraysim::gui {
class SimulationWorkerClient;

class SimulationServicePanel final : public QWidget {
public:
    explicit SimulationServicePanel(SimulationWorkerClient&, QWidget* parent = nullptr);
    void updateStatus(const nlohmann::json&);
    void updateProduct(const nlohmann::json&);

private:
    SimulationWorkerClient& client_;
    QComboBox *platforms_, *equipment_;
    QDoubleSpinBox *heading_, *speed_;
    QLabel *state_, *image_, *values_, *notice_;
    QPushButton* apply_;
};
}
