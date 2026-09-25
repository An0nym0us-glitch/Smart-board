#pragma once

#include "document/Commands.h"
#include "tools/Tool.h"
#include "tools/ToolSettings.h"

#include <QHash>
#include <QSet>

#include <vector>

namespace cb {

/// Stroke, object and area eraser, plus the grouped palm-erase gesture.
///
/// An erase "session" lasts while any eraser pointer (or the palm) is down; all changes of a
/// session become a single undo step (ReplaceObjectsCommand).
class EraserTool final : public Tool
{
public:
    explicit EraserTool(ToolHost& host);

    ToolId id() const override { return ToolId::Eraser; }
    void deactivate() override;
    void pointerDown(const PointerEvent& e) override;
    void pointerMove(const PointerEvent& e) override;
    void pointerUp(const PointerEvent& e) override;
    void pointerCancel(const PointerEvent& e) override;
    void hover(const PointerEvent& e) override;
    void paintViewOverlay(QPainter& painter) const override;
    void pageChanged() override;
    QCursor cursor() const override { return QCursor(Qt::BlankCursor); }

    // Palm gesture (page coordinates)
    void beginPalm(const QPointF& center, qreal radius);
    void movePalm(const QPointF& center, qreal radius);
    void endPalm();

private:
    struct Session
    {
        bool active = false;
        PageId page;
        EraserMode mode = EraserMode::Area;
        QHash<ObjectId, int> initialIndex;             ///< page order before the session
        std::vector<ReplaceObjectsCommand::Removed> removed;
        QSet<ObjectId> created;
        int users = 0;
    };

    void beginSession(EraserMode mode);
    void eraseAt(const QPointF& center, qreal radius);
    void eraseAlong(const QPointF& from, const QPointF& to, qreal radius);
    void takeOut(const ObjectId& id);
    void commit();
    qreal pageRadius() const;

    Session m_session;
    QHash<int, QPointF> m_pointers; ///< last page position per pointer
    QPointF m_hoverView;
    bool m_hoverVisible = false;
    QPointF m_palmCenter;
    bool m_palmActive = false;
};

} // namespace cb
