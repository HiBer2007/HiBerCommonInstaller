// QtResourceSource: qrc-backed IDeploySource ("qrc:/prefix" payload).
// Registered into the core's deploy-source factory by registerQtSources().
// GUI shell only (Qt resource system).

#include "hci/payload.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>

namespace hci {

namespace {

class QtResourceSource : public IDeploySource {
public:
    explicit QtResourceSource(const std::string& prefix) : prefix_(prefix)
    {
        kind_ = "qrc";
        // Normalize "/deploy" -> ":/deploy" for the Qt resource engine.
        resPrefix_ = prefix;
        if (!resPrefix_.empty() && resPrefix_[0] != ':') resPrefix_ = ":" + resPrefix_;
    }

    bool open(std::string& error) override
    {
        if (QDir(QString::fromUtf8(resPrefix_.c_str())).exists()) return true;
        error = "qrc prefix not found: " + prefix_;
        return false;
    }

    bool enumerate(std::vector<DeployEntry>& entries, std::string& error) override
    {
        (void)error;
        QDirIterator it(QString::fromUtf8(resPrefix_.c_str()),
                        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            QFileInfo fi = it.fileInfo();
            std::string rel = it.filePath().toUtf8().toStdString();
            if (rel.size() > resPrefix_.size()) rel.erase(0, resPrefix_.size());
            while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
            DeployEntry de;
            de.relPath = rel;
            de.isDir = fi.isDir();
            de.size = de.isDir ? 0LL : static_cast<long long>(fi.size());
            entries.push_back(std::move(de));
        }
        return true;
    }

    bool readFile(const std::string& relPath, std::vector<char>& out,
                  std::string& error) override
    {
        QString full = QString::fromUtf8(resPrefix_.c_str()) + QStringLiteral("/") +
                       QString::fromUtf8(relPath.c_str());
        QFile f(full);
        if (!f.open(QIODevice::ReadOnly)) {
            error = "cannot open qrc file: " + relPath;
            return false;
        }
        QByteArray data = f.readAll();
        f.close();
        out.assign(data.constData(), data.constData() + data.size());
        return true;
    }

private:
    std::string prefix_;    // as written in the product spec ("/deploy")
    std::string resPrefix_; // Qt resource form (":/deploy")
};

} // namespace

// Registers the "qrc" deploy source kind. Call once at GUI shell startup.
bool registerQtSources()
{
    hci::registerDeploySourceFactory("qrc",
        [](const std::string& rest, std::string&) -> std::shared_ptr<IDeploySource> {
            return std::make_shared<QtResourceSource>(rest);
        });
    return true;
}

} // namespace hci