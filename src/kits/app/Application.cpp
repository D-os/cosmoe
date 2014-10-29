//------------------------------------------------------------------------------
//	Copyright (c) 2001-2004, Haiku, inc.
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		Application.cpp
//	Author:			Erik Jaesler (erik@cgsoftware.com)
//	Description:	BApplication class is the center of the application
//					universe.  The global be_app and be_app_messenger 
//					variables are defined here as well.
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------
#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// System Includes -------------------------------------------------------------
#include <AppFileInfo.h>
#include <Application.h>
#include <AppMisc.h>
#include <Cursor.h>
#include <Entry.h>
#include <File.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <RegistrarDefs.h>
#include <Resources.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <Window.h>

#include <kernel.h>
#include <View.h>
#include <String.h>
#include <errno.h>
#include <assert.h>


// Project Includes ------------------------------------------------------------
#include <LooperList.h>
#include <ObjectLocker.h>
#include <AppServerLink.h>
#include <ServerProtocol.h>
#include <PortLink.h>
#include <ServerProtocol.h>


using namespace std;

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------
BApplication*	be_app = NULL;
BMessenger		be_app_messenger;

BResources*	BApplication::_app_resources = NULL;
BLocker		BApplication::_app_resources_lock("_app_resources_lock");


static property_info
sPropertyInfo[] = {
	{
		"Window",
			{},
			{B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER},
			NULL, 0,
			{},
			{},
			{}
	},
	{
		"Window",
			{},
			{B_NAME_SPECIFIER},
			NULL, 1,
			{},
			{},
			{}
	},
	{
		"Looper",
			{},
			{B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER},
			NULL, 2,
			{},
			{},
			{}
	},
	{
		"Looper",
			{},
			{B_ID_SPECIFIER},
			NULL, 3,
			{},
			{},
			{}
	},
	{
		"Looper",
			{},
			{B_NAME_SPECIFIER},
			NULL, 4,
			{},
			{},
			{}
	},
	{
		"Name",
			{B_GET_PROPERTY},
			{B_DIRECT_SPECIFIER},
			NULL, 5,
			{B_STRING_TYPE},
			{},
			{}
	},
	{
		"Window",
			{B_COUNT_PROPERTIES},
			{B_DIRECT_SPECIFIER},
			NULL, 5,
			{B_INT32_TYPE},
			{},
			{}
	},
	{
		"Loopers",
			{B_GET_PROPERTY},
			{B_DIRECT_SPECIFIER},
			NULL, 5,
			{B_MESSENGER_TYPE},
			{},
			{}
	},
	{
		"Windows",
			{B_GET_PROPERTY},
			{B_DIRECT_SPECIFIER},
			NULL, 5,
			{B_MESSENGER_TYPE},
			{},
			{}
	},
	{
		"Looper",
			{B_COUNT_PROPERTIES},
			{B_DIRECT_SPECIFIER},
			NULL, 5,
			{B_INT32_TYPE},
			{},
			{}
	},
	{}
};

// argc/argv
extern const int __libc_argc;
extern const char * const *__libc_argv;

class BMenuWindow : public BWindow
{
public:
	BMenuWindow() :
		BWindow(BRect(), "Menu", B_NO_BORDER_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | WND_SYSTEM)
	{
	}

	virtual ~BMenuWindow() {}

	virtual void WindowActivated(bool active) {}
};
//------------------------------------------------------------------------------

// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


// prototypes of helper functions
static const char* looper_name_for(const char *signature);
static status_t check_app_signature(const char *signature);
static void fill_argv_message(BMessage *message);

//------------------------------------------------------------------------------
BApplication::BApplication(const char* signature)
			: BLooper(looper_name_for(signature)),
			  fAppName(NULL),
			  fServerFrom(-1),
			  fServerTo(-1),
			  fServerHeap(NULL),
			  fPulseRate(500000),
			  fInitialWorkspace(0),
			  fDraggedMessage(NULL),
			  fPulseRunner(NULL),
			  fInitError(B_NO_INIT),
			  fReadyToRunCalled(false)
{
	InitData(signature, NULL);
}
//------------------------------------------------------------------------------
BApplication::BApplication(const char* signature, status_t* error)
			: BLooper(looper_name_for(signature)),
			  fAppName(NULL),
			  fServerFrom(-1),
			  fServerTo(-1),
			  fServerHeap(NULL),
			  fPulseRate(500000),
			  fInitialWorkspace(0),
			  fDraggedMessage(NULL),
			  fPulseRunner(NULL),
			  fInitError(B_NO_INIT),
			  fReadyToRunCalled(false)
{
	InitData(signature, error);
}
//------------------------------------------------------------------------------
BApplication::~BApplication()
{
	// tell all loopers(usually windows) to quit. Also, wait for them.
	
	// TODO: As Axel suggested, this functionality should probably be moved
	// to quit_all_windows(), and that function should be called from both 
	// here and QuitRequested().

#if 0
	BWindow*	window = NULL;
	BList		looperList;
	{
		using namespace BPrivate;
		BObjectLocker<BLooperList> ListLock(gLooperList);
		if (ListLock.IsLocked())
			gLooperList.GetLooperList(&looperList);
	}

	for (int32 i = 0; i < looperList.CountItems(); i++)
	{
		window	= dynamic_cast<BWindow*>((BLooper*)looperList.ItemAt(i));
		if (window)
		{
			window->Lock();
			window->Quit();
		}
	}
#else
	Lock();
	for ( int i = 0 ; i < count_windows(true) ; ++i )
	{
		window_at(i, true)->PostMessage( B_QUIT_REQUESTED );
	}

	// Give the windows some time to get rid of themselves
	while ( count_windows(true) > 0 )
	{
		Unlock();
		snooze( 2000 );
		Lock();
	}
#endif

	// unregister from the roster
	BRoster::Private().RemoveApp(Team());

#ifndef RUN_WITHOUT_APP_SERVER
	if ( write_port( m_hSrvAppPort, B_QUIT_REQUESTED, NULL, 0 ) < 0 )
	{
		printf( "Error: BApplication::~BApplication() failed to send B_QUIT_REQUESTED request to server\n" );
	}
#endif	// RUN_WITHOUT_APP_SERVER

	delete_port( fServerTo );

	// uninitialize be_app and be_app_messenger
	be_app = NULL;
	
	// R5 doesn't uninitialize be_app_messenger.
	//be_app_messenger = BMessenger();

	Unlock();
}
//------------------------------------------------------------------------------
BApplication::BApplication(BMessage* data)
			// Note: BeOS calls the private BLooper(int32, port_id, const char *)
			// constructor here, test if it's needed
			: BLooper(looper_name_for(NULL)),
			  fAppName(NULL),
			  fServerFrom(-1),
			  fServerTo(-1),
			  fServerHeap(NULL),
			  fPulseRate(500000),
			  fInitialWorkspace(0),
			  fDraggedMessage(NULL),
			  fPulseRunner(NULL),
			  fInitError(B_NO_INIT),
			  fReadyToRunCalled(false)
{
	const char *signature = NULL;	
	data->FindString("mime_sig", &signature);
	
	InitData(signature, NULL);

	bigtime_t pulseRate;
	if (data->FindInt64("_pulse", &pulseRate) == B_OK)
		SetPulseRate(pulseRate);

}
//------------------------------------------------------------------------------
BArchivable* BApplication::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BApplication"))
		return new BApplication(data);
	
	return NULL;	
}
//------------------------------------------------------------------------------
status_t
BApplication::Archive(BMessage* data, bool deep) const
{
	status_t status = BLooper::Archive(data, deep);
	if (status < B_OK)
		return status;

	app_info info;
	status = GetAppInfo(&info);
	if (status < B_OK)
		return status;

	status = data->AddString("mime_sig", info.signature);
	if (status < B_OK)
		return status;
	
	status = data->AddInt64("_pulse", fPulseRate);

	return status;
}
//------------------------------------------------------------------------------
status_t BApplication::InitCheck() const
{
	return fInitError;
}
//------------------------------------------------------------------------------
thread_id BApplication::Run()
{
	AssertLocked();

	if (fRunCalled)	
		debugger("BApplication::Run was already called. Can only be called once.");

	// Note: We need a local variable too (for the return value), since
	// fTaskID is cleared by Quit().

	thread_id thread = fTaskID = find_thread(NULL);

	if (fMsgPort < B_OK)
		return fMsgPort;

	fRunCalled = true;

	run_task();

	return thread;
}
//------------------------------------------------------------------------------
void BApplication::Quit()
{
	bool unlock = false;
	if (!IsLocked()) {
		const char* name = Name();
		if (!name)
			name = "no-name";
		printf("ERROR - you must Lock the application object before calling "
			   "Quit(), team=%ld, looper=%s\n", Team(), name);
		unlock = true;
		if (!Lock())
			return;
	}
	// Delete the object, if not running only.
	if (!fRunCalled) {
		delete this;
	} else if (find_thread(NULL) != fTaskID) {
		// We are not the looper thread.
		// We push a _QUIT_ into the queue.
		// TODO: When BLooper::AddMessage() is done, use that instead of
		// PostMessage()??? This would overtake messages that are still at
		// the port.
		// NOTE: We must not unlock here -- otherwise we had to re-lock, which
		// may not work. This is bad, since, if the port is full, it
		// won't get emptier, as the looper thread needs to lock the object
		// before dispatching messages.
		while (PostMessage(_QUIT_, this) == B_WOULD_BLOCK)
			snooze(10000);
	} else {
		// We are the looper thread.
		// Just set fTerminating to true which makes us fall through the
		// message dispatching loop and return from Run().
		fTerminating = true;
	}
	// If we had to lock the object, unlock now.
	if (unlock)
		Unlock();
}
//------------------------------------------------------------------------------
bool BApplication::QuitRequested()
{
	// No windows -- nothing to do.
	// TODO: Au contraire, we can have opened windows, and we have
	// to quit them.
	
	return BLooper::QuitRequested();
}
//------------------------------------------------------------------------------
void BApplication::Pulse()
{
}
//------------------------------------------------------------------------------
void BApplication::ReadyToRun()
{
}
//------------------------------------------------------------------------------
void
BApplication::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case B_COUNT_PROPERTIES:
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		{
			int32 index;
			BMessage specifier;
			int32 what;
			const char *property = NULL;
			bool scriptHandled = false;
			if (message->GetCurrentSpecifier(&index, &specifier, &what, &property) == B_OK) {
				if (ScriptReceived(message, index, &specifier, what, property))
					scriptHandled = true;
			}
			if (!scriptHandled)
				BLooper::MessageReceived(message);
			break;
		}
		
		// Bebook says: B_SILENT_RELAUNCH
		// Sent to a single-launch application when it's activated by being launched
		// (for example, if the user double-clicks its icon in Tracker).
		case B_SILENT_RELAUNCH:
			//be_roster->ActivateApp(Team());
			// supposed to fall through	
		default:
			BLooper::MessageReceived(message);
			break;
	}
	
}
//------------------------------------------------------------------------------
void BApplication::ArgvReceived(int32 argc, char** argv)
{
}
//------------------------------------------------------------------------------
void BApplication::AppActivated(bool active)
{
}
//------------------------------------------------------------------------------
void BApplication::RefsReceived(BMessage* a_message)
{
}
//------------------------------------------------------------------------------
void BApplication::AboutRequested()
{
}
//------------------------------------------------------------------------------
BHandler* BApplication::ResolveSpecifier(BMessage* msg, int32 index,
										 BMessage* specifier, int32 form,
										 const char* property)
{
	return NULL; // TODO: implement? not implemented?
}
//------------------------------------------------------------------------------
void BApplication::ShowCursor()
{
	BMessage cReq(AS_SHOW_CURSOR);
	BMessage cReply;

	BMessenger(m_hSrvAppPort).SendMessage(&cReq, &cReply);
}
//------------------------------------------------------------------------------
void BApplication::HideCursor()
{
	BMessage cReq(AS_HIDE_CURSOR);
	BMessage cReply;

	BMessenger(m_hSrvAppPort).SendMessage(&cReq, &cReply);
}
//------------------------------------------------------------------------------
void BApplication::ObscureCursor()
{
	BMessage cReq(AS_OBSCURE_CURSOR);
	BMessage cReply;

	BMessenger(m_hSrvAppPort).SendMessage(&cReq, &cReply);
}
//------------------------------------------------------------------------------
bool BApplication::IsCursorHidden() const
{
	BMessage cReq(AS_QUERY_CURSOR_HIDDEN);
	BMessage cReply;
	bool hidden;

	BMessenger(m_hSrvAppPort).SendMessage(&cReq, &cReply);
	cReply.FindBool( "hidden", &hidden );

	return hidden;
}
//------------------------------------------------------------------------------
void BApplication::SetCursor(const void* cursor)
{
	// BeBook sez: If you want to call SetCursor() without forcing an immediate
	//				sync of the Application Server, you have to use a BCursor.
	// By deductive reasoning, this function forces a sync. =)
	BCursor Cursor(cursor);
	SetCursor(&Cursor, true);
}
//------------------------------------------------------------------------------
void BApplication::SetCursor(const BCursor* cursor, bool sync)
{
	BMessage cReq(AS_SET_CURSOR_BCURSOR);
	BMessage cReply;

	cReq.AddBool("sync", sync);
	cReq.AddInt32("token", cursor->m_serverToken);
	BMessenger(m_hSrvAppPort).SendMessage(&cReq, &cReply);
	// no reply expected
}
//------------------------------------------------------------------------------
int32 BApplication::CountWindows() const
{
	// BeBook sez: The windows list includes all windows explicitely created by
	//				the app ... but excludes private windows create by Be
	//				classes.
	// I'm taking this to include private menu windows, thus the incl_menus
	// param is false.
	return count_windows(false);
}
//------------------------------------------------------------------------------
BWindow* BApplication::WindowAt(int32 index) const
{
	// BeBook sez: The windows list includes all windows explicitely created by
	//				the app ... but excludes private windows create by Be
	//				classes.
	// I'm taking this to include private menu windows, thus the incl_menus
	// param is false.
	return window_at(index, false);
}
//------------------------------------------------------------------------------
int32 BApplication::CountLoopers() const
{
	using namespace BPrivate;
	BObjectLocker<BLooperList> ListLock(gLooperList);
	if (ListLock.IsLocked())
	{
		return gLooperList.CountLoopers();
	}

	return B_ERROR;	// Some bad, non-specific thing has happened
}
//------------------------------------------------------------------------------
BLooper* BApplication::LooperAt(int32 index) const
{
	using namespace BPrivate;
	BLooper* Looper = NULL;
	BObjectLocker<BLooperList> ListLock(gLooperList);
	if (ListLock.IsLocked())
	{
		Looper = gLooperList.LooperAt(index);
	}

	return Looper;
}
//------------------------------------------------------------------------------
bool BApplication::IsLaunching() const
{
	return !fReadyToRunCalled;
}
//------------------------------------------------------------------------------
status_t BApplication::GetAppInfo(app_info* info) const
{
	return be_roster->GetRunningAppInfo(be_app->Team(), info);
}
//------------------------------------------------------------------------------
BResources* BApplication::AppResources()
{
	return NULL;	// not implemented
}
//------------------------------------------------------------------------------
void
BApplication::DispatchMessage(BMessage* message, BHandler* handler)
{
	switch (message->what) {
		case B_ARGV_RECEIVED:
			do_argv(message);
			break;

		case B_REFS_RECEIVED:
			RefsReceived(message);
			break;
		case B_READY_TO_RUN:
			ReadyToRun();
			fReadyToRunCalled = true;
			break;
		
		case B_ABOUT_REQUESTED:
			AboutRequested();
			break;
		
		// TODO: Handle these as well
		/*
		// These two are handled by BTextView, don't know if also
		// by other classes
		case _DISPOSE_DRAG_: 
		case _PING_:
		
		case _SHOW_DRAG_HANDLES_:
		case B_QUIT_REQUESTED:
			
			break;
		*/
		default:
			BLooper::DispatchMessage(message, handler);
			break;
	}
}
//------------------------------------------------------------------------------
void BApplication::SetPulseRate(bigtime_t rate)
{
	fPulseRate = rate;
}
//------------------------------------------------------------------------------
status_t BApplication::GetSupportedSuites(BMessage* data)
{
	status_t err = B_OK;
	if (!data)
	{
		err = B_BAD_VALUE;
	}

	if (!err)
	{
		err = data->AddString("Suites", "suite/vnd.Be-application");
		if (!err)
		{
			BPropertyInfo PropertyInfo(sPropertyInfo);
			err = data->AddFlat("message", &PropertyInfo);
			if (!err)
			{
				err = BHandler::GetSupportedSuites(data);
			}
		}
	}

	return err;
}
//------------------------------------------------------------------------------
status_t BApplication::Perform(perform_code d, void* arg)
{
	return BLooper::Perform(d, arg);
}
//------------------------------------------------------------------------------
BApplication::BApplication(uint32 signature)
{
}
//------------------------------------------------------------------------------
BApplication::BApplication(const BApplication& rhs)
{
}
//------------------------------------------------------------------------------
BApplication& BApplication::operator=(const BApplication& rhs)
{
	return *this;
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication1()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication2()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication3()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication4()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication5()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication6()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication7()
{
}
//------------------------------------------------------------------------------
void BApplication::_ReservedApplication8()
{
}
//------------------------------------------------------------------------------
bool
BApplication::ScriptReceived(BMessage *message, int32 index, BMessage *specifier,
							int32 what, const char *property)
{
	// TODO: Implement
	printf("message:\n");
	message->PrintToStream();
	printf("index: %ld\n", index);
	printf("specifier:\n");
	specifier->PrintToStream();
	printf("what: %ld\n", what);
	printf("property: %s\n", property ? property : "");
	return false;
}
//------------------------------------------------------------------------------
void BApplication::run_task()
{
	task_looper();
}
//------------------------------------------------------------------------------
void BApplication::InitData(const char* signature, status_t* error)
{
	// check whether there exists already an application
	if (be_app)
		debugger("2 BApplication objects were created. Only one is allowed.");
	// check signature
	fInitError = check_app_signature(signature);
	fAppName = signature;

#ifndef RUN_WITHOUT_REGISTRAR
	bool isRegistrar
		= (signature && !strcasecmp(signature, kRegistrarSignature));
	// get team and thread
	team_id team = Team();
	thread_id thread = BPrivate::main_thread_for(team);
#endif
	// get app executable ref
	entry_ref ref;
	// get the BAppFileInfo and extract the information we need
	uint32 appFlags = B_REG_DEFAULT_APP_FLAGS;
#ifndef RUN_WITHOUT_REGISTRAR
	// check whether be_roster is valid
	if (fInitError == B_OK && !isRegistrar
		&& !BRoster::Private().IsMessengerValid(false)) {
		printf("FATAL: be_roster is not valid. Is the registrar running?\n");
		fInitError = B_NO_INIT;
	}

	// check whether or not we are pre-registered
	app_info appInfo;
	if (fInitError == B_OK) {
		// not pre-registered -- try to register the application
		team_id otherTeam = -1;
		// the registrar must not register
		if (!isRegistrar) {
			/*fInitError =*/ BRoster::Private().AddApplication(signature, &ref,
				appFlags, team, thread, fMsgPort, true, NULL, &otherTeam);
		}
		if (fInitError == B_OK) {
			// the registrations was successful
			// Create a B_ARGV_RECEIVED message and send it to ourselves.
			// Do that even, if we are B_ARGV_ONLY.
			// TODO: When BLooper::AddMessage() is done, use that instead of
			// PostMessage().
			//if (__libc_argc > 1) {
			//	BMessage argvMessage(B_ARGV_RECEIVED);
			//	do_argv(&argvMessage);
			//	PostMessage(&argvMessage, this);
			//}
			// send a B_READY_TO_RUN message as well
			PostMessage(B_READY_TO_RUN, this);
		} else if (fInitError > B_ERRORS_END) {
			// Registrar internal errors shouldn't fall into the user's hands.
			fInitError = B_ERROR;
		}
	}
#endif	// ifdef RUN_WITHOUT_REGISTRAR

	// TODO: Not completely sure about the order, but this should be close.
	
#ifndef RUN_WITHOUT_APP_SERVER
	// An app_server connection is necessary for a lot of stuff, so get that first.
printf("1: fInitError = %ld\n", fInitError);
	if (fInitError == B_OK)
		connect_to_app_server();
printf("2: fInitError = %ld\n", fInitError);
	if (fInitError == B_OK)
		setup_server_heaps();
printf("3: fInitError = %ld\n", fInitError);
	if (fInitError == B_OK)
		get_scs();
printf("4: fInitError = %ld\n", fInitError);
#endif	// RUN_WITHOUT_APP_SERVER

	// init be_app and be_app_messenger
	if (fInitError == B_OK) {
		be_app = this;
		be_app_messenger = BMessenger(NULL, this);
	}
	
printf("5: be_app and be_app_messenger\n");
	
#ifndef RUN_WITHOUT_APP_SERVER
	// Initialize the IK after we have set be_app because of a construction of a
	// BAppServerLink (which depends on be_app) nested inside the call to get_menu_info.
	if (fInitError == B_OK)
		fInitError = _init_interface_kit_();
#endif	// RUN_WITHOUT_APP_SERVER
	// set the BHandler's name

printf("6: _init_interface_kit_\n");
	
	// create meta MIME
	if (fInitError == B_OK) {
	}

#ifndef RUN_WITHOUT_APP_SERVER
	// create global system cursors
	// ToDo: these could have a predefined server token to safe the communication!
	B_CURSOR_SYSTEM_DEFAULT = new BCursor(B_HAND_CURSOR);
	B_CURSOR_I_BEAM = new BCursor(B_I_BEAM_CURSOR);
#endif	// RUN_WITHOUT_APP_SERVER

printf("7: cursors created\n");
	
	// Return the error or exit, if there was an error and no error variable
	// has been supplied.
	if (error)
		*error = fInitError;
	else if (fInitError != B_OK)
	{
		printf("App could not initialize due to error (%ld)\n", fInitError);
		fflush(NULL);
		exit(0);
	}
	
printf("8: fInitError = %ld\n", fInitError);
}
//------------------------------------------------------------------------------

int BApplication::GetScreenModeCount()
{
	BMessage cReq( AR_GET_SCREENMODE_COUNT );
	BMessage cReply;

	BMessenger( m_hSrvAppPort ).SendMessage( &cReq, &cReply );

	int32 nCount;

	cReply.FindInt32( "count", &nCount );

	return( nCount );
}


int BApplication::GetDefaultFont( const std::string& cName, font_properties* psProps ) const
{
	BMessage cReq( DR_GET_DEFAULT_FONT );
	BMessage cReply;

	cReq.AddString( "config_name", cName.c_str() );
	if ( BMessenger(fServerFrom).SendMessage( &cReq, &cReply ) >= 0 )
	{
		int32 nError = -EINVAL;
		cReply.FindInt32( "error", &nError );
		if ( nError < 0 )
		{
			errno = -nError;
			return( -1 );
		}

		if ( psProps != NULL )
		{
			BString aString;
			cReply.FindString( "family", &aString );
			psProps->m_cFamily = aString.String();
			cReply.FindString( "style", &aString );
			psProps->m_cStyle = aString.String();

			cReply.FindFloat( "size",     &psProps->m_vSize );
			cReply.FindFloat( "shear",    &psProps->m_vShear );
			cReply.FindFloat( "rotation", &psProps->m_vRotation );
		}
		return( 0 );
	}
	else
	{
		printf( "BApplication::GetDefaultFont() failed to send message to server: %s\n", strerror( errno ) );
		return( -1 );
	} 
}


port_id BApplication::CreateWindow( BView* pcTopView, const BRect& cFrame, const std::string& cTitle,
                                   int32 look, int32 feel, uint32 nFlags, uint32 nDesktopMask, port_id hEventPort, int32* phTopView )
{
	BMessage cReq( AS_CREATE_WINDOW );

	cReq.AddString( "title", cTitle.c_str() );
	cReq.AddInt32( "event_port", hEventPort );
	cReq.AddInt32( "look", look );
	cReq.AddInt32( "feel", feel );
	cReq.AddInt32( "flags", nFlags );
	cReq.AddInt32( "desktop_mask", nDesktopMask );
	cReq.AddRect( "frame", cFrame );
	cReq.AddPointer( "user_obj", pcTopView );

	BMessage cReply;

	BMessenger( m_hSrvAppPort ).SendMessage( &cReq, &cReply );

	port_id hCmdPort;
	cReply.FindInt32( "top_view", phTopView );
	cReply.FindInt32( "cmd_port", &hCmdPort );
	return( hCmdPort );
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

port_id BApplication::CreateWindow( BView* pcTopView, int hBitmapHandle, int32* phTopView )
{
	BMessage cReq( AR_OPEN_BITMAP_WINDOW );

	cReq.AddInt32( "bitmap_handle", hBitmapHandle );
	cReq.AddPointer( "user_obj", pcTopView );

	BMessage cReply;

	BMessenger( m_hSrvAppPort ).SendMessage( &cReq, &cReply );

	port_id hCmdPort;
	cReply.FindInt32( "top_view", phTopView );
	cReply.FindInt32( "cmd_port", &hCmdPort );

	return( hCmdPort );
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

int BApplication::CreateBitmap( BRect rect,
								color_space eColorSpc,
								int32 flags,
								int32 bytesperline,
								int32* phHandle,
								area_id* phArea,
								int32 *offset )
{
	AR_CreateBitmap_s      sReq;
	AR_CreateBitmapReply_s sReply;
	int32 msgCode;

	sReq.hReply            = fServerTo;
	sReq.flags             = flags;
	sReq.rect              = rect;
	sReq.eColorSpc         = eColorSpc;
	sReq.bytesperline      = bytesperline;

	if ( write_port( m_hSrvAppPort, AS_CREATE_BITMAP, &sReq, sizeof( sReq ) ) != 0 )
	{
		printf( "Error: BApplication::CreateBitmap() failed to send request to server\n" );
		return( -1 );
	}

	if ( read_port( fServerTo, &msgCode, &sReply, sizeof( sReply ) ) < 0 )
	{
		printf( "Error: BApplication::CreateBitmap() failed to read reply from server\n" );
		return( -1 );
	}

	if ( sReply.m_hHandle < 0 )
	{
		return( sReply.m_hHandle );
	}
	*phHandle = (int32)sReply.m_hHandle;
	*phArea   = (area_id)sReply.m_hArea;
	*offset   = sReply.areaOffset;
	return( 0 );
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

int BApplication::DeleteBitmap( int nBitmap )
{
	AR_DeleteBitmap_s sReq( nBitmap );

	if ( write_port( m_hSrvAppPort, AS_DELETE_BITMAP, &sReq, sizeof( sReq ) ) != 0 )
	{
		printf( "Error: BApplication::DeleteBitmap() failed to send request to server\n" );
		return( -1 );
	}
	
	return( 0 );
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

int BApplication::CreateSprite( const BRect& cFrame, int nBitmapHandle, uint32* pnHandle )
{
	AR_CreateSprite_s sReq( fServerTo, cFrame, nBitmapHandle );

	if ( write_port( m_hSrvAppPort, AR_CREATE_SPRITE, &sReq, sizeof( sReq ) ) != 0 )
	{
		printf( "Error: BApplication::CreateSprite() failed to send request to server\n" );
		return( -1 );
	}

	AR_CreateSpriteReply_s sReply;
	int32 msgCode;

	if ( read_port( fServerTo, &msgCode, &sReply, sizeof( sReply ) ) < 0 )
	{
		printf( "Error: BApplication::CreateSprite() failed to read reply from server\n" );
		return( -1 );
	}
	*pnHandle = sReply.m_nHandle;
	return( sReply.m_nError);
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

void BApplication::DeleteSprite( uint32 nSprite )
{
	AR_DeleteSprite_s sReq( nSprite );

	if ( write_port( m_hSrvAppPort, AR_DELETE_SPRITE, &sReq, sizeof( sReq ) ) != 0 )
	{
		printf( "Error: BApplication::DeleteSprite() failed to send request to server\n" );
	}
}

//----------------------------------------------------------------------------
// NAME:
// DESC:
// NOTE:
// SEE ALSO:
//----------------------------------------------------------------------------

void BApplication::MoveSprite( uint32 nSprite, const BPoint& cNewPos )
{
	AR_MoveSprite_s sReq( nSprite, cNewPos );

	if ( write_port( m_hSrvAppPort, AR_MOVE_SPRITE, &sReq, sizeof( sReq ) ) != 0 )
	{
		printf( "Error: BApplication::MoveSprite() failed to send request to server\n" );
	}
}


int BApplication::LockDesktop( screen_id nDesktop, BMessage* pcDesktopParams )
{
	BMessage cReq( AR_LOCK_DESKTOP );
	cReq.AddInt32( "desktop", nDesktop.id );
	return( BMessenger( m_hSrvAppPort ).SendMessage( &cReq, pcDesktopParams ) );
}


int BApplication::UnlockDesktop( int nCookie )
{
	return( 0 );
}


void BApplication::SetScreenMode( screen_id inDesktop, uint32 nValidityMask, screen_mode* psMode )
{
	BMessage cReq( AR_SET_SCREEN_MODE );

	cReq.AddInt32( "desktop", inDesktop.id );
	cReq.AddIPoint( "resolution", IPoint( psMode->m_nWidth, psMode->m_nHeight ) );
	cReq.AddInt32( "color_space", psMode->m_eColorSpace );
	cReq.AddFloat( "refresh_rate", psMode->m_vRefreshRate );
	cReq.AddFloat( "h_pos", psMode->m_vHPos );
	cReq.AddFloat( "v_pos", psMode->m_vVPos );
	cReq.AddFloat( "h_size", psMode->m_vHSize );
	cReq.AddFloat( "v_size", psMode->m_vVSize );

	BMessage cReply;
	BMessenger( m_hSrvAppPort ).SendMessage( &cReq, &cReply );
}


//------------------------------------------------------------------------------
void BApplication::BeginRectTracking(BRect r, bool trackWhole)
{
}
//------------------------------------------------------------------------------
void BApplication::EndRectTracking()
{
}
//------------------------------------------------------------------------------
void BApplication::get_scs()
{
}
//------------------------------------------------------------------------------
void BApplication::setup_server_heaps()
{
	// TODO: implement?

	// We may not need to implement this function or the XX_offs_to_ptr functions.
	// R5 sets up a couple of areas for various tasks having to do with the
	// app_server. Currently (7/29/04), the R1 app_server does not do this and
	// may never do this unless a significant need is found for it. --DW
}
//------------------------------------------------------------------------------
void* BApplication::rw_offs_to_ptr(uint32 offset)
{
	return NULL;	// TODO: implement
}
//------------------------------------------------------------------------------
void* BApplication::ro_offs_to_ptr(uint32 offset)
{
	return NULL;	// TODO: implement
}
//------------------------------------------------------------------------------
void* BApplication::global_ro_offs_to_ptr(uint32 offset)
{
	return NULL;	// TODO: implement
}
//------------------------------------------------------------------------------
void BApplication::connect_to_app_server()
{
	fServerFrom = find_port(SERVER_PORT_NAME);
	if (fServerFrom >= 0) {
		// Create the port so that the app_server knows where to send messages
		fServerTo = create_port(100, "a<fServerTo");
		if (fServerTo >= 0) {
			
			//We can't use BAppServerLink because be_app == NULL
			
			// AS_CREATE_APP:
	
			// Attach data:
			// 1) port_id - receiver port of a regular app
			// 2) port_id - looper port for this BApplication
			// 3) team_id - team identification field
			// 4) int32 - handler ID token of the app
			// 5) char * - signature of the regular app
			BMessage cReq(AS_CREATE_APP);
			assert( NULL == be_app );
  
			cReq.AddInt32("process_id", getpid());
			cReq.AddInt32("event_port", fMsgPort);
			cReq.AddString("app_name", fAppName);
  
			BMessage cReply;
  
			if ( BMessenger(fServerFrom).SendMessage( &cReq, &cReply ) >= 0 )
				cReply.FindInt32( "app_cmd_port", &m_hSrvAppPort );
			else
				printf( "BApplication::BApplication() failed to send message to server: %s\n", strerror( errno ) );
  
			for ( int i = 0 ; i < COL_COUNT ; ++i )
			{
				int32				intcolor;
		  
				if (cReply.FindInt32("cfg_colors", i, &intcolor) != 0)
					break;

				__set_default_color( static_cast<default_color_t>(i),
							   _get_rgb_color(intcolor));
			}
		} else
			fInitError = fServerTo;
	} else
		fInitError = fServerFrom;
}
//------------------------------------------------------------------------------
void BApplication::send_drag(BMessage* msg, int32 vs_token, BPoint offset, BRect drag_rect, BHandler* reply_to)
{
	// TODO: implement
}
//------------------------------------------------------------------------------
void BApplication::send_drag(BMessage* msg, int32 vs_token, BPoint offset, int32 bitmap_token, drawing_mode dragMode, BHandler* reply_to)
{
	// TODO: implement
}
//------------------------------------------------------------------------------
void BApplication::write_drag(_BSession_* session, BMessage* a_message)
{
	// TODO: implement
}
//------------------------------------------------------------------------------
bool BApplication::quit_all_windows(bool force)
{
	return false;	// TODO: implement?
}
//------------------------------------------------------------------------------
bool BApplication::window_quit_loop(bool, bool)
{
	// TODO: Implement and use in BApplication::QuitRequested()
	return false;
}
//------------------------------------------------------------------------------
void
BApplication::do_argv(BMessage* message)
{
	// TODO: Consider renaming this function to something 
	// a bit more descriptive, like "handle_argv_message()", 
	// or "HandleArgvMessage()" 
 
	ASSERT(message != NULL); 

	// build the argv vector 
	status_t error = B_OK; 
	int32 argc; 
	char **argv = NULL; 
	if (message->FindInt32("argc", &argc) == B_OK && argc > 0) { 
		argv = new char*[argc]; 
		for (int32 i = 0; i < argc; i++) 
			argv[i] = NULL; 
        
		// copy the arguments 
		for (int32 i = 0; error == B_OK && i < argc; i++) { 
			const char *arg = NULL; 
			error = message->FindString("argv", i, &arg); 
			if (error == B_OK && arg) { 
				argv[i] = new(std::nothrow) char[strlen(arg) + 1]; 
				if (argv[i]) 
					strcpy(argv[i], arg); 
				else 
					error = B_NO_MEMORY; 
			} 
		} 
	} 

	// call the hook 
	if (error == B_OK) 
		ArgvReceived(argc, argv); 

	// cleanup 
	if (argv) { 
		for (int32 i = 0; i < argc; i++) 
			delete[] argv[i]; 
		delete[] argv; 
	} 
}
//------------------------------------------------------------------------------
uint32 BApplication::InitialWorkspace()
{
	return fInitialWorkspace;
}
//------------------------------------------------------------------------------
int32
BApplication::count_windows(bool includeMenus) const
{
	int32 count = 0;
	BList windowList;
	if (get_window_list(&windowList, includeMenus) == B_OK)
		count = windowList.CountItems();
	
	return count;
}
//------------------------------------------------------------------------------
BWindow *
BApplication::window_at(uint32 index, bool includeMenus) const
{
	BList windowList;
	BWindow *window = NULL;
	if (get_window_list(&windowList, includeMenus) == B_OK) {
		if ((int32)index < windowList.CountItems())
			window = static_cast<BWindow *>(windowList.ItemAt(index));
	}
	
	return window;	
}
//------------------------------------------------------------------------------
status_t
BApplication::get_window_list(BList *list, bool includeMenus) const
{
	using namespace BPrivate;
	
	ASSERT(list);

	// Windows are BLoopers, so we can just check each BLooper to see if it's
	// a BWindow (or BMenuWindow)
	BObjectLocker<BLooperList> listLock(gLooperList);
	if (listLock.IsLocked()) {
		BLooper *looper = NULL;
		for (int32 i = 0; i < gLooperList.CountLoopers(); i++) {
			looper = gLooperList.LooperAt(i);
			if (dynamic_cast<BWindow *>(looper)) {
				if (includeMenus || dynamic_cast<BMenuWindow *>(looper) == NULL)
					list->AddItem(looper);
			}
		}
		
		return B_OK;
	}
	
	return B_ERROR;
}
//------------------------------------------------------------------------------
int32 BApplication::async_quit_entry(void* data)
{
	return 0;	// TODO: implement? not implemented?
}
//------------------------------------------------------------------------------

// check_app_signature
/*!	\brief Checks whether the supplied string is a valid application signature.

	An error message is printed, if the string is no valid app signature.

	\param signature The string to be checked.
	\return
	- \c B_OK: \a signature is a valid app signature.
	- \c B_BAD_VALUE: \a signature is \c NULL or no valid app signature.
*/
static
status_t
check_app_signature(const char *signature)
{
	bool isValid = false;
	BMimeType type(signature);
	if (type.IsValid() && !type.IsSupertypeOnly()
		&& BMimeType("application").Contains(&type)) {
		isValid = true;
	}
	if (!isValid) {
		printf("bad signature (%s), must begin with \"application/\" and "
			   "can't conflict with existing registered mime types inside "
			   "the \"application\" media type.\n", signature);
	}
	return (isValid ? B_OK : B_BAD_VALUE);
}

// looper_name_for
/*!	\brief Returns the looper name for a given signature.

	Normally this is "AppLooperPort", but in case of the registrar a
	special name.

	\return The looper name.
*/
static
const char*
looper_name_for(const char *signature)
{
	if (signature && !strcmp(signature, kRegistrarSignature))
		return kRosterPortName;
	return "AppLooperPort";
}


// fill_argv_message
/*!	\brief Fills the passed BMessage with B_ARGV_RECEIVED infos.
*/
static void 
fill_argv_message(BMessage *message)
{ 
	if (message) { 
		message->what = B_ARGV_RECEIVED; 

		// add current working directory        
		char cwd[B_PATH_NAME_LENGTH]; 
		if (getcwd(cwd, B_PATH_NAME_LENGTH)) 
			message->AddString("cwd", cwd); 
	} 
} 

/*
 * $Log $
 *
 * $Id  $
 *
 */

