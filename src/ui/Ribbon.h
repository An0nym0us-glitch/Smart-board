#pragma once

#include "tools/Tool.h"

#include <QWidget>

namespace cb {

struct UiContext;
class TouchButton;

/// The bottom command ribbon: always visible, large touch targets, the primary command centre.
///
/// MORE | PEN | ERASER | SELECT | [active tool chip] | UNDO | REDO | ‹ 3 / 12 › | + PAGE | zoom | fullscreen
class Ribbon : public QWidget
{
    Q_OBJECT
public:
    explicit Ribbon(const UiContext& ui, QWidget* parent = nullptr);

    TouchButton* moreButton() const { return m_more; }
    TouchButton* penButton() const { return m_pen; }
    TouchButton* eraserButton() const { return m_eraser; }
    TouchButton* selectButton() const { return m_select; }
    TouchButton* toolChip() const { return m_toolChip; }
    TouchButton* pageButton() const { return m_page; }
    TouchButton* newPageButton() const { return m_newPage; }
    TouchButton* zoomButton() const { return m_zoom; }

    void setActiveTool(ToolId id);
    /// Shows a chip for tools reached through MORE (shape, text, measure ...). Empty icon hides it.
    void setToolChip(const QString& icon, const QString& label);
    void setPenColor(const QColor& color);
    void setPageInfo(int currentIndex, int count);
    void setUndoRedo(bool canUndo, bool canRedo, const QString& undoText, const QString& redoText);
    void setZoomPercent(int percent);
    void setFullScreen(bool on);
    void setMoreOpen(bool open);

    QSize sizeHint() const override;

signals:
    void moreClicked();
    void penClicked();
    void eraserClicked();
    void selectClicked();
    void toolChipClicked();
    void undoClicked();
    void redoClicked();
    void previousPageClicked();
    void nextPageClicked();
    void pageIndicatorClicked();
    void newPageClicked();
    void zoomClicked();
    void fullScreenClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TouchButton* addButton(const QString& icon, const QString& text);

    const UiContext& m_ui;
    TouchButton* m_more = nullptr;
    TouchButton* m_pen = nullptr;
    TouchButton* m_eraser = nullptr;
    TouchButton* m_select = nullptr;
    TouchButton* m_toolChip = nullptr;
    TouchButton* m_undo = nullptr;
    TouchButton* m_redo = nullptr;
    TouchButton* m_prev = nullptr;
    TouchButton* m_page = nullptr;
    TouchButton* m_next = nullptr;
    TouchButton* m_newPage = nullptr;
    TouchButton* m_zoom = nullptr;
    TouchButton* m_fullScreen = nullptr;
};

} // namespace cb
