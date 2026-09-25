#pragma once

#include <QPointF>
#include <QVector>

namespace cb {

class CoordinateSystem;
class DocumentObject;
class Page;

/// Chooses the mathematical coordinate system for page points: if all points lie inside an
/// object with its own coordinate system (a function graph) that one is used, otherwise the
/// page-global system. This is what makes measurements inside a graph use graph units.
const CoordinateSystem* resolveCoordinateSystem(const Page* page, const CoordinateSystem* global,
                                                const QVector<QPointF>& pagePoints,
                                                const DocumentObject* exclude = nullptr);

} // namespace cb
