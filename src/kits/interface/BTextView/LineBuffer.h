//------------------------------------------------------------------------------
//	Copyright (c) 2001-2004, Haiku, Inc.
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
//	File Name:		LineBuffer.h
//	Author:			Marc Flerackers (mflerackers@androme.be)
//	Description:	Line storage used by BTextView
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------
#include "SupportDefs.h"

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------
struct STELine {
	int32	fOffset;
	float	fHeight;
	float	fLineHeight;
	float	fWidth;
};

// Globals ---------------------------------------------------------------------

// _BLineBuffer_ class ---------------------------------------------------------
class _BLineBuffer_ {

public:
				_BLineBuffer_();
virtual			~_BLineBuffer_();

		void	InsertLine(STELine *line, int32);
		void	RemoveLines(int32 index, int32 count);
		void	RemoveLineRange(int32 from, int32 to);

		int32	OffsetToLine(int32 offset) const;
		int32	PixelToLine(float height) const;
			
		void	BumpOffset(int32 line, int32 offset);
		void	BumpOrigin(float, int32);

		int32	fBlockSize;
		int32	fItemCount;
		size_t	fPhysicalSize;
		STELine	*fObjectList;
};
//------------------------------------------------------------------------------

/*
 * $Log $
 *
 * $Id  $
 *
 */
