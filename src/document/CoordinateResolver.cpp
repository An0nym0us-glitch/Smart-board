#include "document/CoordinateResolver.h"

#include "document/DocumentObject.h"
#include "document/Page.h"

namespace cb {

const CoordinateSystem* resolveCoordinateSystem(const Page* page, const CoordinateSystem* global,
                                                const QVector<QPointF>& pagePoints, const DocumentObject* exclude)
{
    if (!page || pagePoints.isEmpty())
        return global;
    const auto& objects = page->objects();
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        const DocumentObject* o = it->get();
        if (o == exclude)
            continue;
        const CoordinateSystem* cs = o->mathCoordinateSystem();
        if (!cs)
            continue;
        bool allInside = true;
        for (const QPointF& p : pagePoints) {
            if (!o->hitTest(p, 0.0)) {
                allInside = false;
                break;
            }
        }
        if (allInside)
            return cs;
    }
    return global;
}

} // namespace cb
