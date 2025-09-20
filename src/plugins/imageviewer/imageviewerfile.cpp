// Copyright (C) 2016 Denis Mingulov.
// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "imageviewerfile.h"

#include "imageviewerconstants.h"
#include "imageviewertr.h"

#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/ieditor.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

#include <QGraphicsPixmapItem>
#include <QImageReader>
#include <QMovie>
#include <QPainter>
#include <QPixmap>

#ifndef QT_NO_SVG
#include <QGraphicsSvgItem>
#endif

using namespace Core;
using namespace Utils;

namespace ImageViewer::Internal {

class MovieItem : public QObject, public QGraphicsPixmapItem
{
public:
    MovieItem(QMovie *movie)
        : m_movie(movie)
    {
        setPixmap(m_movie->currentPixmap());
        connect(m_movie, &QMovie::updated, this, [this](const QRectF &rect) {
            update(rect);
        });
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const bool smoothTransform = painter->worldTransform().m11() < 1;
        painter->setRenderHint(QPainter::SmoothPixmapTransform, smoothTransform);
        painter->drawPixmap(offset(), m_movie->currentPixmap());
    }

private:
    QMovie *m_movie;
};

ImageViewerFile::ImageViewerFile()
{
    setId(Constants::IMAGEVIEWER_ID);
    connect(this, &ImageViewerFile::mimeTypeChanged, this, &ImageViewerFile::changed);
}

ImageViewerFile::~ImageViewerFile()
{
    cleanUp();
}

Result<> ImageViewerFile::open(const FilePath &filePath, const FilePath &realfilePath)
{
    QTC_CHECK(filePath == realfilePath); // does not support auto save
    Result<> res = openImpl(filePath);
    emit openFinished(res.has_value());
    return res;
}

Result<> ImageViewerFile::openImpl(const FilePath &filePath)
{
    cleanUp();

    if (!filePath.isReadableFile())
        return ResultError(Tr::tr("File not readable."));

    const QString &fileName = filePath.toUrlishString();
    QByteArray format = QImageReader::imageFormat(fileName);
    // if it is impossible to recognize a file format - file will not be open correctly
    if (format.isEmpty())
        return ResultError(Tr::tr("Image format not supported."));

#ifndef QT_NO_SVG
    if (format.startsWith("svg")) {
        m_tempSvgItem = new QGraphicsSvgItem(fileName);
        QRectF bound = m_tempSvgItem->boundingRect();
        if (!bound.isValid() || (qFuzzyIsNull(bound.width()) && qFuzzyIsNull(bound.height()))) {
            delete m_tempSvgItem;
            m_tempSvgItem = nullptr;
            return ResultError(Tr::tr("Failed to read SVG image."));
        }
        m_type = TypeSvg;
        emit imageSizeChanged(m_tempSvgItem->boundingRect().size().toSize());
    } else
#endif
    if (QMovie::supportedFormats().contains(format)) {
        m_movie = new QMovie(fileName, QByteArray(), this);
        // force reading movie/image data, so we can catch completely invalid movies/images early:
        m_movie->jumpToNextFrame();
        if (!m_movie->isValid()) {
            delete m_movie;
            m_movie = nullptr;
            return ResultError(Tr::tr("Failed to read image."));
        }
        m_type = TypeMovie;
        connect(m_movie, &QMovie::resized, this, &ImageViewerFile::imageSizeChanged);
        connect(m_movie, &QMovie::stateChanged, this, &ImageViewerFile::movieStateChanged);
    } else {
        m_pixmap = new QPixmap(fileName);
        if (m_pixmap->isNull()) {
            delete m_pixmap;
            m_pixmap = nullptr;
            return ResultError(Tr::tr("Failed to read image."));
        }
        m_type = TypePixmap;
        emit imageSizeChanged(m_pixmap->size());
    }

    setFilePath(filePath);
    setMimeType(Utils::mimeTypeForFile(filePath).name());
    return ResultOk;
}

Core::IDocument::ReloadBehavior ImageViewerFile::reloadBehavior(ChangeTrigger state, ChangeType type) const
{
    if (type == TypeRemoved)
        return BehaviorSilent;
    if (type == TypeContents && state == TriggerInternal && !isModified())
        return BehaviorSilent;
    return BehaviorAsk;
}

Result<> ImageViewerFile::reload(IDocument::ReloadFlag flag, IDocument::ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return ResultOk;
    emit aboutToReload();
    const Result<> result = openImpl(filePath());
    emit reloadFinished(result.has_value());
    return result;
}

QMovie *ImageViewerFile::movie() const
{
    return m_movie;
}

QGraphicsItem *ImageViewerFile::createGraphicsItem() const
{
    QGraphicsItem *val = nullptr;
    switch (m_type) {
    case TypeInvalid:
        break;
    case TypeSvg:
#ifndef QT_NO_SVG
        if (m_tempSvgItem) {
            val = m_tempSvgItem;
            m_tempSvgItem = nullptr;
        } else {
            val = new QGraphicsSvgItem(filePath().toUrlishString());
        }
#endif
        break;
    case TypeMovie:
        val = new MovieItem(m_movie);
        break;
    case TypePixmap: {
        auto pixmapItem = new QGraphicsPixmapItem(*m_pixmap);
        pixmapItem->setTransformationMode(Qt::SmoothTransformation);
        val = pixmapItem;
        break;
    }
    default:
        break;
    }
    return val;
}

ImageViewerFile::ImageType ImageViewerFile::type() const
{
    return m_type;
}

void ImageViewerFile::updateVisibility()
{
    if (!m_movie || m_movie->state() != QMovie::Running)
        return;
    const bool anyVisible = Utils::anyOf(Core::DocumentModel::editorsForDocument(this),
                                           [] (Core::IEditor *editor)
                                           { return editor->widget()->isVisible(); });
    if (!anyVisible)
        m_movie->setPaused(true);
}

void ImageViewerFile::cleanUp()
{
    delete m_pixmap;
    m_pixmap = nullptr;
    delete m_movie;
    m_movie = nullptr;
#ifndef QT_NO_SVG
    delete m_tempSvgItem;
    m_tempSvgItem = nullptr;
#endif
    m_type = TypeInvalid;
}

} // ImageViewer::Internal
