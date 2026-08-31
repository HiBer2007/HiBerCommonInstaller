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
    const unsigned char* data = res.data();
    if (!data) return false;
    out.assign(reinterpret_cast<const char*>(data),
               reinterpret_cast<const char*>(data) + res.size());
    return true;
}

} // namespace gui
} // namespace hci