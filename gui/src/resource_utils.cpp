#include "resource_utils.h"

#include <QResource>

namespace hci {
namespace gui {

bool readResource(const std::string& path, std::string& out)
{
    std::string p = path;
    if (p.rfind("qrc:", 0) == 0) p = p.substr(4);
    if (p.empty() || p[0] != ':') p = ":" + p;
    QResource res(QString::fromUtf8(p.c_str()));
    if (!res.isValid() || res.size() <= 0) return false;
    // Qt 6: data() returns the raw (possibly compressed) bytes stored in the
    // rcc unit; uncompressedData() decompresses (zlib/zstd per build) and
    // returns the real content. Use it for all embedded text resources.
    QByteArray bytes = res.uncompressedData();
    if (bytes.isEmpty() && res.size() > 0) return false;
    out.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
    return true;
}

} // namespace gui
} // namespace hci