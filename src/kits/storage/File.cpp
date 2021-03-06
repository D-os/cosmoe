//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//---------------------------------------------------------------------
/*!
	\file File.cpp
	BFile implementation.
*/

#include <fsproto.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>

#include "kernel_interface.h"

#ifdef USE_OPENBEOS_NAMESPACE
namespace OpenBeOS {
#endif

// constructor
//! Creates an uninitialized BFile.
BFile::BFile()
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
}

// copy constructor
//! Creates a copy of the supplied BFile.
/*! If \a file is uninitialized, the newly constructed BFile will be, too.
	\param file the BFile object to be copied
*/
BFile::BFile(const BFile &file)
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
	*this = file;
}

// constructor
/*! \brief Creates a BFile and initializes it to the file referred to by
		   the supplied entry_ref and according to the specified open mode.
	\param ref the entry_ref referring to the file
	\param openMode the mode in which the file should be opened
	\see SetTo() for values for \a openMode
*/
BFile::BFile(const entry_ref *ref, uint32 openMode)
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
	SetTo(ref, openMode);
}

// constructor
/*! \brief Creates a BFile and initializes it to the file referred to by
		   the supplied BEntry and according to the specified open mode.
	\param entry the BEntry referring to the file
	\param openMode the mode in which the file should be opened
	\see SetTo() for values for \a openMode
*/
BFile::BFile(const BEntry *entry, uint32 openMode)
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
	SetTo(entry, openMode);
}

// constructor
/*! \brief Creates a BFile and initializes it to the file referred to by
		   the supplied path name and according to the specified open mode.
	\param path the file's path name 
	\param openMode the mode in which the file should be opened
	\see SetTo() for values for \a openMode
*/
BFile::BFile(const char *path, uint32 openMode)
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
	SetTo(path, openMode);
}

// constructor
/*! \brief Creates a BFile and initializes it to the file referred to by
		   the supplied path name relative to the specified BDirectory and
		   according to the specified open mode.
	\param dir the BDirectory, relative to which the file's path name is
		   given
	\param path the file's path name relative to \a dir
	\param openMode the mode in which the file should be opened
	\see SetTo() for values for \a openMode
*/
BFile::BFile(const BDirectory *dir, const char *path, uint32 openMode)
	 : BNode(),
	   BPositionIO(),
	   fMode(0)
{
	SetTo(dir, path, openMode);
}

// destructor
//! Frees all allocated resources.
/*!	If the file is properly initialized, the file's file descriptor is closed.
*/
BFile::~BFile()
{
	// Also called by the BNode destructor, but we rather try to avoid
	// problems with calling virtual functions in the base class destructor.
	// Depending on the compiler implementation an object may be degraded to
	// an object of the base class after the destructor of the derived class
	// has been executed.
	close_fd();
}

// SetTo
/*! \brief Re-initializes the BFile to the file referred to by the
		   supplied entry_ref and according to the specified open mode.
	\param ref the entry_ref referring to the file
	\param openMode the mode in which the file should be opened
	\a openMode must be a bitwise or of exactly one of the flags
	- \c B_READ_ONLY: The file is opened read only.
	- \c B_WRITE_ONLY: The file is opened write only.
	- \c B_READ_WRITE: The file is opened for random read/write access.
	and any number of the flags
	- \c B_CREATE_FILE: A new file will be created, if it does not already
	  exist.
	- \c B_FAIL_IF_EXISTS: If the file does already exist and B_CREATE_FILE is
	  set, SetTo() fails.
	- \c B_ERASE_FILE: An already existing file is truncated to zero size.
	- \c B_OPEN_AT_END: Seek() to the end of the file after opening.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a ref or bad \a openMode.
	- \c B_ENTRY_NOT_FOUND: File not found or failed to create file.
	- \c B_FILE_EXISTS: File exists and \c B_FAIL_IF_EXISTS was passed.
	- \c B_PERMISSION_DENIED: File permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\todo Currently implemented using BPrivate::Storage::entry_ref_to_path().
		  Reimplement!
*/
status_t
BFile::SetTo(const entry_ref *ref, uint32 openMode)
{
	Unset();
	char path[B_PATH_NAME_LENGTH];
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		error = BPrivate::Storage::entry_ref_to_path(ref, path,
													 B_PATH_NAME_LENGTH);
	}
	if (error == B_OK)
		error = SetTo(path, openMode);
	set_status(error);
	return error;
}

// SetTo
/*! \brief Re-initializes the BFile to the file referred to by the
		   supplied BEntry and according to the specified open mode.
	\param entry the BEntry referring to the file
	\param openMode the mode in which the file should be opened
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a entry or bad \a openMode.
	- \c B_ENTRY_NOT_FOUND: File not found or failed to create file.
	- \c B_FILE_EXISTS: File exists and \c B_FAIL_IF_EXISTS was passed.
	- \c B_PERMISSION_DENIED: File permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\todo Implemented using SetTo(entry_ref*, uint32). Check, if necessary
		  to reimplement!
*/
status_t
BFile::SetTo(const BEntry *entry, uint32 openMode)
{
	Unset();
	if (!entry)
		return (fCStatus = B_BAD_VALUE);
	if (entry->InitCheck() != B_OK)
		return (fCStatus = entry->InitCheck());
	entry_ref ref;
	status_t error;

	error = entry->GetRef(&ref);
	if (error == B_OK)
		error = SetTo(&ref, openMode);
	set_status(error);
	return error;
}

// SetTo
/*! \brief Re-initializes the BFile to the file referred to by the
		   supplied path name and according to the specified open mode.
	\param path the file's path name 
	\param openMode the mode in which the file should be opened
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a path or bad \a openMode.
	- \c B_ENTRY_NOT_FOUND: File not found or failed to create file.
	- \c B_FILE_EXISTS: File exists and \c B_FAIL_IF_EXISTS was passed.
	- \c B_PERMISSION_DENIED: File permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
*/
status_t
BFile::SetTo(const char *path, uint32 openMode)
{
	Unset();
	status_t result = B_OK;
	int newFd = -1;
	if (path) {
		// analyze openMode
		// Well, it's a bit schizophrenic to convert the B_* style openMode
		// to POSIX style openFlags, but to use O_RWMASK to filter openMode.
		BPrivate::Storage::OpenFlags openFlags = 0;
		switch (openMode & O_RWMASK) {
			case B_READ_ONLY:
 				openFlags = O_RDONLY;
				break;
			case B_WRITE_ONLY:
 				openFlags = O_WRONLY;
				break;
			case B_READ_WRITE:
 				openFlags = O_RDWR;
				break;
			default:
				result = B_BAD_VALUE;
				break;
		}
		if (result == B_OK) {
			if (openMode & B_ERASE_FILE)
				openFlags |= O_TRUNC;
			if (openMode & B_OPEN_AT_END)
				openFlags |= O_APPEND;
			if (openMode & B_CREATE_FILE) {
				openFlags |= O_CREAT;
				if (openMode & B_FAIL_IF_EXISTS)
					openFlags |= O_EXCL;
				result = BPrivate::Storage::open(path, openFlags, S_IREAD | S_IWRITE,
										  newFd);
			} else
				result = BPrivate::Storage::open(path, openFlags, newFd);
			if (result == B_OK)
				fMode = openFlags;
		}
	} else
		result = B_BAD_VALUE;
	// set the new file descriptor
	if (result == B_OK) {
		result = set_fd(newFd);
		if (result != B_OK)
			BPrivate::Storage::close(newFd);
	}
	// finally set the BNode status
	set_status(result);
	return result;
}

// SetTo
/*! \brief Re-initializes the BFile to the file referred to by the
		   supplied path name relative to the specified BDirectory and
		   according to the specified open mode.
	\param dir the BDirectory, relative to which the file's path name is
		   given
	\param path the file's path name relative to \a dir
	\param openMode the mode in which the file should be opened
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a dir or \a path or bad \a openMode.
	- \c B_ENTRY_NOT_FOUND: File not found or failed to create file.
	- \c B_FILE_EXISTS: File exists and \c B_FAIL_IF_EXISTS was passed.
	- \c B_PERMISSION_DENIED: File permissions didn't allow operation.
	- \c B_NO_MEMORY: Insufficient memory for operation.
	- \c B_LINK_LIMIT: Indicates a cyclic loop within the file system.
	- \c B_BUSY: A node was busy.
	- \c B_FILE_ERROR: A general file error.
	- \c B_NO_MORE_FDS: The application has run out of file descriptors.
	\todo Implemented using SetTo(BEntry*, uint32). Check, if necessary
		  to reimplement!
*/
status_t
BFile::SetTo(const BDirectory *dir, const char *path, uint32 openMode)
{
	Unset();
	status_t error = (dir && path ? B_OK : B_BAD_VALUE);
	BEntry entry;
	if (error == B_OK)
		error = entry.SetTo(dir, path);
	if (error == B_OK)
		error = SetTo(&entry, openMode);
	set_status(error);
	return error;
}

// IsReadable
//! Returns whether the file is readable.
/*!	\return
	- \c true, if the BFile has been initialized properly and the file has
	  been been opened for reading,
	- \c false, otherwise.
*/
bool
BFile::IsReadable() const
{
	return (InitCheck() == B_OK
			&& ((fMode & O_RWMASK) == O_RDONLY
				|| (fMode & O_RWMASK) == O_RDWR));
}

// IsWritable
//!	Returns whether the file is writable.
/*!	\return
	- \c true, if the BFile has been initialized properly and the file has
	  been opened for writing,
	- \c false, otherwise.
*/
bool
BFile::IsWritable() const
{
	return (InitCheck() == B_OK
			&& ((fMode & O_RWMASK) == O_WRONLY
				|| (fMode & O_RWMASK) == O_RDWR));
}

// Read
//!	Reads a number of bytes from the file into a buffer.
/*!	\param buffer the buffer the data from the file shall be written to
	\param size the number of bytes that shall be read
	\return the number of bytes actually read or an error code
*/
ssize_t
BFile::Read(void *buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	return BPrivate::Storage::read(get_fd(), buffer, size);
}

// ReadAt
/*!	\brief Reads a number of bytes from a certain position within the file
		   into a buffer.
	\param location the position (in bytes) within the file from which the
		   data shall be read
	\param buffer the buffer the data from the file shall be written to
	\param size the number of bytes that shall be read
	\return the number of bytes actually read or an error code
*/
ssize_t
BFile::ReadAt(off_t location, void *buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (location < 0)
		return B_BAD_VALUE;
	return BPrivate::Storage::read(get_fd(), buffer, location, size);
}

// Write
//!	Writes a number of bytes from a buffer into the file.
/*!	\param buffer the buffer containing the data to be written to the file
	\param size the number of bytes that shall be written
	\return the number of bytes actually written or an error code
*/
ssize_t
BFile::Write(const void *buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	return BPrivate::Storage::write(get_fd(), buffer, size);
}

// WriteAt
/*!	\brief Writes a number of bytes from a buffer at a certain position
		   into the file.
	\param location the position (in bytes) within the file at which the data
		   shall be written
	\param buffer the buffer containing the data to be written to the file
	\param size the number of bytes that shall be written
	\return the number of bytes actually written or an error code
*/
ssize_t
BFile::WriteAt(off_t location, const void *buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (location < 0)
		return B_BAD_VALUE;
	return BPrivate::Storage::write(get_fd(), buffer, location, size);
}

// Seek
//!	Seeks to another read/write position within the file.
/*!	It is allowed to seek past the end of the file. A subsequent call to
	Write() will pad the file with undefined data. Seeking before the
	beginning of the file will fail and the behavior of subsequent Read()
	or Write() invocations will be undefined.
	\param offset new read/write position, depending on \a seekMode relative
		   to the beginning or the end of the file or the current position
	\param seekMode:
		- \c SEEK_SET: move relative to the beginning of the file
		- \c SEEK_CUR: move relative to the current position
		- \c SEEK_END: move relative to the end of the file
	\return
	- the new read/write position relative to the beginning of the file
	- \c B_ERROR when trying to seek before the beginning of the file
	- \c B_FILE_ERROR, if the file is not properly initialized
*/
off_t
BFile::Seek(off_t offset, uint32 seekMode)
{
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return BPrivate::Storage::seek(get_fd(), offset, seekMode);
}

// Position
//!	Returns the current read/write position within the file.
/*!	\return
	- the current read/write position relative to the beginning of the file
	- \c B_ERROR, after a Seek() before the beginning of the file
	- \c B_FILE_ERROR, if the file has not been initialized
*/
off_t
BFile::Position() const
{
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return BPrivate::Storage::get_position(get_fd());
}

// SetSize
//!	Sets the size of the file.
/*!	If the file is shorter than \a size bytes it will be padded with
	unspecified data to the requested size. If it is larger, it will be
	truncated.
	Note: There's no problem with setting the size of a BFile opened in
	\c B_READ_ONLY mode, unless the file resides on a read only volume.
	\param size the new file size
	\return
	- \c B_OK, if everything went fine
	- \c B_NOT_ALLOWED, if trying to set the size of a file on a read only
	  volume
	- \c B_DEVICE_FULL, if there's not enough space left on the volume
*/
status_t
BFile::SetSize(off_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (size < 0)
		return B_BAD_VALUE;
	struct stat statData;
	statData.st_size = size;
	return set_stat(statData, WSTAT_SIZE);
}

// =
//!	Assigns another BFile to this BFile.
/*!	If the other BFile is uninitialized, this one will be too. Otherwise it
	will refer to the same file using the same mode, unless an error occurs.
	\param file the original BFile
	\return a reference to this BFile
*/
BFile &
BFile::operator=(const BFile &file)
{
	if (&file != this) {	// no need to assign us to ourselves
		Unset();
		if (file.InitCheck() == B_OK) {
			// duplicate the file descriptor
			int fd = -1;
			status_t status = BPrivate::Storage::dup(file.get_fd(), fd);
			// set it
			if (status == B_OK) {
				status = set_fd(fd);
				if (status == B_OK)
					fMode = file.fMode;
				else
					BPrivate::Storage::close(fd);
			}
			set_status(status);
		}
	}
	return *this;
}


// FBC
void BFile::_PhiloFile1() {}
void BFile::_PhiloFile2() {}
void BFile::_PhiloFile3() {}
void BFile::_PhiloFile4() {}
void BFile::_PhiloFile5() {}
void BFile::_PhiloFile6() {}


// get_fd
/*!	Returns the file descriptor.
	To be used instead of accessing the BNode's private \c fFd member directly.
	\return the file descriptor, or -1, if not properly initialized.
*/
int
BFile::get_fd() const
{
	return fFd;
}


#ifdef USE_OPENBEOS_NAMESPACE
};		// namespace OpenBeOS
#endif




