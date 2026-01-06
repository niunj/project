#ifndef ADDMODELDALOG_H
#define ADDMODELDALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QMap>

class AddModelDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddModelDialog(const QString &category, QWidget *parent = nullptr);
    ~AddModelDialog();

    QString getModelName() const;
    QString getJpgPath() const;
    QString getModelPath() const;
    QString getTexturePath() const;
    QString getMaterialPath() const;
    QString getMeshPath() const;
    QString getCategory() const;

private slots:
    void onJpgButtonClicked();
    void onModelButtonClicked();
    void onTextureButtonClicked();
    void onMaterialButtonClicked();
    void onMeshButtonClicked();
    void onOkButtonClicked();
    void onCancelButtonClicked();

private:
    QString m_category;
    QLineEdit *m_nameLineEdit;
    QLineEdit *m_jpgLineEdit;
    QLineEdit *m_modelLineEdit;
    QLineEdit *m_textureLineEdit;
    QLineEdit *m_materialLineEdit;
    QLineEdit *m_meshLineEdit;
    QPushButton *m_modelButton;
    QPushButton *m_jpgButton;
    QPushButton *m_textureButton;
    QPushButton *m_materialButton;
    QPushButton *m_meshButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};

#endif // ADDMODELDALOG_H
