// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "textmark.h"

#include "fontsettings.h"
#include "textdocument.h"
#include "texteditor.h"
#include "texteditortr.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/icore.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/tooltip/tooltip.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QDesktopServices>
#include <QGridLayout>
#include <QPainter>
#include <QToolButton>

using namespace Core;
using namespace Utils;
using namespace TextEditor::Internal;

namespace TextEditor {

class TextMarkRegistry : public QObject
{
public:
    TextMarkRegistry(QObject *parent);

    static void add(TextMark *mark);
    static void add(TextMark *mark, TextDocument *document);
    static bool remove(TextMark *mark);

private:
    void editorOpened(Core::IEditor *editor);
    void documentRenamed(Core::IDocument *document,
                         const FilePath &oldPath,
                         const FilePath &newPath);
    void allDocumentsRenamed(const FilePath &oldPath, const FilePath &newPath);

};

static QHash<FilePath, QSet<TextMark *>> s_marks;

class AnnotationColors
{
public:
    static AnnotationColors &getAnnotationColors(const QColor &markColor,
                                                 const QColor &backgroundColor);

public:
    using SourceColors = QPair<QColor, QColor>;
    QColor rectColor;
    QColor textColor;

private:
    static QHash<SourceColors, AnnotationColors> m_colorCache;
};

TextMark::TextMark(const FilePath &filePath, int lineNumber, TextMarkCategory category)
    : m_fileName(filePath)
    , m_lineNumber(lineNumber)
    , m_visible(true)
    , m_category(category)
{
    if (!m_fileName.isEmpty())
        TextMarkRegistry::add(this);
}

TextMark::TextMark(TextDocument *document, int lineNumber, TextMarkCategory category)
    : m_fileName(QTC_GUARD(document) ? document->filePath() : FilePath())
    , m_lineNumber(lineNumber)
    , m_visible(true)
    , m_category(category)
{
    if (!m_fileName.isEmpty())
        TextMarkRegistry::add(this, document);
}

TextMark::~TextMark()
{
    if (!m_fileName.isEmpty())
        TextMarkRegistry::remove(this);
    if (m_baseTextDocument)
        m_baseTextDocument->removeMark(this);
    if (m_deleteCallback)
        m_deleteCallback();
    m_baseTextDocument = nullptr;
}

FilePath TextMark::filePath() const
{
    return m_fileName;
}

void TextMark::updateFilePath(const FilePath &filePath)
{
    if (filePath == m_fileName)
        return;
    if (!m_fileName.isEmpty())
        TextMarkRegistry::remove(this);
    m_fileName = filePath;
    if (!m_fileName.isEmpty())
        TextMarkRegistry::add(this);
}

int TextMark::lineNumber() const
{
    return m_lineNumber;
}

void TextMark::paintIcon(QPainter *painter, const QRect &rect) const
{
    icon().paint(painter, rect, Qt::AlignCenter);
}

void TextMark::paintAnnotation(QPainter &painter,
                               const QRect &eventRect,
                               QRectF *annotationRect,
                               const qreal fadeInOffset,
                               const qreal fadeOutOffset,
                               const QPointF &contentOffset) const
{
    QString text = lineAnnotation();
    if (text.isEmpty())
        return;

    const AnnotationRects &rects = annotationRects(*annotationRect,
                                                   painter.fontMetrics(),
                                                   fadeInOffset,
                                                   fadeOutOffset);
    if (m_staticAnnotationText.text() != rects.text) {
        m_staticAnnotationText.setText(rects.text);
        m_staticAnnotationText.setTextFormat(m_annotationTextFormat);
    }
    annotationRect->setRight(rects.fadeOutRect.right());
    const QRectF eventRectF(eventRect);
    if (!(rects.fadeInRect.intersects(eventRectF) || rects.annotationRect.intersects(eventRectF)
          || rects.fadeOutRect.intersects(eventRectF))) {
        return;
    }

    const QColor &markColor = annotationColor();

    const FontSettings &fontSettings = m_baseTextDocument->fontSettings();
    const AnnotationColors colors = AnnotationColors::getAnnotationColors(
                markColor.isValid() ? markColor : painter.pen().color(),
                fontSettings.toTextCharFormat(C_TEXT).background().color());

    painter.save();
    QLinearGradient grad(rects.fadeInRect.topLeft() - contentOffset,
                         rects.fadeInRect.topRight() - contentOffset);
    grad.setColorAt(0.0, Qt::transparent);
    grad.setColorAt(1.0, colors.rectColor);
    painter.fillRect(rects.fadeInRect, grad);
    painter.fillRect(rects.annotationRect, colors.rectColor);
    painter.setPen(colors.textColor);
    paintIcon(&painter, rects.iconRect.toAlignedRect());
    painter.drawStaticText(rects.textRect.topLeft(), m_staticAnnotationText);
    if (rects.fadeOutRect.isValid()) {
        grad = QLinearGradient(rects.fadeOutRect.topLeft() - contentOffset,
                               rects.fadeOutRect.topRight() - contentOffset);
        grad.setColorAt(0.0, colors.rectColor);
        grad.setColorAt(1.0, Qt::transparent);
        painter.fillRect(rects.fadeOutRect, grad);
    }
    painter.restore();
}

TextMark::AnnotationRects TextMark::annotationRects(const QRectF &boundingRect,
                                                    const QFontMetrics &fm,
                                                    const qreal fadeInOffset,
                                                    const qreal fadeOutOffset) const
{
    AnnotationRects rects;
    rects.text = lineAnnotation().simplified();
    if (rects.text.isEmpty())
        return rects;
    // truncate the text to a sensible length to avoid expensive width calculation in QFontMetrics
    // see QTBUG-138487
    rects.text.truncate(1.2 * boundingRect.width() / fm.averageCharWidth());

    rects.fadeInRect = boundingRect;
    rects.fadeInRect.setWidth(fadeInOffset);
    rects.annotationRect = boundingRect;
    rects.annotationRect.setLeft(rects.fadeInRect.right());
    const bool drawIcon = !icon().isNull();
    constexpr qreal margin = 1;
    rects.iconRect = QRectF(rects.annotationRect.left(), boundingRect.top(),
                            0, boundingRect.height());
    if (drawIcon)
        rects.iconRect.setWidth(rects.iconRect.height());
    rects.textRect = QRectF(rects.iconRect.right() + margin, boundingRect.top(),
                            qreal(fm.horizontalAdvance(rects.text)), boundingRect.height());
    rects.annotationRect.setRight(rects.textRect.right() + margin);
    if (rects.annotationRect.right() > boundingRect.right()) {
        rects.textRect.setRight(boundingRect.right() - margin);
        rects.text = fm.elidedText(rects.text, Qt::ElideRight, int(rects.textRect.width()));
        rects.annotationRect.setRight(boundingRect.right());
        rects.fadeOutRect = QRectF(rects.annotationRect.topRight(),
                                   rects.annotationRect.bottomRight());
    } else {
        rects.fadeOutRect = boundingRect;
        rects.fadeOutRect.setLeft(rects.annotationRect.right());
        rects.fadeOutRect.setWidth(fadeOutOffset);
    }
    return rects;
}

void TextMark::updateLineNumber(int lineNumber)
{
    m_lineNumber = lineNumber;
}

void TextMark::move(int line)
{
    if (line == m_lineNumber)
        return;
    const int previousLine = m_lineNumber;
    m_lineNumber = line;
    if (m_baseTextDocument)
        m_baseTextDocument->moveMark(this, previousLine);
}

void TextMark::updateBlock(const QTextBlock &)
{}

void TextMark::removedFromEditor()
{}

void TextMark::updateMarker()
{
    if (m_baseTextDocument)
        m_baseTextDocument->scheduleUpdateLayout();
}

void TextMark::setPriority(TextMark::Priority prioriy)
{
    m_priority = prioriy;
    if (m_baseTextDocument)
        m_baseTextDocument->updateMark(this);
}

bool TextMark::isVisible() const
{
    return m_visible;
}

void TextMark::setVisible(bool visible)
{
    m_visible = visible;
    updateMarker();
}

bool TextMark::isClickable() const
{
    return false;
}

void TextMark::clicked()
{}

bool TextMark::isDraggable() const
{
    return false;
}

void TextMark::dragToLine(int lineNumber)
{
    Q_UNUSED(lineNumber)
}

void TextMark::addToToolTipLayout(QGridLayout *target) const
{
    auto contentLayout = new QVBoxLayout;
    addToolTipContent(contentLayout);
    if (contentLayout->count() <= 0)
        return;

    // Left column: text mark icon
    const int row = target->rowCount();
    const QIcon icon = this->icon();
    if (!icon.isNull()) {
        auto iconLabel = new QLabel;
        iconLabel->setPixmap(icon.pixmap(16, 16));
        target->addWidget(iconLabel, row, 0, Qt::AlignTop | Qt::AlignHCenter);
    }

    // Middle column: tooltip content
    target->addLayout(contentLayout, row, 1);

    // Right column: action icons/button
    QList<QAction *> actions;
    if (m_actionsProvider)
        actions = m_actionsProvider();
    if (m_category.id.isValid() && !m_lineAnnotation.isEmpty()) {
        auto visibilityAction = new QAction;
        const bool isHidden = TextDocument::marksAnnotationHidden(m_category.id);
        visibilityAction->setIcon(Utils::Icons::EYE_OPEN.icon());
        const QString tooltip = (isHidden ? Tr::tr("Show inline annotations for %1")
                                          : Tr::tr("Temporarily hide inline annotations for %1"))
                                    .arg(m_category.displayName);
        visibilityAction->setToolTip(tooltip);
        auto callback = [id = m_category.id, isHidden] {
            if (isHidden)
                TextDocument::showMarksAnnotation(id);
            else
                TextDocument::temporaryHideMarksAnnotation(id);
        };
        QObject::connect(visibilityAction, &QAction::triggered, Core::ICore::instance(), callback);
        actions.append(visibilityAction);
    }
    if (m_settingsPage.isValid()) {
        auto settingsAction = new QAction;
        settingsAction->setIcon(Utils::Icons::SETTINGS.icon());
        settingsAction->setToolTip(Tr::tr("Show Diagnostic Settings"));
        QObject::connect(settingsAction, &QAction::triggered, Core::ICore::instance(),
            [id = m_settingsPage] { Core::ICore::showOptionsDialog(id); },
            Qt::QueuedConnection);
        actions.append(settingsAction);
    }
    if (!actions.isEmpty()) {
        auto actionsLayout = new QHBoxLayout;
        QMargins margins = actionsLayout->contentsMargins();
        margins.setLeft(margins.left() + 5);
        actionsLayout->setContentsMargins(margins);
        for (QAction *action : std::as_const(actions)) {
            QTC_ASSERT(!action->icon().isNull(), delete action; continue);
            auto button = new QToolButton;
            button->setIcon(action->icon());
            button->setToolTip(action->toolTip());
            action->setParent(button);
            QObject::connect(button, &QToolButton::clicked, action, &QAction::triggered);
            QObject::connect(button, &QToolButton::clicked, []() {
                Utils::ToolTip::hideImmediately();
            });
            actionsLayout->addWidget(button, 0, Qt::AlignTop | Qt::AlignRight);
        }
        target->addLayout(actionsLayout, row, 2);
    }
}

bool TextMark::addToolTipContent(QLayout *target) const
{
    bool useDefaultToolTip = false;
    QString text = toolTip();
    if (text.isEmpty()) {
        useDefaultToolTip = true;
        text = m_defaultToolTip;
        if (text.isEmpty())
            return false;
    }

    auto textLabel = new QLabel;
    textLabel->setText(text);
    // Differentiate between tool tips that where explicitly set and default tool tips.
    textLabel->setDisabled(useDefaultToolTip);
    target->addWidget(textLabel);
    QObject::connect(textLabel, &QLabel::linkActivated, [](const QString &link) {
        if (OutputLineParser::isLinkTarget(link)) {
            Core::EditorManager::openEditorAt(OutputLineParser::parseLinkTarget(link), {},
                                              Core::EditorManager::SwitchSplitIfAlreadyVisible);
        } else {
            QDesktopServices::openUrl(link);
        }
    });

    return true;
}

QColor TextMark::annotationColor() const
{
    if (m_color.has_value())
        return Utils::creatorColor(*m_color).toHsl();
    return {};
}

void TextMark::setIcon(const QIcon &icon)
{
    m_icon = icon;
    m_iconProvider = std::function<QIcon()>();
    updateMarker();
}

void TextMark::setIconProvider(const std::function<QIcon ()> &iconProvider)
{
    m_iconProvider = iconProvider;
    updateMarker();
}

const QIcon TextMark::icon() const
{
    return m_iconProvider ? m_iconProvider() : m_icon;
}

std::optional<Theme::Color> TextMark::color() const
{
    return m_color;
}

void TextMark::setColor(const Theme::Color &color)
{
    if (m_color.has_value() && *m_color == color)
        return;
    m_color = color;
    updateMarker();
}

void TextMark::unsetColor()
{
    m_color.reset();
    updateMarker();
}

void TextMark::setLineAnnotation(const QString &lineAnnotation)
{
    m_lineAnnotation = lineAnnotation;
    updateMarker();
}

void TextMark::setToolTipProvider(const std::function<QString()> &toolTipProvider)
{
    m_toolTipProvider = toolTipProvider;
}

QString TextMark::toolTip() const
{
    return m_toolTipProvider ? m_toolTipProvider() : m_toolTip;
}

void TextMark::setToolTip(const QString &toolTip)
{
    m_toolTip = toolTip;
    m_toolTipProvider = std::function<QString()>();
}

void TextMark::setActionsProvider(const std::function<QList<QAction *>()> &actionsProvider)
{
    m_actionsProvider = actionsProvider;
}

void TextMark::setSettingsPage(Id settingsPage)
{
    m_settingsPage = settingsPage;
}

Qt::TextFormat TextMark::annotationTextFormat() const
{
    return m_annotationTextFormat;
}

void TextMark::setAnnotationTextFormat(Qt::TextFormat newTextFormat)
{
    m_annotationTextFormat = newTextFormat;
}

bool TextMark::isLocationMarker() const
{
    return m_isLocationMarker;
}

void TextMark::setIsLocationMarker(bool newIsLocationMarker)
{
    m_isLocationMarker = newIsLocationMarker;
}

TextMarkRegistry::TextMarkRegistry(QObject *parent)
    : QObject(parent)
{
    connect(EditorManager::instance(), &EditorManager::editorOpened,
            this, &TextMarkRegistry::editorOpened);

    connect(DocumentManager::instance(), &DocumentManager::allDocumentsRenamed,
            this, &TextMarkRegistry::allDocumentsRenamed);
    connect(DocumentManager::instance(), &DocumentManager::documentRenamed,
            this, &TextMarkRegistry::documentRenamed);
}

void TextMarkRegistry::add(TextMark *mark)
{
    add(mark, TextDocument::textDocumentForFilePath(mark->filePath()));
}

void TextMarkRegistry::add(TextMark *mark, TextDocument *document)
{
    s_marks[mark->filePath()].insert(mark);
    if (document)
        document->addMark(mark);
}

bool TextMarkRegistry::remove(TextMark *mark)
{
    return s_marks[mark->filePath()].remove(mark);
}

void TextMarkRegistry::editorOpened(IEditor *editor)
{
    auto document = qobject_cast<TextDocument *>(editor ? editor->document() : nullptr);
    if (!document)
        return;
    if (!s_marks.contains(document->filePath()))
        return;

    const QSet<TextMark *> marks = s_marks.value(document->filePath());
    for (TextMark *mark : marks)
        document->addMark(mark);
}

void TextMarkRegistry::documentRenamed(IDocument *document,
                                       const FilePath &oldPath,
                                       const FilePath &newPath)
{
    auto baseTextDocument = qobject_cast<TextDocument *>(document);
    if (!baseTextDocument)
        return;
    if (!s_marks.contains(oldPath))
        return;

    QSet<TextMark *> toBeMoved;
    const QList<TextMark *> marks = baseTextDocument->marks();
    for (TextMark *mark : marks)
        toBeMoved.insert(mark);

    s_marks[oldPath].subtract(toBeMoved);
    s_marks[newPath].unite(toBeMoved);

    for (TextMark *mark : std::as_const(toBeMoved))
        mark->updateFilePath(newPath);
}

void TextMarkRegistry::allDocumentsRenamed(const FilePath &oldPath, const FilePath &newPath)
{
    if (!s_marks.contains(oldPath))
        return;

    const QSet<TextMark *> oldFileNameMarks = s_marks.value(oldPath);

    s_marks[newPath].unite(oldFileNameMarks);
    s_marks[oldPath].clear();

    for (TextMark *mark : oldFileNameMarks)
        mark->updateFilePath(newPath);
}

QHash<AnnotationColors::SourceColors, AnnotationColors> AnnotationColors::m_colorCache;

AnnotationColors &AnnotationColors::getAnnotationColors(const QColor &markColor,
                                                        const QColor &backgroundColor)
{
    auto highClipHsl = [](qreal value) {
        return std::max(0.7, std::min(0.9, value));
    };
    auto lowClipHsl = [](qreal value) {
        return std::max(0.1, std::min(0.3, value));
    };
    AnnotationColors &colors = m_colorCache[{markColor, backgroundColor}];
    if (!colors.rectColor.isValid() || !colors.textColor.isValid()) {
        const double backgroundLightness = backgroundColor.lightnessF();
        const double foregroundLightness = backgroundLightness > 0.5
                ? lowClipHsl(backgroundLightness - 0.5)
                : highClipHsl(backgroundLightness + 0.5);

        colors.rectColor = markColor;
        colors.rectColor.setAlphaF(0.15f);

        colors.textColor.setHslF(markColor.hslHueF(),
                                 markColor.hslSaturationF(),
                                 foregroundLightness);
    }
    return colors;
}

void setupTextMarkRegistry(QObject *guard)
{
    (void) new TextMarkRegistry(guard);
}

} // namespace TextEditor
