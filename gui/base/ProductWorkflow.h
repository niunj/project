#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStringList>

namespace vsgraysim::ui {

/// Product handoff uses sibling executables and structured arguments. The
/// scenario path remains absolute even when a child chooses its resource root.
struct ProductWorkflow {
    static QString resolveSceneAsset(const QString& reference, const QString& scenario,
                                     const QString& resourceRoot) {
        if (reference.isEmpty()) return {};
        QStringList candidates;
        if (QFileInfo(reference).isAbsolute()) candidates << reference;
        else {
            if (!scenario.isEmpty()) candidates << QFileInfo(scenario).absoluteDir().absoluteFilePath(reference);
            candidates << QDir(resourceRoot).absoluteFilePath(reference)
                       << QDir::current().absoluteFilePath(reference);
        }
        for (const auto& candidate : candidates) {
            const QFileInfo file(candidate);
            if (file.isFile()) return file.canonicalFilePath();
        }
        return {};
    }

    static QString executablePath(const QString& name) {
        const QDir binaries(QCoreApplication::applicationDirPath());
        for (const auto& relative : {name, QStringLiteral("../app/") + name}) {
            const QFileInfo candidate(binaries.absoluteFilePath(relative));
            if (candidate.isFile() && candidate.isExecutable()) return candidate.absoluteFilePath();
        }
        return {};
    }

    static bool launch(QWidget* parent, const QString& executable,
                       const QStringList& arguments, const QString& label) {
        const QString program = executablePath(executable);
        if (program.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("无法启动%1").arg(label),
                QObject::tr("未找到可执行程序 %1。请先构建并安装完整产品套件。")
                    .arg(executable));
            return false;
        }
        qint64 processId = 0;
        if (!QProcess::startDetached(program, arguments, QDir::currentPath(), &processId)) {
            QMessageBox::warning(parent, QObject::tr("无法启动%1").arg(label),
                QObject::tr("操作系统未能启动 %1。\n程序：%2").arg(label, program));
            return false;
        }
        return true;
    }
};

} // namespace vsgraysim::ui
