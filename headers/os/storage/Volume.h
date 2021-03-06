// ----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  File Name:		Directory.cpp
//
//	Description:	BVolume class
// ----------------------------------------------------------------------
/*!
	\file Volume.h
	BVolume interface declarations.
*/
#ifndef _VOLUME_H
#define _VOLUME_H

#include <sys/types.h>
#include <string>

#include <fs_info.h>
#include <Mime.h>
#include <StorageDefs.h>
#include <SupportDefs.h>
#include <Path.h>

#ifndef __APPLE__
#include <mntent.h>
#endif


#ifdef USE_OPENBEOS_NAMESPACE
namespace OpenBeOS {
#endif

class BDirectory;
class BBitmap;

class BVolume {
public:
	BVolume();
	BVolume(dev_t dev);
	BVolume(const BVolume &vol);
	virtual ~BVolume();

	status_t InitCheck() const;
	status_t SetTo(dev_t dev);
	void Unset();

	dev_t Device() const;

	status_t GetRootDirectory(BDirectory *directory) const;

	off_t Capacity() const;
	off_t FreeBytes() const;

	status_t GetName(char *name) const;
	status_t SetName(const char *name);

	status_t GetIcon(BBitmap *icon, icon_size which) const;

	bool IsRemovable() const;
	bool IsReadOnly() const;
	bool IsPersistent() const;
	bool IsShared() const;
	bool KnowsMime() const;
	bool KnowsAttr() const;
	bool KnowsQuery() const;

	bool operator==(const BVolume &volume) const;
	bool operator!=(const BVolume &volume) const;
	BVolume &operator=(const BVolume &volume);

private:
	friend class BVolumeRoster;

	virtual void _TurnUpTheVolume1();
	virtual void _TurnUpTheVolume2();
	virtual void _TurnUpTheVolume3();
	virtual void _TurnUpTheVolume4();
	virtual void _TurnUpTheVolume5();
	virtual void _TurnUpTheVolume6();
	virtual void _TurnUpTheVolume7();
	virtual void _TurnUpTheVolume8();

#ifndef __APPLE__
					BVolume(struct mntent* inMountEntry);
#endif

	void			_LoadVolumeProperties() const;

	dev_t		fDevice;
	status_t	fCStatus;
	int32		_reserved[8];

// These are mutable so we can do lazy loading from const members
// Worthwhile?  Skanky?  You decide.

	mutable	bool		mPropertiesLoaded;
	mutable bool		mIsShared;
	mutable bool		mIsRemovable;
	mutable bool		mIsReadOnly;
	mutable bool		mIsPersistent;
	mutable off_t		mCapacity;
	mutable off_t		mFreeBytes;
	mutable std::string	mName;
	mutable BPath		mDevicePath;
	mutable BPath		mMountPath;
};

#ifdef USE_OPENBEOS_NAMESPACE
}	// namespace OpenBeOS
#endif

#endif	// _VOLUME_H
