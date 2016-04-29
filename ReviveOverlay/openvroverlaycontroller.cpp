//====== Copyright Valve Corporation, All rights reserved. =======


#include "openvroverlaycontroller.h"


#include <QOpenGLFramebufferObjectFormat>
#include <QMouseEvent>
#include <QCursor>
#include <QCoreApplication>

using namespace vr;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
COpenVROverlayController *s_pSharedVRController = NULL;

COpenVROverlayController *COpenVROverlayController::SharedInstance()
{
	if ( !s_pSharedVRController )
	{
		s_pSharedVRController = new COpenVROverlayController();
	}
	return s_pSharedVRController;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
COpenVROverlayController::COpenVROverlayController()
	: BaseClass()
	, m_eLastHmdError( vr::VRInitError_None )
	, m_eCompositorError( vr::VRInitError_None )
	, m_eOverlayError( vr::VRInitError_None )
	, m_strVRDriver( "No Driver" )
	, m_strVRDisplay( "No Display" )
	, m_pOpenGLContext( NULL )
	, m_pRenderControl( NULL )
	, m_pOffscreenSurface ( NULL )
	, m_pFbo( NULL )
	, m_pWindow( NULL )
	, m_pPumpEventsTimer( NULL )
	, m_lastMouseButtons( 0 )
	, m_ulOverlayHandle( vr::k_ulOverlayHandleInvalid )
	, m_bManualMouseHandling( false )
{
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
COpenVROverlayController::~COpenVROverlayController()
{
}


//-----------------------------------------------------------------------------
// Purpose: Helper to get a string from a tracked device property and turn it
//			into a QString
//-----------------------------------------------------------------------------
QString GetTrackedDeviceString( vr::IVRSystem *pHmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop )
{
	char buf[128];
	vr::TrackedPropertyError err;
	pHmd->GetStringTrackedDeviceProperty( unDevice, prop, buf, sizeof( buf ), &err );
	if( err != vr::TrackedProp_Success )
	{
		return QString( "Error Getting String: " ) + pHmd->GetPropErrorNameFromEnum( err );
	}
	else
	{
		return buf;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool COpenVROverlayController::Init()
{
	bool bSuccess = true;

	m_strName = "systemoverlay";

	QStringList arguments = qApp->arguments();

	int nNameArg = arguments.indexOf( "-name" );
	if( nNameArg != -1 && nNameArg + 2 <= arguments.size() )
	{
		m_strName = arguments.at( nNameArg + 1 );
	}

	QSurfaceFormat format;
	format.setMajorVersion( 4 );
	format.setMinorVersion( 1 );
	format.setProfile( QSurfaceFormat::CompatibilityProfile );

	m_pOpenGLContext = new QOpenGLContext();
	m_pOpenGLContext->setFormat( format );
	bSuccess = m_pOpenGLContext->create();
	if( !bSuccess )
		return false;

	// create an offscreen surface to attach the context and FBO to
	m_pOffscreenSurface = new QOffscreenSurface();
	m_pOffscreenSurface->create();
	m_pOpenGLContext->makeCurrent( m_pOffscreenSurface );

	m_pRenderControl = new QQuickRenderControl();
	connect( m_pRenderControl, &QQuickRenderControl::sceneChanged, this, &COpenVROverlayController::OnSceneChanged);

	// Loading the OpenVR Runtime
	bSuccess = ConnectToVRRuntime();

	bSuccess = bSuccess && vr::VRCompositor() != NULL;

	if( vr::VROverlay() )
	{
		std::string sKey = std::string( "sample." ) + m_strName.toStdString();
		vr::VROverlayError overlayError = vr::VROverlay()->CreateDashboardOverlay( sKey.c_str(), m_strName.toStdString().c_str(), &m_ulOverlayHandle, &m_ulOverlayThumbnailHandle );
		bSuccess = bSuccess && overlayError == vr::VROverlayError_None;
	}

	if( bSuccess )
	{
		vr::VROverlay()->SetOverlayWidthInMeters( m_ulOverlayHandle, 1.5f );
		vr::VROverlay()->SetOverlayInputMethod( m_ulOverlayHandle, vr::VROverlayInputMethod_Mouse );
	
		m_pPumpEventsTimer = new QTimer( this );
		connect(m_pPumpEventsTimer, SIGNAL( timeout() ), this, SLOT( OnTimeoutPumpEvents() ) );
		m_pPumpEventsTimer->setInterval( 20 );
		m_pPumpEventsTimer->start();

	}
	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COpenVROverlayController::Shutdown()
{
	DisconnectFromVRRuntime();

	delete m_pRenderControl;
	delete m_pFbo;
	delete m_pOffscreenSurface;

	if( m_pOpenGLContext )
	{
//		m_pOpenGLContext->destroy();
		delete m_pOpenGLContext;
		m_pOpenGLContext = NULL;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COpenVROverlayController::OnSceneChanged()
{
	// skip rendering if the overlay isn't visible
	if( !vr::VROverlay() ||
		!vr::VROverlay()->IsOverlayVisible( m_ulOverlayHandle ) && !vr::VROverlay()->IsOverlayVisible( m_ulOverlayThumbnailHandle ) )
		return;

	m_pOpenGLContext->makeCurrent( m_pOffscreenSurface );
	m_pFbo->bind();

	m_pRenderControl->render();

	m_pFbo->release();

	GLuint unTexture = m_pFbo->texture();
	if( unTexture != 0 )
	{
		vr::Texture_t texture = {(void*)unTexture, vr::API_OpenGL, vr::ColorSpace_Auto };
		vr::VROverlay()->SetOverlayTexture( m_ulOverlayHandle, &texture );
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COpenVROverlayController::OnTimeoutPumpEvents()
{
	if( !vr::VRSystem() )
		return;


	if( m_bManualMouseHandling )
	{
		// tell OpenVR to make some events for us
		for( vr::TrackedDeviceIndex_t unDeviceId = 1; unDeviceId < vr::k_unControllerStateAxisCount; unDeviceId++ )
		{
			if( vr::VROverlay()->HandleControllerOverlayInteractionAsMouse( m_ulOverlayHandle, unDeviceId ) )
			{
				break;
			}
		}
	}

	vr::VREvent_t vrEvent;
	while( vr::VROverlay()->PollNextOverlayEvent( m_ulOverlayHandle, &vrEvent, sizeof( vrEvent )  ) )
	{
		switch( vrEvent.eventType )
		{
		case vr::VREvent_MouseMove:
			{
				QPointF ptNewMouse( vrEvent.data.mouse.x, vrEvent.data.mouse.y );
				QPoint ptGlobal = ptNewMouse.toPoint();
				QMouseEvent mouseEvent( QEvent::MouseMove,
										ptNewMouse,
										ptGlobal,
										Qt::NoButton,
										m_lastMouseButtons,
										0 );

				m_ptLastMouse = ptNewMouse;
				QCoreApplication::sendEvent( m_pWindow, &mouseEvent );

				OnSceneChanged();
			}
			break;

		case vr::VREvent_MouseButtonDown:
			{
				Qt::MouseButton button = vrEvent.data.mouse.button == vr::VRMouseButton_Right ? Qt::RightButton : Qt::LeftButton;

				m_lastMouseButtons |= button;

				QPoint ptGlobal = m_ptLastMouse.toPoint();
				QMouseEvent mouseEvent( QEvent::MouseButtonPress,
										m_ptLastMouse,
										ptGlobal,
										button,
										m_lastMouseButtons,
										0 );

				QCoreApplication::sendEvent( m_pWindow, &mouseEvent );
			}
			break;

		case vr::VREvent_MouseButtonUp:
			{
				Qt::MouseButton button = vrEvent.data.mouse.button == vr::VRMouseButton_Right ? Qt::RightButton : Qt::LeftButton;
				m_lastMouseButtons &= ~button;

				QPoint ptGlobal = m_ptLastMouse.toPoint();
				QMouseEvent mouseEvent( QEvent::MouseButtonRelease,
										m_ptLastMouse,
										ptGlobal,
										button,
										m_lastMouseButtons,
										0 );

				QCoreApplication::sendEvent( m_pWindow, &mouseEvent );
			}
			break;

		case vr::VREvent_OverlayShown:
			{
				m_pWindow->update();
			}
			break;

		case vr::VREvent_Quit:
			QCoreApplication::exit();
			break;
		}
	}

	if( m_ulOverlayThumbnailHandle != vr::k_ulOverlayHandleInvalid )
	{
		while( vr::VROverlay()->PollNextOverlayEvent( m_ulOverlayThumbnailHandle, &vrEvent, sizeof( vrEvent)  ) )
		{
			switch( vrEvent.eventType )
			{
			case vr::VREvent_OverlayShown:
				{
					m_pWindow->update();
				}
				break;
			}
		}
	}

}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COpenVROverlayController::SetWindow( QQuickWindow *pWindow )
{
	/*if( m_pRenderControl )
	{
		// all of the mouse handling stuff requires that the widget be at 0,0
		pWidget->move( 0, 0 );
		m_pScene->addWidget( pWidget );
	}*/
	m_pWindow = pWindow;

	m_pFbo = new QOpenGLFramebufferObject( pWindow->width(), pWindow->height(), GL_TEXTURE_2D );
	pWindow->setRenderTarget(m_pFbo);

	if( vr::VROverlay() )
	{
		vr::HmdVector2_t vecWindowSize =
		{
			(float)pWindow->width(),
			(float)pWindow->height()
		};
		vr::VROverlay()->SetOverlayMouseScale( m_ulOverlayHandle, &vecWindowSize );
	}

}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool COpenVROverlayController::ConnectToVRRuntime()
{
	m_eLastHmdError = vr::VRInitError_None;
	vr::IVRSystem *pVRSystem = vr::VR_Init( &m_eLastHmdError, vr::VRApplication_Overlay );

	if ( m_eLastHmdError != vr::VRInitError_None )
	{
		m_strVRDriver = "No Driver";
		m_strVRDisplay = "No Display";
		return false;
	}

	m_strVRDriver = GetTrackedDeviceString(pVRSystem, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String);
	m_strVRDisplay = GetTrackedDeviceString(pVRSystem, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String);

	return true;
}


void COpenVROverlayController::DisconnectFromVRRuntime()
{
	vr::VR_Shutdown();
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString COpenVROverlayController::GetVRDriverString()
{
	return m_strVRDriver;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString COpenVROverlayController::GetVRDisplayString()
{
	return m_strVRDisplay;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool COpenVROverlayController::BHMDAvailable()
{
	return vr::VRSystem() != NULL;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

vr::HmdError COpenVROverlayController::GetLastHmdError()
{
	return m_eLastHmdError;
}


