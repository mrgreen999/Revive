#include "openvroverlaycontroller.h"
#include <QGuiApplication>
#include <QQuickView>

int main(int argc, char *argv[])
{
	QGuiApplication a(argc, argv);
	QQuickView *view = new QQuickView;
	view->setSource(QUrl::fromLocalFile("Overlay.qml"));

	COpenVROverlayController::SharedInstance()->Init();

	COpenVROverlayController::SharedInstance()->SetWindow( view );

	// don't show the window that you're going display in an overlay
	//view.show();

	return a.exec();
}
