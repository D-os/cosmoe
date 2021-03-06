//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, Haiku, Inc.
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
//	File Name:		AppServer.cpp
//	Author:			DarkWyrm <bpmagic@columbus.rr.com>
//	Description:	main manager object for the app_server
//  
//------------------------------------------------------------------------------

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "../config.h"

#include <AppDefs.h>
#include <Entry.h>
#include <Path.h>
#include <Directory.h>
#include <PortLink.h>
#include <File.h>
#include <Message.h>
#include <RegistrarDefs.h>
#include "AppServer.h"
#include "ColorSet.h"
#include "DisplayDriver.h"
#include "ServerApp.h"
#include "ServerCursor.h"
#include "ServerProtocol.h"
#include "ServerWindow.h"
#include "DefaultDecorator.h"
#include "RGBColor.h"
#include "BitmapManager.h"
#include "CursorManager.h"
#include "Utils.h"
#include "FontServer.h"
#include "Desktop.h"

//#define DEBUG_KEYHANDLING
//#define DEBUG_SERVER

#ifdef DEBUG_KEYHANDLING
#	include <stdio.h>
#	define KBTRACE(x) printf x
#else
#	define KBTRACE(x) ;
#endif

#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif

// Globals

Desktop *desktop;

//! Used to access the app_server from new_decorator
AppServer *app_server=NULL;

//! Default background color for workspaces
RGBColor workspace_default_color(51,102,160);

//! System-wide GUI color object
ColorSet gui_colorset;

/*!
	\brief Constructor
	
	This loads the default fonts, allocates all the major global variables, spawns the main housekeeping
	threads, loads user preferences for the UI and decorator, and allocates various locks.
*/
AppServer::AppServer(void)
{
	fMousePort= create_port(200,SERVER_INPUT_PORT);
	fMessagePort= create_port(200,SERVER_PORT_NAME);

	fAppList= new BList(0);
	fQuittingServer= false;
	fExitPoller= false;
	make_decorator= NULL;
	
	// We need this in order for new_decorator to be able to instantiate new decorators
	app_server=this;

	// Create the font server and scan the proper directories.
	fontserver=new FontServer;
	fontserver->Lock();

	// Used for testing purposes

	// TODO: Re-enable scanning of all font directories when server is actually put to use
	fontserver->ScanDirectory("/usr/share/fonts/ttf/cosmoe");
	fontserver->SaveList();

	if(!fontserver->SetSystemPlain(DEFAULT_PLAIN_FONT_FAMILY,DEFAULT_PLAIN_FONT_STYLE,DEFAULT_PLAIN_FONT_SIZE))
		printf("Couldn't set plain to %s, %s %d pt\n",DEFAULT_PLAIN_FONT_FAMILY,
				DEFAULT_PLAIN_FONT_STYLE,DEFAULT_PLAIN_FONT_SIZE);
	if(!fontserver->SetSystemBold(DEFAULT_BOLD_FONT_FAMILY,DEFAULT_BOLD_FONT_STYLE,DEFAULT_BOLD_FONT_SIZE))
		printf("Couldn't set bold to %s, %s %d pt\n",DEFAULT_BOLD_FONT_FAMILY,
				DEFAULT_BOLD_FONT_STYLE,DEFAULT_BOLD_FONT_SIZE);
	if(!fontserver->SetSystemFixed(DEFAULT_FIXED_FONT_FAMILY,DEFAULT_FIXED_FONT_STYLE,DEFAULT_FIXED_FONT_SIZE))
		printf("Couldn't set fixed to %s, %s %d pt\n",DEFAULT_FIXED_FONT_FAMILY,
				DEFAULT_FIXED_FONT_STYLE,DEFAULT_FIXED_FONT_SIZE);
	fontserver->Unlock();

	
	// Load the GUI colors here and set the global set to the values contained therein. If this
	// is not possible, set colors to the defaults
	if(!LoadGUIColors(&gui_colorset))
		gui_colorset.SetToDefaults();

	InitDecorators();

	// Set up the Desktop
	desktop= new Desktop();
	desktop->Init();

	// Create the cursor manager. Object declared in CursorManager.cpp
	cursormanager= new CursorManager();
	cursormanager->SetCursor(B_CURSOR_DEFAULT);

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	bitmapmanager= new BitmapManager();

	// This is necessary to mediate access between the Poller and app_server threads
	fActiveAppLock= create_sem(1,"app_server_active_sem");

	// This locker is for app_server and Picasso to vie for control of the ServerApp list
	fAppListLock= create_sem(1,"app_server_applist_sem");

	// This locker is to mediate access to the make_decorator pointer
	fDecoratorLock= create_sem(1,"app_server_decor_sem");

	// Spawn our input-polling thread
	fPollerThreadID= spawn_thread(PollerThread, "Poller", B_NORMAL_PRIORITY, this);
	if (fPollerThreadID >= 0)
		resume_thread(fPollerThreadID);

	// Spawn our thread-monitoring thread
	fPicassoThreadID= spawn_thread(PicassoThread,"Picasso", B_NORMAL_PRIORITY, this);
	if (fPicassoThreadID >= 0)
		resume_thread(fPicassoThreadID);

	fDecoratorName="Default";
}

/*!
	\brief Destructor
	
	Reached only when the server is asked to shut down in Test mode. Kills all apps, shuts down the 
	desktop, kills the housekeeping threads, etc.
*/
AppServer::~AppServer(void)
{

	ServerApp *tempapp;
	int32 i;
	acquire_sem(fAppListLock);
	for(i=0;i<fAppList->CountItems();i++)
	{
		tempapp=(ServerApp *)fAppList->ItemAt(i);
		if(tempapp!=NULL)
			delete tempapp;
	}
	delete fAppList;
	release_sem(fAppListLock);

	delete bitmapmanager;
	delete cursormanager;

	delete desktop;

	// If these threads are still running, kill them - after this, if exit_poller
	// is deleted, who knows what will happen... These things will just return an
	// error and fail if the threads have already exited.
	kill_thread(fPollerThreadID);
	kill_thread(fPicassoThreadID);

	delete fontserver;
	
	make_decorator=NULL;
}

/*!
	\brief Thread function for polling and handling input messages
	\param data Pointer to the app_server to which the thread belongs
	\return Throwaway value - always 0
*/
int32 AppServer::PollerThread(void *data)
{
	// This thread handles nothing but input messages for mouse and keyboard
	AppServer *appserver=(AppServer*)data;
	BPortLink mousequeue(-1,appserver->fMousePort);
	int32 code=0;
	status_t err=B_OK;
	
	for(;;)
	{
		STRACE(("info: AppServer::PollerThread listening on port %ld.\n", appserver->fMousePort));
		err=mousequeue.GetNextReply(&code);
		
		if(err<B_OK)
		{
			STRACE(("PollerThread:mousequeue.GetNextReply failed\n"));
			continue;
		}
		
		switch(code)
		{
			// We don't need to do anything with these two, so just pass them
			// onto the active application. Eventually, we will end up passing 
			// them onto the window which is currently under the cursor.
			case B_MOUSE_DOWN:
			case B_MOUSE_UP:
			case B_MOUSE_WHEEL_CHANGED:
			case B_MOUSE_MOVED:
				desktop->MouseEventHandler(code,mousequeue);
				break;

			case B_KEY_DOWN:
			case B_KEY_UP:
			case B_UNMAPPED_KEY_DOWN:
			case B_UNMAPPED_KEY_UP:
			case B_MODIFIERS_CHANGED:
				desktop->KeyboardEventHandler(code,mousequeue);
				break;

			default:
				STRACE(("AppServer::Poller received unexpected code %lx\n",code));
				break;
		}
		
		if(appserver->fExitPoller)
			break;
	}
	return err;
}

/*!
	\brief Thread function for watching for dead apps
	\param data Pointer to the app_server to which the thread belongs
	\return Throwaway value - always 0
*/
int32 AppServer::PicassoThread(void *data)
{
	int32		i;
	AppServer	*appserver=(AppServer*)data;
	ServerApp	*app;
	for(;;)
	{
		acquire_sem(appserver->fAppListLock);
		for(i= 0; i < appserver->fAppList->CountItems(); i++)
		{
			app=(ServerApp*)appserver->fAppList->ItemAt(i);
			if(!app)
			{
				printf("PANIC: NULL app in app list\n");
				continue;
			}
			app->PingTarget();
		}
		release_sem(appserver->fAppListLock);
		
		// if poller thread has to exit, so do we - I just was too lazy
		// to rename the variable name. ;)
		if(appserver->fExitPoller)
			break;

		// we do this every other second so as not to suck *too* many CPU cycles
		snooze(2000000);
	}
	return 0;
}

/*!
	\brief The call that starts it all...
	\return Always 0
*/
thread_id AppServer::Run(void)
{
	MainLoop();
	return 0;
}


//! Main message-monitoring loop for the regular message port - no input messages!
void AppServer::MainLoop(void)
{
	BPortLink pmsg(-1,fMessagePort);
	int32 code=0;
	status_t err=B_OK;
	
	while(1)
	{
		STRACE(("info: AppServer::MainLoop listening on port %ld.\n", fMessagePort));
		err=pmsg.GetNextReply(&code);

		if(err<B_OK)
		{
			STRACE(("MainLoop:pmsg.GetNextReply failed\n"));
			continue;
		}
		
		switch(code)
		{
			case B_QUIT_REQUESTED:
			case AS_CREATE_APP:
			case AS_DELETE_APP:
			case AS_GET_SCREEN_MODE:
			case AS_UPDATED_CLIENT_FONTLIST:
			case AS_QUERY_FONTS_CHANGED:
			case AS_SET_UI_COLORS:
			case AS_GET_UI_COLOR:
			case AS_SET_DECORATOR:
			case AS_GET_DECORATOR:
			case AS_R5_SET_DECORATOR:
				DispatchMessage(code,pmsg);
				break;
			default:
			{
				STRACE(("Server::MainLoop received unexpected code %ld(offset %ld)\n",
						code,code-SERVER_TRUE));
				break;
			}

		}

		if(code==AS_DELETE_APP || (code==B_QUIT_REQUESTED && DISPLAYDRIVER!=HWDRIVER))
		{
			if(fQuittingServer== true && fAppList->CountItems()== 0)
				break;
		}
	}
}

/*!
	\brief Loads the specified decorator and sets the system's decorator to it.
	\param path Path to the decorator to load
	\return True if successful, false if not.

	If the server cannot load the specified decorator, nothing changes. Passing a 
	NULL string to this function sets the decorator	to the internal one.
*/
bool AppServer::LoadDecorator(const char *path)
{
	// Loads a window decorator based on the supplied path and forces a decorator update.
	// If it cannot load the specified decorator, it will retain the current one and
	// return false. Note that passing a NULL string to this function sets the decorator
	// to the internal one.

	// passing the string "Default" will set the window decorator to the app_server's
	// internal one
	if(!path)
	{
		make_decorator= NULL;
		return true;
	}
	
	create_decorator	*pcreatefunc= NULL;
	status_t			stat;
	image_id			addon;
	
	addon= load_add_on(path);
	if(addon < 0)
		return false;

	// As of now, we do nothing with decorator versions, but the possibility exists
	// that the API will change even though I cannot forsee any reason to do so. If
	// we *did* do anything with decorator versions, the assignment to a global would
	// go here.

	// Get the instantiation function
	stat= get_image_symbol(addon, "instantiate_decorator", B_SYMBOL_TYPE_TEXT, (void**)&pcreatefunc);
	if(stat != B_OK)
	{
		STRACE(( "Error: window decorator '%s' does not export instantiate_decorator()\n", path ));
		unload_add_on(addon);
		return false;
	}
	
	BPath temppath(path);
	fDecoratorName=temppath.Leaf();
	
	acquire_sem(fDecoratorLock);
	make_decorator=pcreatefunc;
	fDecoratorID=addon;
	release_sem(fDecoratorLock);
	return true;
}

//! Loads decorator settings on disk or the default if settings are invalid
void AppServer::InitDecorators(void)
{
	BMessage settings;

	BDirectory dir,newdir;
	if(dir.SetTo(SERVER_SETTINGS_DIR)==B_ENTRY_NOT_FOUND)
		create_directory(SERVER_SETTINGS_DIR,0777);

	BString path(SERVER_SETTINGS_DIR);
	path+="DecoratorSettings";
	BFile file(path.String(),B_READ_ONLY);

	if(file.InitCheck()==B_OK)
	{
		if(settings.Unflatten(&file)==B_OK)
		{
			BString itemtext;
			if(settings.FindString("decorator",&itemtext)==B_OK)
			{
				path.SetTo(DECORATORS_DIR);
				path+=itemtext;
				if(LoadDecorator(path.String()))
					return;
			}
		}
	}

	// We got this far, so something must have gone wrong. We set make_decorator
	// to NULL so that the decorator allocation routine knows to utilize the included
	// default decorator instead of an addon.
	make_decorator=NULL;
}

/*!
	\brief Message handling function for all messages sent to the app_server
	\param code ID of the message sent
	\param buffer Attachment buffer for the message.
	
*/
void AppServer::DispatchMessage(int32 code, BPortLink &msg)
{
	switch(code)
	{
		case AS_CREATE_APP:
		{
			// Create the ServerApp to node monitor a new BApplication

			// Attached data:
			// 1) port_id - receiver port of a regular app
			// 2) port_id - client looper port - for send messages to the client
			// 2) team_id - app's team ID
			// 3) int32 - handler token of the regular app
			// 4) char * - signature of the regular app

			// Find the necessary data
			team_id	clientTeamID=-1;
			port_id	clientLooperPort=-1;
			port_id app_port=-1;
			int32 htoken=B_NULL_TOKEN;
			char *app_signature=NULL;

			msg.Read<port_id>(&app_port);
			msg.Read<port_id>(&clientLooperPort);
			msg.Read<team_id>(&clientTeamID);
			msg.Read<int32>(&htoken);
			msg.ReadString(&app_signature);
			
			// Create the ServerApp subthread for this app
			acquire_sem(fAppListLock);
			
			port_id server_listen=create_port(DEFAULT_MONITOR_PORT_SIZE, app_signature);
			if(server_listen<B_OK)
			{
				release_sem(fAppListLock);
				printf("No more ports left. Time to crash. Have a nice day! :)\n");
				break;
			}
			ServerApp *newapp=NULL;
			newapp= new ServerApp(app_port,server_listen, clientLooperPort, clientTeamID, 
					htoken, app_signature);

			// add the new ServerApp to the known list of ServerApps
			fAppList->AddItem(newapp);

			release_sem(fAppListLock);

			BPortLink replylink(app_port);
			replylink.StartMessage(SERVER_TRUE);
			replylink.Attach<int32>(newapp->fMessagePort);
			replylink.Flush();

			// This is necessary because BPortLink::ReadString allocates memory
			if(app_signature)
				free(app_signature);

			break;
		}
		case AS_DELETE_APP:
		{
			// Delete a ServerApp. Received only from the respective ServerApp when a
			// BApplication asks it to quit.
			
			// Attached Data:
			// 1) thread_id - thread ID of the ServerApp to be deleted
			
			int32 	i=0,
					appnum=fAppList->CountItems();
			
			ServerApp *srvapp=NULL;
			thread_id srvapp_id=-1;

			if(msg.Read<thread_id>(&srvapp_id)<B_OK)
				break;

			acquire_sem(fAppListLock);

			// Run through the list of apps and nuke the proper one
			for(i= 0; i < appnum; i++)
			{
				srvapp=(ServerApp *)fAppList->ItemAt(i);

				if(srvapp != NULL && srvapp->fMonitorThreadID== srvapp_id)
				{
					srvapp=(ServerApp *)fAppList->RemoveItem(i);
					if(srvapp)
					{
						status_t		temp;
						wait_for_thread(srvapp_id, &temp);
						delete srvapp;
						srvapp= NULL;
					}
					break;	// jump out of our for() loop
				}
			}

			release_sem(fAppListLock);
			break;
		}
		case AS_UPDATED_CLIENT_FONTLIST:
		{
			// received when the client-side global font list has been
			// refreshed
			fontserver->Lock();
			fontserver->FontsUpdated();
			fontserver->Unlock();
			break;
		}
		case AS_QUERY_FONTS_CHANGED:
		{
			// Client application is asking if the font list has changed since
			// the last client-side refresh

			// Attached data:
			// 1) port_id reply port
			
			fontserver->Lock();
			bool needs_update=fontserver->FontsNeedUpdated();
			fontserver->Unlock();
			
			// Seeing how the client merely wants an answer, we'll skip the BPortLink
			// and all its overhead and just write the code to port.
			port_id replyport;
			if (msg.Read<port_id>(&replyport) < B_OK)
				break;
			BPortLink replylink(replyport);
			replylink.StartMessage(needs_update ? SERVER_TRUE : SERVER_FALSE);
			replylink.Flush();
			break;
		}
		case AS_SET_UI_COLORS:
		{
			// Client application is asking to set all the system colors at once
			// using a ColorSet object
			
			// Attached data:
			// 1) ColorSet new colors to use
			
			gui_colorset.Lock();
			msg.Read<ColorSet>(&gui_colorset);
			gui_colorset.Unlock();
			Broadcast(AS_UPDATE_COLORS);
			break;
		}
		case AS_SET_DECORATOR:
		{
			// Received from an application when the user wants to set the window
			// decorator to a new one
			
			// Attached Data:
			// char * name of the decorator in the decorators path to use
			
			char *decname=NULL;
			msg.ReadString(&decname);
			if(decname)
			{
				if(strcmp(decname,"Default")!=0)
				{
					BString decpath;
					decpath.SetTo(DECORATORS_DIR);
					decpath+=decname;
					if(LoadDecorator(decpath.String()))
						Broadcast(AS_UPDATE_DECORATOR);
				}
				else
				{
					LoadDecorator(NULL);
					Broadcast(AS_UPDATE_DECORATOR);
				}
			}
			free(decname);
			
			break;
		}
		case AS_GET_DECORATOR:
		{
			// Attached Data:
			// 1) port_id reply port
			
			port_id replyport=-1;
			if(msg.Read<port_id>(&replyport)<B_OK)
				return;
			
			BPortLink replylink(replyport);
			replylink.StartMessage(AS_GET_DECORATOR);
			replylink.AttachString(fDecoratorName.String());
			replylink.Flush();
			break;
		}
		case AS_R5_SET_DECORATOR:
		{
			// Sort of supports Tracker's nifty Easter Egg. It was easy to do and 
			// it's kind of neat, so why not?
			
			// Attached Data:
			// char * name of the decorator in the decorators path to use
			
			int32 decindex=0;
			if(msg.Read<int32>(&decindex)<B_OK)
				break;
			
			BString decpath;
			decpath.SetTo(DECORATORS_DIR);
			switch(decindex)
			{
				case 0: decpath+="BeOS"; break;
				case 1: decpath+="AmigaOS"; break;
				case 2: decpath+="Windows"; break;
				case 3: decpath+="MacOS"; break;
				default:
					break;
			}
			if(LoadDecorator(decpath.String()))
				Broadcast(AS_UPDATE_DECORATOR);

			break;
		}
		case AS_GET_SCREEN_MODE:
		{
			// Synchronous message call to get the stats on the current screen mode
			// in the app_server. Simply a hack in place for the Input Server until
			// BScreens are done.
			
			// Attached Data:
			// 1) port_id - port to reply to
			
			// Returned Data:
			// 1) int32 width
			// 2) int32 height
			// 3) int depth
			
			display_mode dmode;
			fDriver->GetMode(&dmode);
			
			port_id replyport=-1;
			if(msg.Read<port_id>(&replyport)<B_OK)
				break;
			
			BPortLink replylink(replyport);
			replylink.StartMessage(AS_GET_SCREEN_MODE);
			replylink.Attach<display_mode>(dmode);
			replylink.Flush();
			break;
		}
		case B_QUIT_REQUESTED:
		{
			// Attached Data:
			// none
			
			// We've been asked to quit, so (for now) broadcast to all
			// test apps to quit. This situation will occur only when the server
			// is compiled as a regular Be application.
			if(DISPLAYDRIVER== HWDRIVER)
				break;
			
			Broadcast(AS_QUIT_APP);

			// we have to wait until *all* threads have finished!
			ServerApp	*app= NULL;
			acquire_sem(fAppListLock);
			thread_info tinfo;
			
			for(int32 i= 0; i < fAppList->CountItems(); i++)
			{
				app=(ServerApp*)fAppList->ItemAt(i);
				if(!app)
					continue;

				// Instead of calling wait_for_thread, we will wait a bit, check for the
				// thread_id. We will only wait so long, because then the app is probably crashed
				// or hung. Seeing that being the case, we'll kill its BApp team and fake the
				// quit message
				if(get_thread_info(app->fMonitorThreadID, &tinfo)==B_OK)
				{
					bool killteam=true;
					
					for(int32 j=0; j<5; j++)
					{
						snooze(1000);	// wait half a second for it to quit
						if(get_thread_info(app->fMonitorThreadID, &tinfo)!=B_OK)
						{
							killteam=false;
							break;
						}
					}
					
					if(killteam)
					{
						kill_team(app->ClientTeamID());
						app->PostMessage(B_QUIT_REQUESTED);
					}
				}
			}
			release_sem(fAppListLock);

			// When we delete the last ServerApp, we can exit the server
			fQuittingServer=true;
			fExitPoller=true;

			// also wait for picasso thread
			kill_thread(fPicassoThreadID);

			// poller thread is stuck reading messages from its input port
			// so, there is no cleaner way to make it quit, other than killing it!
			kill_thread(fPollerThreadID);

			// we are now clear to exit
			break;
		}
		case AS_SET_SYSCURSOR_DEFAULTS:
		{
			cursormanager->SetDefaults();
			break;
		}
		default:
			// we should never get here.
			break;
	}
}

/*!
	\brief Send a quick (no attachments) message to all applications
	
	Quite useful for notification for things like server shutdown, system 
	color changes, etc.
*/
void AppServer::Broadcast(int32 code)
{
	ServerApp	*app= NULL;
	
	acquire_sem(fAppListLock);
	for(int32 i= 0; i < fAppList->CountItems(); i++)
	{
		app=(ServerApp*)fAppList->ItemAt(i);
		if(!app)
			{ printf("PANIC in AppServer::Broadcast()\n"); continue; }
		app->PostMessage(code);
	}
	release_sem(fAppListLock);
}

/*!
	\brief Finds the application with the given signature
	\param sig MIME signature of the application to find
	\return the corresponding ServerApp or NULL if not found
	
	This call should be made only when necessary because it locks the app list 
	while it does its searching.
*/
ServerApp *AppServer::FindApp(const char *sig)
{
	if(!sig)
		return NULL;

	ServerApp *foundapp=NULL;

	acquire_sem(fAppListLock);

	for(int32 i=0; i<fAppList->CountItems();i++)
	{
		foundapp=(ServerApp*)fAppList->ItemAt(i);
		if(foundapp && foundapp->fSignature==sig)
		{
			release_sem(fAppListLock);
			return foundapp;
		}
	}

	release_sem(fAppListLock);
	
	// couldn't find a match
	return NULL;
}

/*!
	\brief Creates a new decorator instance
	\param rect Frame size
	\param title Title string for the "window"
	\param wlook Window look type. See Window.h
	\param wfeel Window feel type. See Window.h
	\param wflags Window flags. See Window.h
	
	If a decorator has not been set, we use the default one packaged in with the app_server 
	being that we can't do anything with a window without one.
*/
Decorator *new_decorator(BRect rect, const char *title, int32 wlook, int32 wfeel,
	int32 wflags, DisplayDriver *ddriver)
{
	Decorator *dec=NULL;
// Temporary solution!
	dec=new DefaultDecorator(rect,wlook,wfeel,wflags);
/*	if(!app_server->make_decorator)
		dec=new DefaultDecorator(rect,wlook,wfeel,wflags);
	else
		dec=app_server->make_decorator(rect,wlook,wfeel,wflags);
*/
	gui_colorset.Lock();
	dec->SetDriver(ddriver);
	dec->SetColors(gui_colorset);
	dec->SetTitle(title);
	gui_colorset.Unlock();

	return dec;
}

/*!
	\brief Entry function to run the entire server
	\param argc Number of command-line arguments present
	\param argv String array of the command-line arguments
	\return -1 if the app_server is already running, 0 if everything's OK.
*/
int main( int argc, char** argv )
{
	STRACE(( "Appserver Alive %ld\n", find_thread(NULL) ));

	// There can be only one....
	if(find_port(SERVER_PORT_NAME)!=B_NAME_NOT_FOUND)
		return -1;

	STRACE(("There can be only one... app_server, that is.  We're it.\n"));

	AppServer	app_server;
	app_server.Run();
	return 0;
}
