#include "partwidget.h"
#include "commands.h"
#include "mainwindow.h"

#include <cmath>
#include <QUndoStack>
#include <QCloseEvent>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QDebug>
#include <QRgb>
#include <QMdiSubWindow>
#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QBuffer>
#include <QQueue>


PartWidget::PartWidget(const QString& partName, QWidget *parent) :
    QMdiSubWindow(parent),
    mPartName(partName),
    mModeName(),
    mPart(NULL),
    mPartView(NULL),
    mOverlayPixmapItem(NULL),
    mZoom(4),
    mPosition(0,0),
    mPenSize(1),
    mPenColour(QColor(0,0,0)),
    mDrawToolType(kDrawToolPaint),
    mAnimationTimer(NULL),
    mFrameNumber(0),
    mSecondsPassedSinceLastFrame(0),
    mIsPlaying(false),
    mBoundsItem(NULL),
    mScribbling(false),
    mMovingCanvas(false),
    mPlaybackSpeedMultiplierIndex(-1),
    mPlaybackSpeedMultiplier(1),
    mOverlayImage(NULL),
    mClipboardItem(NULL),
    mCopyRectItem(NULL)
{
    // Customise window
    setMinimumSize(64,64);
    //setWindowFlags(Qt::CustomizeWindowHint); //Set window with no title bar
    //setWindowFlags(Qt::WindowCloseButtonHint);
    // setWindowFlags(Qt::FramelessWindowHint); //Set a frameless window

    // Setup part view
    mPartView = new PartView(this, new QGraphicsScene());
    mPartView->setTransform(QTransform::fromScale(mZoom,mZoom));
    setWidget(mPartView);

    updatePartFrames();
}

void PartWidget::updatePartFrames(){
    if (!mPixmapItems.isEmpty()){
        foreach(QGraphicsPixmapItem* pi, mPixmapItems){
            mPartView->scene()->removeItem(pi);
            delete pi;
        }
        mPixmapItems.clear();
    }

    if (mOverlayPixmapItem!=NULL){
        mPartView->scene()->removeItem(mOverlayPixmapItem);
        delete mOverlayPixmapItem;
        mOverlayPixmapItem = NULL;
    }

    // get rid of any remaining text etc
    mAnchorItems.clear();
    mAnchors.clear();
    for(int i=0;i<MAX_PIVOTS;i++){
        mPivotItems[i].clear();
        mPivots[i].clear();
    }

    foreach(QGraphicsRectItem* pi, mRectItems){
        mPartView->scene()->removeItem(pi);
        delete pi;
    }
    mRectItems.clear();

    if (mClipboardItem!=NULL){
        mPartView->scene()->removeItem(mClipboardItem);
        delete mClipboardItem;
        mClipboardItem = NULL;
    }

    if (mCopyRectItem!=NULL){
        mPartView->scene()->removeItem(mCopyRectItem);
        delete mCopyRectItem;
        mCopyRectItem = NULL;
    }

    mPartView->scene()->clear();
    mBoundsItem = NULL;

    if (ProjectModel::Instance()->parts.contains(mPartName)){
        mPart = ProjectModel::Instance()->parts[mPartName];
        mProperties = mPart->properties;

        if (mPart->modes.size()>0){
            if (mModeName.isEmpty() || !mPart->modes.contains(mModeName)){
                mModeName = mPart->modes.begin().key();
            }
            Part::Mode m = mPart->modes.value(mModeName);
            mNumPivots = m.numPivots;            
            mFPS = m.framesPerSecond;
            mSPF = 1./mFPS;
            int w = mPart->modes.value(mModeName).width;
            int h = mPart->modes.value(mModeName).height;
            setWindowTitle(mPartName + tr("/") + mModeName);

            mBoundsItem = mPartView->scene()->addRect(-0.5,-0.5,w+1,h+1, QPen(QColor(0,0,0), 1, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin), QBrush(QColor(0,0,0,0)));

            if (m.numFrames>0){
                for(int i=0;i<m.numFrames;i++){
                    QImage* img = m.images.at(i);
                    if (img!=NULL){
                        QGraphicsPixmapItem* pi = mPartView->scene()->addPixmap(QPixmap::fromImage(*img));
                        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect();
                        pi->setGraphicsEffect(effect);
                        mPixmapItems.push_back(pi);
                    }
                    else {
                        mPartView->scene()->addSimpleText(tr("!"));
                    }

                    QPen pivotPen = QPen(QColor(255,0,255), 0.1);
                    QFont font("monospace");
                    font.setHintingPreference(QFont::PreferFullHinting);

                    {
                        QPoint ap = m.anchor.at(i);
                        QGraphicsSimpleTextItem* ti = mPartView->scene()->addSimpleText("a", font);

                        ti->setPen(pivotPen);
                        ti->setBrush(QBrush(QColor(255,0,255)));
                        ti->setScale(0.05);
                        ti->setPos(ap.x()+0.3,ap.y()-0.1);

                        mAnchors.push_back(ap);
                        mAnchorItems.push_back(ti);
                        // QGraphicsPolygonItem* pi = mPartView->scene()->addPolygon(QPolygonF(QRectF(ap.x()+0.5, ap.y()+0.5, 0.25, 0.25)), pivotPen);
                    }

                    for(int p=0;p<MAX_PIVOTS;p++){
                        QPoint pp = m.pivots[p].at(i);
                        QGraphicsSimpleTextItem* ti = mPartView->scene()->addSimpleText(QString::number(p+1), font);
                        ti->setPen(pivotPen);
                        ti->setBrush(QBrush(QColor(255,0,255)));
                        ti->setScale(0.05);
                        ti->setPos(pp.x()+0.3,pp.y()-0.1);
                        // QGraphicsPolygonItem* pi = mPartView->scene()->addPolygon(QPolygonF(QRectF(pp.x()+0.5, pp.y()+0.5, 0.25, 0.25)), pivotPen);

                        mPivots[p].push_back(pp);
                        mPivotItems[p].push_back(ti);
                    }
                }

                updateDropShadow();
                updateOnionSkinning();
                updatePivots();
                updatePropertiesOverlays();

                if (mOverlayImage!=NULL) delete mOverlayImage;
                mOverlayImage = new QImage(w,h,QImage::Format_ARGB32);
                mOverlayImage->fill(0x00FFFFFF);
                mOverlayPixmapItem = mPartView->scene()->addPixmap(QPixmap::fromImage(*mOverlayImage));

                // Update draw tool type
                setDrawToolType(mDrawToolType);
            }
        }
        else {
            mModeName = QString();
            setWindowTitle(mPartName);
            mPartView->scene()->addSimpleText(tr("no modes"));
        }
    }
    else {
        setWindowTitle(mPartName + tr("!"));
        mPartView->scene()->addSimpleText(tr("invalid"));
    }

    if (mFrameNumber >= numFrames()){
        mFrameNumber = numFrames()-1;
    }
    setFrame(mFrameNumber);
}

void PartWidget::setFrame(int f){
    int oldf = mFrameNumber;    
    mFrameNumber = f;
    showFrame(mFrameNumber);
    if (oldf!=f){
        emit(frameChanged(mFrameNumber));
    }
}

void PartWidget::setPlaybackSpeedMultiplier(int index, float value){
    mPlaybackSpeedMultiplierIndex = index;
    mPlaybackSpeedMultiplier = value;
}

void PartWidget::setDrawToolType(DrawToolType type){
    mDrawToolType=type;

    if (mClipboardItem!=NULL){
        mPartView->scene()->removeItem(mClipboardItem);
        delete mClipboardItem;
        mClipboardItem = NULL;
    }

    if (mCopyRectItem){
        mPartView->scene()->removeItem(mCopyRectItem);
        delete mCopyRectItem;
        mCopyRectItem = NULL;
    }

    switch(mDrawToolType){
        case kDrawToolPaint: {
            mPartView->setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_draw.png")),8,8));
            break;
        }
        case kDrawToolEraser: {
            mPartView->setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_erase.png")),8,8));
            break;
        }
        case kDrawToolPickColour: {
            mPartView->setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_pickcolour.png")),8,8));
            break;
        }
        case kDrawToolStamp: {
            // TODO: tint the icon
            QImage img = QImage(":/icon/icons/tool_draw.png");
            QPixmap pm = QPixmap::fromImage(img);
            mPartView->setCursor(pm);

            QImage cImage = QGuiApplication::clipboard()->image();
            if (cImage.isNull()){
                qDebug() << "Clipboard doesn't contain valid image!";
            }
            else {
                // qDebug() << "Clipboard contains image with dim " << cImage.width() << "x" << cImage.height();
                mClipboardImage = QPixmap::fromImage(cImage);
                mClipboardItem = mPartView->scene()->addPixmap(mClipboardImage);

                QPointF pt = mPartView->mapToScene(mMousePos.x(),mMousePos.y());
                QPointF snap = QPointF(floor(pt.x()),floor(pt.y()));
                mClipboardItem->setPos(snap);
            }

            break;
         }
        case kDrawToolCopy: {
            mPartView->setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_draw.png")),8,8));
            break;
        }
        default: {
            mPartView->setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_none.png")),8,8));
            break;
        }
    }
}

void PartWidget::setMode(const QString& mode){
    mModeName = mode;
    updatePartFrames();
}

void PartWidget::partNameChanged(const QString& newPartName){
    mPartName = newPartName;
    setWindowTitle(mPartName);
}

void PartWidget::partFrameUpdated(const QString& part, const QString& mode, int /*frame*/){
    if (part==mPartName && mModeName==mode){
        // update ..
        updatePartFrames();
    }
}

void PartWidget::partFramesUpdated(const QString& part, const QString& mode){
    if (part==mPartName && mModeName==mode){
        // update ..
        updatePartFrames();
    }
}

void PartWidget::partNumPivotsUpdated(const QString& part, const QString& mode){
    if (part==mPartName && mModeName==mode){
        // update ..
        Part* part = ProjectModel::Instance()->parts[mPartName];
        Part::Mode& mode = part->modes[mModeName];
        mNumPivots = mode.numPivots;
        showFrame(mFrameNumber);
    }
}

void PartWidget::updatePropertiesOverlays(){
    // Parse the properties
    // trying to extra any info that we can draw into the scene
    foreach(QGraphicsRectItem* pi, mRectItems){
        mPartView->scene()->removeItem(pi);
        delete pi;
    }
    mRectItems.clear();

    QJsonParseError error;
    QJsonDocument propDoc = QJsonDocument::fromJson(QByteArray(mProperties.toStdString().c_str()), &error);
    // QJsonDocument prefsDoc = QJsonDocument::fromJson(QByteArray(mProperties.buffer, mProperties.length), &error);

    if (error.error!=QJsonParseError::NoError || propDoc.isNull() || propDoc.isEmpty() || !propDoc.isObject()){
        // couldn't parse.. (or not object)
    }
    else {
        static const int NUM_COLOURS = 4;
        static const QColor COLOURS[NUM_COLOURS] = {QColor(255,0,255), QColor(150,100,255), QColor(255,100,150), QColor(200,150,200)};

        int index = 0;
        QJsonObject propObj = propDoc.object();
        for(QJsonObject::iterator it = propObj.begin(); it!=propObj.end(); it++){
            QString key = it.key();

            if (key.endsWith("_rect") && it.value().isArray()){
                // draw a rectangle over the sprite

                const QJsonArray& array = it.value().toArray();
                 if (array.size()==4){
                    double x = array.at(0).toDouble(0);
                    double y = array.at(1).toDouble(0);
                    double w = array.at(2).toDouble(1);
                    double h = array.at(3).toDouble(1);

                    QPen pen = QPen(COLOURS[index%NUM_COLOURS], 0.1);
                    QBrush brush = Qt::NoBrush;
                    QGraphicsRectItem* item = mPartView->scene()->addRect(x,y,w,h,pen,brush);
                    mRectItems.append(item);

                    index++;
                 }
            }
        }
    }
}

void PartWidget::partPropertiesChanged(const QString& part){
    if (part==mPartName){
        // update ..
        Part* part = ProjectModel::Instance()->parts[mPartName];
        mProperties = part->properties;
        updatePropertiesOverlays();
        showFrame(mFrameNumber);
    }
}

void PartWidget::closeEvent(QCloseEvent *event)
{
    emit(closed(this));
    event->accept();
}

void PartWidget::updateOverlay(){
    if (mOverlayPixmapItem){
        mOverlayPixmapItem->setPixmap(QPixmap::fromImage(*mOverlayImage));
    }
}

void PartWidget::setZoom(int z){
    mZoom = z;
    // mPartView->setTransform(QTransform::fromScale(mZoom,mZoom)*QTransform::fromTranslate(mPosition.x(),mPosition.y()));
    mPartView->setTransform(QTransform::fromScale(mZoom,mZoom));
    mPartView->setSceneRect(mPartView->scene()->sceneRect().translated(mPosition.x()/mZoom, mPosition.y()/mZoom));
    updateDropShadow();
    mPartView->update();
}

void PartWidget::setPenSize(int size){
    mPenSize = size;
}

void PartWidget::setPenColour(QColor colour){
    mPenColour = colour;
}

void PartWidget::fitToWindow(){
    // use graphicsview fit to window
    if (mPartView){
        // mPartView->centerOn();
        mPartView->fitInView(mBoundsItem, Qt::KeepAspectRatio);
        QTransform transform = mPartView->transform();
        QPoint scale = transform.map(QPoint(1,1));
        mZoom = floor(scale.x()); // mPartView->transform();
        mPosition = QPoint(0,0);
        setZoom(mZoom);
        // QPoint trans = transform.map(QPoint(0,0));
        // mPosition = trans;

        zoomChanged();

    }
}

void PartWidget::updateDropShadow(){
    QSettings settings;
    float opacity = settings.value("drop_shadow_opacity", QVariant(100.f/255)).toFloat();
    float blur = settings.value("drop_shadow_blur", QVariant(0.5f)).toFloat();
    float dx = settings.value("drop_shadow_offset_x", QVariant(0.5f)).toFloat();
    float dy = settings.value("drop_shadow_offset_y", QVariant(0.5f)).toFloat();
    QColor dropShadowColour = QColor(settings.value("drop_shadow_colour", QColor(0,0,0).rgba()).toUInt());

    if (mPartView){
        foreach(QGraphicsPixmapItem* it, mPixmapItems){
            QGraphicsDropShadowEffect* effect = (QGraphicsDropShadowEffect*) it->graphicsEffect();
            effect->setOffset(dx*2*mZoom/4.,dy*2*mZoom/2.);
            effect->setColor(QColor(dropShadowColour.red(),dropShadowColour.green(),dropShadowColour.blue(),opacity*255));
            effect->setBlurRadius(blur*2*mZoom*2.);
        }
    }
}

void PartWidget::updateOnionSkinning(){
    QSettings settings;
    float trans = settings.value("onion_skinning_transparency", 0.3f).toFloat();
    bool edp = settings.value("onion_skinning_enabled_during_playback", true).toBool();
    if (mPartView){
        mOnionSkinningEnabled = (trans>0);
        mOnionSkinningTransparency = trans*0.3;
        mOnionSkinningEnabledDuringPlayback = edp;
        showFrame(mFrameNumber);
    }
}

void PartWidget::updatePivots(){
    // qDebug() << "UpdatePivots";
    QSettings settings;
    bool pivotsEnabled = settings.value("pivot_enabled", true).toBool();
    bool pivotsEnabledDuringPlayback = settings.value("pivot_enabled_during_playback", true).toBool();
    QColor pivotColour = QColor(settings.value("pivot_colour", QColor(255,0,255).rgba()).toUInt());
    if (mPartView){
        mPivotsEnabled = pivotsEnabled;
        mPivotsEnabledDuringPlayback = pivotsEnabledDuringPlayback;
        mPivotColour = pivotColour;

        // qDebug() << mPivotsEnabled << mPivotsEnabledDuringPlayback << mPivotColour;

        foreach(QGraphicsSimpleTextItem* it, mAnchorItems){
            // it->setPen(QColor(mPivotColour));
            it->setBrush(QBrush(QColor(mPivotColour)));
        }
        for(int i=0;i<MAX_PIVOTS;i++){
            foreach(QGraphicsSimpleTextItem* it, mPivotItems[i]){
                // it->setPen(QColor(mPivotColour));
                it->setBrush(QBrush(QColor(mPivotColour)));
            }
        }
        showFrame(mFrameNumber);
    }
}

void PartWidget::setPosition(QPointF pos){
    mPosition = pos;
    // mPartView->setTransform(QTransform::fromScale(mZoom,mZoom)*QTransform::fromTranslate(mPosition.x(),mPosition.y()));

    mPartView->setTransform(QTransform::fromScale(mZoom,mZoom));
    mPartView->setSceneRect(mPartView->scene()->sceneRect().translated(pos.x()/mZoom,pos.y()/mZoom));
    mPartView->update();
}

static float S_PER_UPDATE = 1.f/60;
void PartWidget::play(bool play){
    if (!play){
        mIsPlaying = false;
        if (mAnimationTimer){
            mAnimationTimer->stop();
        }
    }
    else {
        if (mAnimationTimer==NULL){
            mAnimationTimer=new QTimer(this);
            connect(mAnimationTimer, SIGNAL(timeout()), this, SLOT(updateAnimation()));
        }
        mSecondsPassedSinceLastFrame = 0;
        mAnimationTimer->start(S_PER_UPDATE*1000);
        mIsPlaying = true;
    }
}

void PartWidget::stop(){
    mIsPlaying = false;
    if (mAnimationTimer){
        mAnimationTimer->stop();
        mFrameNumber = 0;
        showFrame(mFrameNumber);
        emit(frameChanged(mFrameNumber));
    }
}

void PartWidget::updateAnimation(){
    mSecondsPassedSinceLastFrame += mPlaybackSpeedMultiplier*S_PER_UPDATE;
    bool updated = false;
    while (mSecondsPassedSinceLastFrame > mSPF){
        updated = true;
        mSecondsPassedSinceLastFrame -= mSPF;
        mFrameNumber = (mFrameNumber+1)%mPixmapItems.size();
    }
    if (updated){
        showFrame(mFrameNumber);
        emit(frameChanged(mFrameNumber));
    }
}

void PartWidget::partViewMousePressEvent(QMouseEvent *event){
    mLastPoint = event->pos();
    bool left = event->button()==Qt::LeftButton;
    bool right = event->button()==Qt::RightButton;
    bool middle = event->button()==Qt::MiddleButton;


    if (mScribbling && right && (mDrawToolType==kDrawToolPaint || mDrawToolType==kDrawToolEraser)){
        mScribbling = false;
        // Cancel
        mOverlayImage->fill(0x00FFFFFF);
        updateOverlay();
    }
    else if (left && mDrawToolType==kDrawToolPaint){
        drawLineTo(mLastPoint);
        mScribbling = true;
    }
    else if (left && mDrawToolType==kDrawToolEraser){
        eraseLineTo(mLastPoint);
        mScribbling = true;
    }
    else if (left && mDrawToolType==kDrawToolCopy){
        // eraseLineTo(mLastPoint);
        // record top point
        mTopLeftPoint = event->pos();
        mScribbling = true;

        QPointF pt = mPartView->mapToScene(event->pos().x(),event->pos().y());
        QPen pen = QPen(QColor(255,0,255), 0.1, Qt::DashLine);
        QBrush brush = Qt::NoBrush;
        mCopyRectItem = mPartView->scene()->addRect(floor(pt.x()),floor(pt.y()),1,1,pen,brush);
    }
    else if (left && mDrawToolType==kDrawToolFill){
        QPointF pt = mPartView->mapToScene(event->pos().x(),event->pos().y());
        QPoint pi(floor(pt.x()),floor(pt.y()));

        // Perform fill
        const QImage* img = mPart->modes[mModeName].images.at(mFrameNumber);

        if (pi.x()>=0 && pi.x()<img->width() && pi.y()>=0 && pi.y()<img->height()){
            QImage fillPattern = img->copy(); // (QSize(img->width(),img->height()), QImage::Format_ARGB32);
            // fillPattern.fill(0x00000000);

            QRgb targetColour = fillPattern.pixel(pi.x(),pi.y());
            QRgb replacementColour = mPenColour.rgba();

            if (targetColour!=replacementColour){
                QQueue<QPoint> q;
                q.enqueue(pi);
                while(!q.isEmpty()){
                    QPoint p = q.dequeue();
                   //  qDebug() << p;
                    if (p.x()>=0 && p.x()<fillPattern.width() && p.y()>=0 && p.y()<fillPattern.height()){
                        QRgb c = fillPattern.pixel(p);
                        if (c==targetColour){
                            fillPattern.setPixel(p, replacementColour);
                            q.enqueue(QPoint(p.x()+1,p.y()));
                            q.enqueue(QPoint(p.x()-1,p.y()));
                            q.enqueue(QPoint(p.x(),p.y()+1));
                            q.enqueue(QPoint(p.x(),p.y()-1));
                        }
                    }
                }

                DrawOnPartCommand* command = new DrawOnPartCommand(mPartName, mModeName, mFrameNumber, fillPattern, QPoint(0,0));
                if (command->ok) MainWindow::Instance()->undoStack()->push(command);
                else delete command;
            }
        }


    }
    else if ((left&&mDrawToolType==kDrawToolPickColour) || right){
        // select colour under pen
        QPointF pt = mPartView->mapToScene(event->pos().x(),event->pos().y());
        selectColourUnderPoint(pt);
    }
    else if (left&&mDrawToolType==kDrawToolStamp){
        // stamp
        if (mClipboardItem!=NULL){
            DrawOnPartCommand* command = new DrawOnPartCommand(mPartName, mModeName, mFrameNumber, mClipboardItem->pixmap().toImage().copy(),  mClipboardItem->pos().toPoint());
            if (command->ok)
                MainWindow::Instance()->undoStack()->push(command);
            else
                delete command;
        }
    }
    else if (middle){
        mLastCanvasPosition = mPosition;
        mMovingCanvas = true;
    }
}

void PartWidget::partViewMouseMoveEvent(QMouseEvent *event){
    // qDebug() << "moving..";
    mMousePos = event->pos();

    bool left = event->buttons()&Qt::LeftButton;
    bool right = event->buttons()&Qt::RightButton;
    bool middle = event->buttons()&Qt::MiddleButton;

    if (left && mScribbling){
        if (mDrawToolType==kDrawToolPaint){
            drawLineTo(mMousePos);
        }
        else if (mDrawToolType==kDrawToolEraser){
            eraseLineTo(mMousePos);
        }
        else if (mDrawToolType==kDrawToolCopy && mCopyRectItem!=NULL){
            QPointF pt = mPartView->mapToScene(event->pos().x(),event->pos().y());
            QRectF rect = mCopyRectItem->rect();
            int w = floor(pt.x()) - rect.x();
            int h = floor(pt.y()) - rect.y();
            mCopyRectItem->setRect(rect.x(),rect.y(),w,h);
        }
    }
    else if (right){

    }
    else if (middle && mMovingCanvas){
        QPoint diff = mLastPoint - mMousePos;
        setPosition(mLastCanvasPosition + QPointF(diff));
    }

    if (mClipboardItem!=NULL){
        // Set proper position (mapped and snapped)
        QPointF pt = mPartView->mapToScene(mMousePos.x(),mMousePos.y());
        QPointF snap = QPointF(floor(pt.x()),floor(pt.y()));
        mClipboardItem->setPos(snap);
    }
}

void PartWidget::partViewMouseReleaseEvent(QMouseEvent *event){
    if (event->button() == Qt::LeftButton && mScribbling) {
        mScribbling = false;
        if (mDrawToolType==kDrawToolPaint){
            drawLineTo(event->pos());

            // Create the drawIntoCommand and clear the image and pixmap item
            // TODO: clip the image to save space..
            // Can use Pixmap->boundingRect()...
            DrawOnPartCommand* command = new DrawOnPartCommand(mPartName, mModeName, mFrameNumber, *mOverlayImage, mOverlayPixmapItem->pos().toPoint());
            if (command->ok) MainWindow::Instance()->undoStack()->push(command);
            else delete command;

            mOverlayImage->fill(0x00FFFFFF);
            updateOverlay();
        }
        else if (mDrawToolType==kDrawToolEraser){
            eraseLineTo(event->pos());

            // TODO: Do the actual erasing with a command
            EraseOnPartCommand* command = new EraseOnPartCommand(mPartName, mModeName, mFrameNumber, *mOverlayImage, mOverlayPixmapItem->pos().toPoint());
            if (command->ok) MainWindow::Instance()->undoStack()->push(command);
            else delete command;

            mOverlayImage->fill(0x00FFFFFF);
            updateOverlay();
        }
        else if (mDrawToolType==kDrawToolCopy){
            // qDebug() << "Copied rect in image to clipboard";

            QRectF rect = mCopyRectItem->rect();
            const QImage* img = mPart->modes[mModeName].images.at(mFrameNumber);
            QImage subImg = img->copy(rect.x(),rect.y(),rect.width(),rect.height());
            if (subImg.isNull()){
                qDebug() << "Can't copy image region";
            }
            else {
                /*
                QMimeData* mimeData = new QMimeData();
                QByteArray data;
                QBuffer buffer(&data);
                buffer.open(QIODevice::WriteOnly);
                subImg.save(&buffer, "PNG");
                buffer.close();
                mimeData->setData("application/x-qt-windows-mime;value=\"PNG\"", data);
                QGuiApplication::clipboard()->setMimeData( mimeData );
                */

                // NB: This doesn't work with GIMP for some reason
                QGuiApplication::clipboard()->setImage(subImg);

                //QGuiApplication::clipboard()->setPixmap(QPixmap::fromImage(subImg));
            }
            // QGuiApplication::clipboard()->setImage(subImg, QClipboard::Selection);

            // delete rect
            if (mCopyRectItem!=NULL){
                mPartView->scene()->removeItem(mCopyRectItem);
                delete mCopyRectItem;
                mCopyRectItem = NULL;
            }
        }
    }
    else if (event->button()==Qt::MiddleButton && mMovingCanvas){
        mMovingCanvas = false;
    }
}

void PartWidget::partViewWheelEvent(QWheelEvent *event){
    if (event->modifiers()&Qt::ControlModifier){
        // qDebug() << "PartView::wheelEvent\t" << event->angleDelta();
        QPoint pt = event->angleDelta();
        if (pt.y()>0){
            // zoom in
            if (zoom()<48){
                // zoom in proportional to current zoom..
                int nz = std::min(48,zoom()*2);
                setZoom(nz);
                emit(zoomChanged());
            }
        }
        else if (pt.y()<0) {
            // zoom out
            if (zoom()>1){
                int nz = std::max(1, zoom()/2);
                setZoom(nz);
                emit(zoomChanged());
            }
        }
    }
}

void PartWidget::partViewKeyPressEvent(QKeyEvent *event){
    QPointF coordf = mPartView->mapToScene(mMousePos.x(),mMousePos.y());
    QPoint coord(floor(coordf.x()), floor(coordf.y()));

    bool updatedAnchorOrPivots = false;

    switch (event->key()){
    case Qt::Key_A: {
        updatedAnchorOrPivots = true;
        mAnchors.replace(mFrameNumber, coord);
        break;
    }
    case Qt::Key_1: {
        updatedAnchorOrPivots = true;
        if (mNumPivots>0) mPivots[0].replace(mFrameNumber, coord);
        break;
    }
    case Qt::Key_2: {
        updatedAnchorOrPivots = true;
        if (mNumPivots>1) mPivots[1].replace(mFrameNumber, coord);
        break;
    }
    case Qt::Key_3: {
        updatedAnchorOrPivots = true;
        if (mNumPivots>2) mPivots[2].replace(mFrameNumber, coord);
        break;
    }
    case Qt::Key_4: {
        updatedAnchorOrPivots = true;
        if (mNumPivots>3) mPivots[3].replace(mFrameNumber, coord);
        break;
    }
    case Qt::Key_E: {
        emit(selectNextMode());
        break;
    }
    case Qt::Key_W: {
        emit(selectPreviousMode());
        break;
    }
    case Qt::Key_D: {
        // next frame (and stop if playing)
        if (mIsPlaying) play(false);
        if (mFrameNumber >= (numFrames()-1)){
            setFrame(0);
        }
        else {
            setFrame(mFrameNumber+1);
        }
        break;
    }
    case Qt::Key_S: {
        if (mIsPlaying) stop();
        if (mFrameNumber > 0){
            setFrame(mFrameNumber-1);
        }
        else {
            setFrame(numFrames()-1);
        }
        break;
    }
    case Qt::Key_Space: {
        // Toggle play/pause
        if (mIsPlaying) stop();
        else play(true);

        emit (playActivated(mIsPlaying));
        break;
    }
    }

    if (updatedAnchorOrPivots){
        QPoint a = mAnchors.at(mFrameNumber);
        QPoint p1 = (mNumPivots>0)?mPivots[0].at(mFrameNumber):QPoint(0,0);
        QPoint p2 = (mNumPivots>1)?mPivots[1].at(mFrameNumber):QPoint(0,0);
        QPoint p3 = (mNumPivots>2)?mPivots[2].at(mFrameNumber):QPoint(0,0);
        QPoint p4 = (mNumPivots>3)?mPivots[3].at(mFrameNumber):QPoint(0,0);

        UpdateAnchorAndPivotsCommand* command = new UpdateAnchorAndPivotsCommand(mPartName, mModeName, mFrameNumber, a, p1, p2, p3, p4);
        if (command->ok){
            MainWindow::Instance()->undoStack()->push(command);
        }
        else {
            delete command;
        }
    }
}

void PartWidget::drawLineTo(const QPoint &endPoint)
{
    float offset = penSize()/2.;
    QPainter painter(mOverlayImage);
    painter.setPen(QPen(penColour(), penSize(), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));

    if (endPoint==mLastPoint){
        QPointF lastPointImageCoords = mPartView->mapToScene(mLastPoint.x()+offset,mLastPoint.y()+offset);
        lastPointImageCoords.setX(floor(lastPointImageCoords.x()));
        lastPointImageCoords.setY(floor(lastPointImageCoords.y()));
        painter.drawPoint(lastPointImageCoords);
    }
    else {
        // map the points
        QPointF lastPointImageCoords = mPartView->mapToScene(mLastPoint.x()+offset,mLastPoint.y()+offset);
        lastPointImageCoords.setX(floor(lastPointImageCoords.x()));
        lastPointImageCoords.setY(floor(lastPointImageCoords.y()));
        QPointF endPointImageCoords = mPartView->mapToScene(endPoint.x()+offset,endPoint.y()+offset);
        endPointImageCoords.setX(floor(endPointImageCoords.x()));
        endPointImageCoords.setY(floor(endPointImageCoords.y()));
        painter.drawLine(lastPointImageCoords, endPointImageCoords);
    }
    // modified
    updateOverlay();
    mLastPoint = endPoint;
}

void PartWidget::eraseLineTo(const QPoint &endPoint)
{
    float offset = penSize()/2.;
    QPainter painter(mOverlayImage);
    painter.setPen(QPen(QColor(255,0,255), penSize(), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    if (endPoint==mLastPoint){
        QPointF lastPointImageCoords = mPartView->mapToScene(mLastPoint.x()+offset,mLastPoint.y()+offset);
        lastPointImageCoords.setX(floor(lastPointImageCoords.x()));
        lastPointImageCoords.setY(floor(lastPointImageCoords.y()));
        painter.drawPoint(lastPointImageCoords);
    }
    else {
        // map the points
        QPointF lastPointImageCoords = mPartView->mapToScene(mLastPoint.x()+offset,mLastPoint.y()+offset);
        lastPointImageCoords.setX(floor(lastPointImageCoords.x()));
        lastPointImageCoords.setY(floor(lastPointImageCoords.y()));
        QPointF endPointImageCoords = mPartView->mapToScene(endPoint.x()+offset,endPoint.y()+offset);
        endPointImageCoords.setX(floor(endPointImageCoords.x()));
        endPointImageCoords.setY(floor(endPointImageCoords.y()));
        painter.drawLine(lastPointImageCoords, endPointImageCoords);
    }
    // modified
    updateOverlay();
    mLastPoint = endPoint;
}

void PartWidget::enterEvent(QEvent* ev){
    QMdiSubWindow::enterEvent(ev);
    // NB: don't do this, it's annoying
    // setFocus();
}

void PartWidget::selectColourUnderPoint(QPointF pt){
    int px = floor(pt.x());
    int py = floor(pt.y());
    QGraphicsPixmapItem* pi = mPixmapItems.at(mFrameNumber%mPixmapItems.size());

    bool inBounds = px>=0 || px<pi->pixmap().width() || py>=0 || py<pi->pixmap().height();
    if (pi!=NULL && inBounds){
        QImage img = pi->pixmap().toImage();
        QImage alpha = img.alphaChannel();
        QRgb rgb = img.pixel(pt.x(), pt.y());
        if (QColor(alpha.pixel(pt.x(),pt.y())).red()!=0){
            QColor colour(rgb);
            setPenColour(colour);
            setDrawToolType(kDrawToolPaint);
            emit(penChanged());
        }
        else {
            // Change to eraser
            setDrawToolType(kDrawToolEraser);
            emit(penChanged());
        }
    }
    else if (!inBounds){
        // Change to eraser
        setDrawToolType(kDrawToolEraser);
        emit(penChanged());
    }
}

void PartWidget::showFrame(int f){
    bool onion = mOnionSkinningEnabled && ((mIsPlaying&&mOnionSkinningEnabledDuringPlayback) || !mIsPlaying);
    bool pivots = mPivotsEnabled && ((mIsPlaying&&mPivotsEnabledDuringPlayback) || !mIsPlaying);

    for(int i=0;i<mPixmapItems.size();i++){
        QGraphicsPixmapItem* pi = mPixmapItems.at(i);
        pi->graphicsEffect()->setEnabled(false);
        pi->show();
        mAnchorItems.at(i)->hide();
        for(int p=0;p<MAX_PIVOTS;p++) mPivotItems[p].at(i)->hide();
        if (i==f){
            pi->setOpacity(1);
            pi->graphicsEffect()->setEnabled(true);
            if (pivots){
                mAnchorItems.at(i)->show();
                for(int p=0;p<mNumPivots;p++) mPivotItems[p].at(i)->show();
            }
        }
        else if (onion && ((i-f)==1 || (i-f)==-1)){
            pi->setOpacity(mOnionSkinningTransparency);
        }
        else if (onion && ((i-f)==2 || (i-f)==-2)){
            pi->setOpacity(mOnionSkinningTransparency/4);
        }
        else {
            pi->hide();
        }
    }
}


////////////////////////////////////////////////
// Part View
////////////////////////////////////////////////

PartView::PartView(PartWidget *parent, QGraphicsScene *scene)
    :QGraphicsView(scene, parent),
      pw(parent)
{
    setMouseTracking(true);
    // setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);

    setCursor(QCursor(QPixmap::fromImage(QImage(":/icon/icons/tool_none.png")),8,8));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // QLabel* label = new QLabel("widget", this);
    // label->move(2,0);
}

void PartView::scrollContentsBy(int sx, int sy)
{
    QGraphicsView::scrollContentsBy(sx,sy);
}

void PartView::mousePressEvent(QMouseEvent *event){
    pw->partViewMousePressEvent(event);
}

void PartView::mouseMoveEvent(QMouseEvent *event){
    pw->partViewMouseMoveEvent(event);
}

void PartView::mouseReleaseEvent(QMouseEvent *event){
    pw->partViewMouseReleaseEvent(event);
}

void PartView::wheelEvent(QWheelEvent *event){
    pw->partViewWheelEvent(event);
}

void PartView::keyPressEvent(QKeyEvent *event){
    pw->partViewKeyPressEvent(event);
}

void PartView::drawBackground(QPainter *painter, const QRectF &rect)
{
    QSettings settings;
    QColor backgroundColour = QColor(settings.value("background_colour", QColor(111,198,143).rgba()).toUInt());       
    painter->fillRect(rect, backgroundColour);

    // Calculate good colour for bounds rect
    QColor boundsColour = backgroundColour;
    int h = backgroundColour.hue();
    int s = backgroundColour.saturation();
    int val = backgroundColour.value();

    if (val < 50){
        val += 30;
    }
    else if (val < 255/2){
        val *= 1.2;
    }
    else if (val < (255/2 - 50)){
        val /= 1.2;
    }
    else {
        val -= 30;
    }
    boundsColour.setHsv(h,s,val);

    if (pw->mBoundsItem){
        QPen pen = pw->mBoundsItem->pen();
        pen.setColor(boundsColour);
        pw->mBoundsItem->setPen(pen);
    }

    /*
    float sc = 1.1;
    QRectF sr = this->sceneRect();
    QPointF center = sr.center();
    sr.setWidth(sr.width()*sc);
    sr.setHeight(sr.height()*sc);
    sr.moveCenter(center - pw->mPosition/pw->mZoom);
    painter->fillRect(sr, darker);
    painter->fillRect(this->sceneRect().translated(-pw->mPosition/pw->mZoom), backgroundColour);

    QRectF textRect = this->sceneRect().translated(-pw->mPosition/pw->mZoom);
    textRect.translate(0, -textRect.height());
    // textRect.setHeight(12.f);
    // painter->setPen(QColor("white"));
    painter->setPen(backgroundColour.lighter());
    */

    /*
    QFont font;
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(ceil(12.f/pw->mZoom));
    // font.setPointSizeF(12.f/pw->mZoom);
    painter->setFont(font);
    // painter->drawText(textRect, Qt::AlignLeft, pw->mPartName);
    painter->drawText(textRect.bottomLeft(), pw->mPartName);
    */
}